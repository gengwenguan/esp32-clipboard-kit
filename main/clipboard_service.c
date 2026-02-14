#include <string.h>
#include <stdlib.h>
#include "clipboard_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

static const char *TAG = "clipboard";

static char shared_clipboard[SHARED_CLIPBOARD_MAX_LEN + 1] = {0};
static SemaphoreHandle_t clipboard_mutex = NULL;

esp_err_t clipboard_service_init(void)
{
    if (clipboard_mutex == NULL) {
        clipboard_mutex = xSemaphoreCreateMutex();
        if (clipboard_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t clipboard_service_set(const char *content)
{
    if (clipboard_mutex == NULL) return ESP_FAIL;
    
    if (strlen(content) > SHARED_CLIPBOARD_MAX_LEN) {
        ESP_LOGE(TAG, "Content too long");
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(clipboard_mutex, portMAX_DELAY);
    strncpy(shared_clipboard, content, SHARED_CLIPBOARD_MAX_LEN);
    shared_clipboard[SHARED_CLIPBOARD_MAX_LEN] = '\0';
    xSemaphoreGive(clipboard_mutex);
    
    return ESP_OK;
}

esp_err_t clipboard_service_get(char *buffer, size_t buffer_len)
{
    if (clipboard_mutex == NULL) return ESP_FAIL;
    
    xSemaphoreTake(clipboard_mutex, portMAX_DELAY);
    strncpy(buffer, shared_clipboard, buffer_len - 1);
    buffer[buffer_len - 1] = '\0';
    xSemaphoreGive(clipboard_mutex);
    
    return ESP_OK;
}

esp_err_t clipboard_service_get_base64(char *buffer, size_t buffer_len)
{
    if (clipboard_mutex == NULL) return ESP_FAIL;
    
    xSemaphoreTake(clipboard_mutex, portMAX_DELAY);
    size_t olen = 0;
    int ret = mbedtls_base64_encode((unsigned char *)buffer, buffer_len, &olen, 
                                    (const unsigned char *)shared_clipboard, strlen(shared_clipboard));
    xSemaphoreGive(clipboard_mutex);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encode failed: %d", ret);
        return ESP_FAIL;
    }
    
    buffer[olen] = '\0'; // Null-terminate the string
    
    return ESP_OK;
}

esp_err_t clipboard_service_set_base64(const char *base64_content)
{
    if (clipboard_mutex == NULL) return ESP_FAIL;
    
    size_t olen = 0;
    // Decode to a temporary buffer first to ensure atomicity and size check
    char *temp_buf = malloc(SHARED_CLIPBOARD_MAX_LEN + 1);
    if (temp_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    int ret = mbedtls_base64_decode((unsigned char *)temp_buf, SHARED_CLIPBOARD_MAX_LEN, &olen, 
                                    (const unsigned char *)base64_content, strlen(base64_content));
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d", ret);
        free(temp_buf);
        return ESP_FAIL;
    }
    
    temp_buf[olen] = '\0';
    
    xSemaphoreTake(clipboard_mutex, portMAX_DELAY);
    strncpy(shared_clipboard, temp_buf, SHARED_CLIPBOARD_MAX_LEN);
    shared_clipboard[SHARED_CLIPBOARD_MAX_LEN] = '\0';
    xSemaphoreGive(clipboard_mutex);
    
    free(temp_buf);
    return ESP_OK;
}
