#include "lcd_display.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9107.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "font.h"
#include <string.h>
#include <assert.h>

/* LCD Pin Definition */
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_SCLK           5
#define EXAMPLE_PIN_NUM_MOSI           4
#define EXAMPLE_PIN_NUM_MISO           -1
#define EXAMPLE_PIN_NUM_LCD_DC         38
#define EXAMPLE_PIN_NUM_LCD_RST        10
#define EXAMPLE_PIN_NUM_LCD_CS         9
#define EXAMPLE_PIN_NUM_LCD_BL         37
#define EXAMPLE_LCD_H_RES              128
#define EXAMPLE_LCD_V_RES              128
#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8

static const char *TAG = "lcd_display";

// Global panel handle
static esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t lcd_trans_done_sem = NULL;
static SemaphoreHandle_t lcd_mutex = NULL;
static bool is_inverted = true;

static bool notify_lcd_draw_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(lcd_trans_done_sem, &high_task_awoken);
    return high_task_awoken == pdTRUE;
}

void lcd_clear(void)
{
    lcd_draw_color_bar(0, 0, EXAMPLE_LCD_H_RES - 1, EXAMPLE_LCD_V_RES - 1, 0x0000);
}

void lcd_draw_color_bar(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color)
{
    if (!panel_handle) return;
    
    if (lcd_mutex) xSemaphoreTakeRecursive(lcd_mutex, portMAX_DELAY);

    int width = x_end - x_start + 1;
    int height = y_end - y_start + 1;
    
    // Use a static buffer to avoid allocation overhead and ensure validity during async DMA
    static uint16_t *buffer = NULL;
    if (buffer == NULL) {
        buffer = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_DMA);
        assert(buffer);
    }

    // Wait for previous transaction to finish before touching the buffer
    if (lcd_trans_done_sem) {
        xSemaphoreTake(lcd_trans_done_sem, portMAX_DELAY);
    }

    if (buffer) {
        for (int i = 0; i < width * height; i++) {
            buffer[i] = (color >> 8) | (color << 8); // Swap bytes for RGB565 big-endian
        }
        esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end + 1, y_end + 1, buffer);
    }
    
    if (lcd_mutex) xSemaphoreGiveRecursive(lcd_mutex);
}

static void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color)
{
    if (!panel_handle) return;
    if (c < ' ' || c > '~') return; // Out of bounds
    
    // 8x8 character buffer
    static uint16_t *char_buffer = NULL;
    if (char_buffer == NULL) {
        char_buffer = heap_caps_malloc(8 * 8 * sizeof(uint16_t), MALLOC_CAP_DMA);
        assert(char_buffer);
    }

    // Wait for previous transaction
    if (lcd_trans_done_sem) {
        xSemaphoreTake(lcd_trans_done_sem, portMAX_DELAY);
    }

    const uint8_t *glyph = font8x8_basic[(int)(c - ' ')];
    
    for (int col = 0; col < 8; col++) {
        uint8_t col_data = glyph[col];
        for (int row = 0; row < 8; row++) {
            // Check bit (row 0 is LSB)
            uint16_t pixel = (col_data & (1 << row)) ? color : bg_color;
            // Swap bytes for RGB565 big-endian
            char_buffer[row * 8 + col] = (pixel >> 8) | (pixel << 8);
        }
    }
    
    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 8, y + 8, char_buffer);
}

void lcd_draw_string(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t bg_color)
{
    if (lcd_mutex) xSemaphoreTakeRecursive(lcd_mutex, portMAX_DELAY);
    
    uint16_t current_x = x;
    uint16_t current_y = y;
    
    while (*text) {
        if (*text == '\n') {
            current_x = x;
            current_y += 8;
        } else {
            if (current_x + 8 > EXAMPLE_LCD_H_RES) {
                current_x = x;
                current_y += 8;
            }
            if (current_y + 8 > EXAMPLE_LCD_V_RES) break;
            
            lcd_draw_char(current_x, current_y, *text, color, bg_color);
            current_x += 8;
        }
        text++;
    }
    
    if (lcd_mutex) xSemaphoreGiveRecursive(lcd_mutex);
}

