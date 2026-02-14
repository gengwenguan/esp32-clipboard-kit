#ifndef CLIPBOARD_SERVICE_H
#define CLIPBOARD_SERVICE_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#define SHARED_CLIPBOARD_MAX_LEN 1024

/**
 * @brief Initialize the clipboard service
 * @return ESP_OK on success
 */
esp_err_t clipboard_service_init(void);

/**
 * @brief Set clipboard content
 * @param content Null-terminated string content
 * @return ESP_OK on success
 */
esp_err_t clipboard_service_set(const char *content);

/**
 * @brief Get clipboard content
 * @param buffer Output buffer
 * @param buffer_len Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t clipboard_service_get(char *buffer, size_t buffer_len);

/**
 * @brief Get clipboard content as Base64 encoded string
 * @param buffer Output buffer
 * @param buffer_len Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t clipboard_service_get_base64(char *buffer, size_t buffer_len);

/**
 * @brief Set clipboard content from Base64 encoded string
 * @param base64_content Base64 encoded string
 * @return ESP_OK on success
 */
esp_err_t clipboard_service_set_base64(const char *base64_content);

#endif // CLIPBOARD_SERVICE_H
