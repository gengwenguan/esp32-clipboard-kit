#include "usb_hid.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "soc/usb_serial_jtag_reg.h"
#include "soc/system_reg.h"
#include "soc/usb_serial_jtag_struct.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "USB_HID";
static bool s_usb_enabled = false;
static char s_usb_string[USB_STRING_MAX_LEN + 1] = {0};

/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

// HID Report Descriptor for a standard keyboard
static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

/**
 * @brief String descriptor
 */
const char* hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",             // 1: Manufacturer
    "TinyUSB Device",      // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "Example HID interface",  // 4: HID
};

/**
 * @brief Configuration descriptor
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

// Invoked when received SET_REPORT control request
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
}

/********* Application ***************/

bool usb_hid_is_active(void)
{
    return s_usb_enabled;
}

void usb_hid_set_enabled(bool enabled)
{
    if (enabled && !s_usb_enabled) {
        ESP_LOGI(TAG, "Enabling USB HID - installing driver");
        
        // Restore PHY to USB OTG (TinyUSB)
        // 1. Enable SW control of muxing USB OTG vs USJ
        SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL);
        // 2. Select Internal USB FSLS PHY for USB OTG (1)
        SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_USB_PHY_SEL);
        
        tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

        tusb_cfg.descriptor.device = NULL;
        tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
        tusb_cfg.descriptor.string = hid_string_descriptor;
        tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
        
    #if (TUD_OPT_HIGH_SPEED)
        tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
    #endif // TUD_OPT_HIGH_SPEED

        esp_err_t err = tinyusb_driver_install(&tusb_cfg);
        if (err == ESP_OK) {
            s_usb_enabled = true;
            ESP_LOGI(TAG, "USB HID driver installed");
        } else {
            ESP_LOGE(TAG, "Failed to install USB HID driver: %s", esp_err_to_name(err));
        }
    } else if (!enabled && s_usb_enabled) {
        ESP_LOGI(TAG, "Disabling USB HID - uninstalling driver");
        esp_err_t err = tinyusb_driver_uninstall();
        if (err == ESP_OK) {
            s_usb_enabled = false;
            ESP_LOGI(TAG, "USB HID driver uninstalled");
            
            // Restore USB Serial JTAG
            // 1. Reset USB OTG (TinyUSB controller) to stop it from interfering
            SET_PERI_REG_MASK(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_USB_RST);
            // 2. Disable USB OTG Clock
            CLEAR_PERI_REG_MASK(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_USB_CLK_EN);
            
            // 3. Enable USB Serial/JTAG Clock FIRST
            SET_PERI_REG_MASK(SYSTEM_PERIP_CLK_EN1_REG, SYSTEM_USB_DEVICE_CLK_EN);

            // 4. Manually switch PHY to USB Serial/JTAG using RTC controller
            // Enable SW control of muxing USB OTG vs USJ to the internal USB FSLS PHY
            SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL);
            // 0 - Internal USB FSLS PHY is mapped to the USJ. USB Wrap mapped to external PHY
            CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_USB_PHY_SEL);

            // 5. Reset USB Serial/JTAG controller
            SET_PERI_REG_MASK(SYSTEM_PERIP_RST_EN1_REG, SYSTEM_USB_DEVICE_RST);
            CLEAR_PERI_REG_MASK(SYSTEM_PERIP_RST_EN1_REG, SYSTEM_USB_DEVICE_RST);
            
            // 6. Select internal PHY for Serial/JTAG
            USB_SERIAL_JTAG.conf0.phy_sel = 0; 
            
            // 7. Force Detach (Disable USB pads)
            USB_SERIAL_JTAG.conf0.usb_pad_enable = 0;
            vTaskDelay(pdMS_TO_TICKS(100)); // Wait for host to detect detach

            // 8. Enable USB pads (Attach)
            USB_SERIAL_JTAG.conf0.usb_pad_enable = 1; 

            ESP_LOGI(TAG, "USB Serial JTAG restored");
        } else {
            ESP_LOGE(TAG, "Failed to uninstall USB HID driver: %s", esp_err_to_name(err));
        }
    }
}