void lcd_draw_char_scaled(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color, int scale)
{
    if (!panel_handle) return;
    if (c < ' ' || c > '~') return; 
    
    int w = 8 * scale;
    int h = 8 * scale;
    
    static uint16_t *scaled_buffer = NULL;
    static int scaled_buffer_size = 0;
    
    int required_size = w * h * sizeof(uint16_t);
    if (scaled_buffer == NULL || scaled_buffer_size < required_size) {
        if (scaled_buffer) heap_caps_free(scaled_buffer);
        scaled_buffer = heap_caps_malloc(required_size, MALLOC_CAP_DMA);
        assert(scaled_buffer);
        scaled_buffer_size = required_size;
    }

    if (lcd_trans_done_sem) {
        xSemaphoreTake(lcd_trans_done_sem, portMAX_DELAY);
    }

    const uint8_t *glyph = font8x8_basic[(int)(c - ' ')];
    
    for (int col = 0; col < 8; col++) {
        uint8_t col_data = glyph[col];
        for (int row = 0; row < 8; row++) {
             uint16_t pixel = (col_data & (1 << row)) ? color : bg_color;
             uint16_t pixel_be = (pixel >> 8) | (pixel << 8);
             
             for (int dy = 0; dy < scale; dy++) {
                 for (int dx = 0; dx < scale; dx++) {
                     int px = col * scale + dx;
                     int py = row * scale + dy;
                     scaled_buffer[py * w + px] = pixel_be;
                 }
             }
        }
    }
    
    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + w, y + h, scaled_buffer);
}

void lcd_draw_string_scaled(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t bg_color, int scale)
{
    if (lcd_mutex) xSemaphoreTakeRecursive(lcd_mutex, portMAX_DELAY);
    
    uint16_t current_x = x;
    uint16_t current_y = y;
    int char_w = 8 * scale;
    int char_h = 8 * scale;
    
    while (*text) {
        if (*text == '\n') {
            current_x = x;
            current_y += char_h;
        } else {
            if (current_x + char_w > EXAMPLE_LCD_H_RES) {
                current_x = x;
                current_y += char_h;
            }
            if (current_y + char_h > EXAMPLE_LCD_V_RES) break;
            
            lcd_draw_char_scaled(current_x, current_y, *text, color, bg_color, scale);
            current_x += char_w;
        }
        text++;
    }
    
    if (lcd_mutex) xSemaphoreGiveRecursive(lcd_mutex);
}

void lcd_init(void)
{
    // Create semaphore for synchronization
    lcd_trans_done_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(lcd_trans_done_sem); // Give initially so first draw can proceed
    
    lcd_mutex = xSemaphoreCreateRecursiveMutex();
    
    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_SCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
        .miso_io_num = EXAMPLE_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lcd_draw_ready,
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install GC9107 panel driver");
    // Global handle is used now
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR, // Change to BGR
        .bits_per_pixel = 16,
    };
    // Use the specific GC9107 initialization function
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9107(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // Set gap to handle 128x128 offset in 132x132 or 160x128 frame
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 2, 1));
    
    // Rotate 180 degrees
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));
    
    // Invert color if needed
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, is_inverted));
    
    // Turn on the screen
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_LCD_BL
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(EXAMPLE_PIN_NUM_LCD_BL, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
    
    // Clear screen to Black
    lcd_draw_color_bar(0, 0, EXAMPLE_LCD_H_RES - 1, EXAMPLE_LCD_V_RES - 1, 0x0000);
    
    // Draw initial status
    lcd_draw_string(0, 0, "Initializing...", 0xFFFF, 0x0000);
}

void lcd_toggle_inversion(void)
{
    if (panel_handle) {
        is_inverted = !is_inverted;
        esp_lcd_panel_invert_color(panel_handle, is_inverted);
        ESP_LOGI(TAG, "LCD Inversion toggled to %d", is_inverted);
    }
}
