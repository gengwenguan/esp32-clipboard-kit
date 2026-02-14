#include "button.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ui_manager.h"

static const char *TAG = "button";
static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void button_task(void* arg)
{
    uint32_t io_num;
    
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            // Simple debouncing logic
            vTaskDelay(pdMS_TO_TICKS(80)); // Delay 80ms for debouncing
            
            // Check for simultaneous press of KEY2 and KEY3
            if (gpio_get_level(GPIO_KEY2) == 0 && gpio_get_level(GPIO_KEY3) == 0) {
                ESP_LOGI(TAG, "KEY2 + KEY3 Combo Pressed - Toggling LCD Inversion");
                ui_toggle_inversion();
                
                // Wait for BOTH buttons to be released
                while(gpio_get_level(GPIO_KEY2) == 0 || gpio_get_level(GPIO_KEY3) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                xQueueReset(gpio_evt_queue);
            }
            // Single press handling
            else if (gpio_get_level(io_num) == 0) {
                ESP_LOGI(TAG, "GPIO[%"PRIu32"] intr, val: %d", io_num, gpio_get_level(io_num));
                
                if (io_num == GPIO_KEY1) {
                    ESP_LOGI(TAG, "KEY1 Pressed");
                    ui_enter_action();
                } else if (io_num == GPIO_KEY2) {
                    ESP_LOGI(TAG, "KEY2 (Left) Pressed");
                    ui_prev_page();
                } else if (io_num == GPIO_KEY3) {
                    ESP_LOGI(TAG, "KEY3 (Right) Pressed");
                    ui_next_page();
                }
                
                // Wait for button release
                while(gpio_get_level(io_num) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
    }
}

void button_init(void)
{
    // Zero-initialize the config structure.
    gpio_config_t io_conf = {};
    
    // Interrupt of falling edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    // Bit mask of the pins
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    // Set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    // Enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    // Create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    
    // Start gpio task - Stack size 4096 as determined before
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);

    // Install gpio isr service
    // Note: gpio_install_isr_service should be called only once.
    // If other modules use it, we might need to check if it's already installed or handle the error.
    // For now, assuming this is the only place initializing it or it handles re-init gracefully (it returns error if already installed)
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
    }
    
    // Hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_KEY1, gpio_isr_handler, (void*) GPIO_KEY1);
    gpio_isr_handler_add(GPIO_KEY2, gpio_isr_handler, (void*) GPIO_KEY2);
    gpio_isr_handler_add(GPIO_KEY3, gpio_isr_handler, (void*) GPIO_KEY3);
    
    ESP_LOGI(TAG, "Button initialized");
}
