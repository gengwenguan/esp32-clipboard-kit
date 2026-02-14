#include <string.h>
#include <stdlib.h>
#include "ws_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "ws_server";

typedef struct {
    httpd_handle_t handle;
    int fd;
    bool connected;
} ws_client_t;

static ws_client_t ws_clients[WEBSOCKET_CLIENT_MAX];
static SemaphoreHandle_t ws_mutex = NULL;
static bool ws_initialized = false;

void ws_server_init(void)
{
    if (ws_mutex == NULL) {
        ws_mutex = xSemaphoreCreateMutex();
    }
    if (ws_mutex != NULL) {
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        for (int i = 0; i < WEBSOCKET_CLIENT_MAX; i++) {
            ws_clients[i].handle = NULL;
            ws_clients[i].fd = -1;
            ws_clients[i].connected = false;
        }
        ws_initialized = true;
        xSemaphoreGive(ws_mutex);
    }
}

int ws_server_add_client(httpd_handle_t handle, int fd)
{
    int index = -1;
    if (ws_mutex == NULL || !ws_initialized) {
        ws_server_init();
    }
    
    if (ws_mutex == NULL) return -1;
    
    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    
    // First check if client already exists
    for (int i = 0; i < WEBSOCKET_CLIENT_MAX; i++) {
        if (ws_clients[i].connected && ws_clients[i].fd == fd) {
            ESP_LOGI(TAG, "WebSocket client already exists at index %d, fd=%d", i, fd);
            ws_clients[i].handle = handle; // Update handle just in case
            xSemaphoreGive(ws_mutex);
            return i;
        }
    }

    // Add new client
    for (int i = 0; i < WEBSOCKET_CLIENT_MAX; i++) {
        if (!ws_clients[i].connected) {
            ws_clients[i].handle = handle;
            ws_clients[i].fd = fd;
            ws_clients[i].connected = true;
            index = i;
            ESP_LOGI(TAG, "WebSocket client connected at index %d, fd=%d", i, fd);
            break;
        }
    }
    xSemaphoreGive(ws_mutex);
    
    if (index == -1) {
        ESP_LOGW(TAG, "No available WebSocket client slot");
    }
    
    return index;
}

void ws_server_remove_client(int fd)
{
    if (ws_mutex == NULL) return;
    
    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    for (int i = 0; i < WEBSOCKET_CLIENT_MAX; i++) {
        if (ws_clients[i].connected && ws_clients[i].fd == fd) {
            ws_clients[i].connected = false;
            ws_clients[i].fd = -1;
            ws_clients[i].handle = NULL;
            ESP_LOGI(TAG, "WebSocket client disconnected at index %d, fd=%d", i, fd);
            break;
        }
    }
    xSemaphoreGive(ws_mutex);
}

void ws_server_broadcast(const char *message)
{
    if (ws_mutex == NULL || !ws_initialized) return;
    
    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)message,
        .len = strlen(message),
        .final = true
    };
    
    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    for (int i = 0; i < WEBSOCKET_CLIENT_MAX; i++) {
        if (ws_clients[i].connected && ws_clients[i].handle != NULL) {
            // Check if the file descriptor is still valid
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(ws_clients[i].fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                 ESP_LOGW(TAG, "Client %d (fd=%d) socket invalid, removing", i, ws_clients[i].fd);
                 ws_clients[i].connected = false;
                 ws_clients[i].handle = NULL;
                 ws_clients[i].fd = -1;
                 continue;
            }

            esp_err_t ret = httpd_ws_send_frame_async(ws_clients[i].handle, ws_clients[i].fd, &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send to client %d (fd=%d): %s", i, ws_clients[i].fd, esp_err_to_name(ret));
                ws_clients[i].connected = false;
                ws_clients[i].handle = NULL;
                ws_clients[i].fd = -1;
            }
        }
    }
    xSemaphoreGive(ws_mutex);
}