static void send_key(uint8_t modifier, uint8_t keycode)
{
    if (s_usb_enabled && tud_mounted()) {
        uint8_t keys[6] = {keycode, 0, 0, 0, 0, 0};
        tud_hid_n_keyboard_report(0, 0, modifier, keys);
        vTaskDelay(pdMS_TO_TICKS(10));
        
        uint8_t empty[6] = {0};
        tud_hid_n_keyboard_report(0, 0, 0, empty);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void char_to_hid(char c, uint8_t *modifier, uint8_t *keycode)
{
    *modifier = 0;
    *keycode = 0;
    
    if (c >= 'a' && c <= 'z') {
        *keycode = HID_KEY_A + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
        *modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
        *keycode = HID_KEY_A + (c - 'A');
    } else if (c >= '1' && c <= '9') {
        *keycode = HID_KEY_1 + (c - '1');
    } else if (c == '0') {
        *keycode = HID_KEY_0;
    } else {
        // Handle some common symbols
        switch (c) {
            case '!': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_1; break;
            case '@': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_2; break;
            case '#': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_3; break;
            case '$': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_4; break;
            case '%': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_5; break;
            case '^': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_6; break;
            case '&': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_7; break;
            case '*': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_8; break;
            case '(': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_9; break;
            case ')': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_0; break;
            case '-': *keycode = HID_KEY_MINUS; break;
            case '_': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_MINUS; break;
            case '=': *keycode = HID_KEY_EQUAL; break;
            case '+': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_EQUAL; break;
            case '[': *keycode = HID_KEY_BRACKET_LEFT; break;
            case '{': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_BRACKET_LEFT; break;
            case ']': *keycode = HID_KEY_BRACKET_RIGHT; break;
            case '}': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_BRACKET_RIGHT; break;
            case '\\': *keycode = HID_KEY_BACKSLASH; break;
            case '|': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_BACKSLASH; break;
            case ';': *keycode = HID_KEY_SEMICOLON; break;
            case ':': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_SEMICOLON; break;
            case '\'': *keycode = HID_KEY_APOSTROPHE; break;
            case '\"': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_APOSTROPHE; break;
            case ',': *keycode = HID_KEY_COMMA; break;
            case '<': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_COMMA; break;
            case '.': *keycode = HID_KEY_PERIOD; break;
            case '>': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_PERIOD; break;
            case '/': *keycode = HID_KEY_SLASH; break;
            case '?': *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; *keycode = HID_KEY_SLASH; break;
            case ' ': *keycode = HID_KEY_SPACE; break;
            case '\n': *keycode = HID_KEY_ENTER; break;
            case '\r': *keycode = HID_KEY_ENTER; break;
            // Map ASCII control codes to Function Keys
            case 0x01: *keycode = HID_KEY_F1; break;
            case 0x02: *keycode = HID_KEY_F2; break;
            case 0x03: *keycode = HID_KEY_F3; break;
            case 0x04: *keycode = HID_KEY_F4; break;
            case 0x05: *keycode = HID_KEY_F5; break;
            case 0x06: *keycode = HID_KEY_F6; break;
            case 0x07: *keycode = HID_KEY_F7; break;
            case 0x18: *keycode = HID_KEY_F8; break; // 0x08 is BS
            case 0x19: *keycode = HID_KEY_F9; break; // 0x09 is TAB
            case 0x1A: *keycode = HID_KEY_F10; break; // 0x0A is LF (Enter)
            case 0x0B: *keycode = HID_KEY_F11; break;
            case 0x0C: *keycode = HID_KEY_F12; break;
            default: break;
        }
    }
}

void usb_hid_set_string(const char *string)
{
    if (string) {
        strncpy(s_usb_string, string, USB_STRING_MAX_LEN);
        s_usb_string[USB_STRING_MAX_LEN] = '\0';
        ESP_LOGI(TAG, "USB String set to: %s", s_usb_string);
    }
}

const char *usb_hid_get_string(void)
{
    return s_usb_string;
}

esp_err_t usb_hid_save_string(const char *string)
{
    if (string == NULL) return ESP_ERR_INVALID_ARG;
    
    // Update internal state
    usb_hid_set_string(string);
    
    // Save to NVS
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(my_handle, "usb_str", string);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) setting NVS string", esp_err_to_name(err));
    } else {
        err = nvs_commit(my_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error (%s) committing NVS", esp_err_to_name(err));
        }
    }
    
    nvs_close(my_handle);
    return err;
}

esp_err_t usb_hid_load_string(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
             ESP_LOGE(TAG, "Error (%s) opening NVS handle", esp_err_to_name(err));
        }
        return err;
    }
    
    char buffer[USB_STRING_MAX_LEN + 1];
    size_t required_size = sizeof(buffer);
    err = nvs_get_str(my_handle, "usb_str", buffer, &required_size);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded USB String from NVS: %s", buffer);
        usb_hid_set_string(buffer);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No USB String found in NVS");
    } else {
        ESP_LOGE(TAG, "Error (%s) reading NVS string", esp_err_to_name(err));
    }
    
    nvs_close(my_handle);
    return err;
}

static QueueHandle_t s_usb_hid_queue = NULL;

static void usb_hid_task(void *arg)
{
    char c;
    while (1) {
        if (xQueueReceive(s_usb_hid_queue, &c, portMAX_DELAY)) {
            if (s_usb_enabled && tud_mounted()) {
                uint8_t modifier = 0;
                uint8_t keycode = 0;
                char_to_hid(c, &modifier, &keycode);
                if (keycode != 0) {
                    send_key(modifier, keycode);
                }
            } else {
                // Drain queue if not connected to avoid stale data when re-connecting
                // But maybe user wants it to type when connected?
                // Let's just consume it.
            }
        }
    }
}

void usb_hid_init(void)
{
    if (s_usb_hid_queue == NULL) {
        s_usb_hid_queue = xQueueCreate(1024, sizeof(char)); // Buffer for ~1000 chars
        if (s_usb_hid_queue) {
            xTaskCreate(usb_hid_task, "usb_hid", 4096, NULL, 5, NULL);
            ESP_LOGI(TAG, "USB HID Task started");
        }
    }
}

void usb_hid_send_string(void)
{

    if (!s_usb_enabled) {
        ESP_LOGW(TAG, "USB not enabled, cannot send string");
        return;
    }
    
    if (s_usb_hid_queue == NULL) {
        ESP_LOGE(TAG, "USB HID queue not initialized");
        return;
    }

    ESP_LOGI(TAG, "Queueing string: %s", s_usb_string);
    
    const char *p = s_usb_string;
    if (strlen(p) == 0) {
        ESP_LOGW(TAG, "String is empty");
        return;
    }
    
    while (*p) {
        if (xQueueSend(s_usb_hid_queue, p, 0) != pdTRUE) {
            ESP_LOGW(TAG, "USB HID Queue full, dropped char: %c", *p);
        }
        p++;
    }
}

