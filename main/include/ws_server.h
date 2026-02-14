#ifndef WS_SERVER_H
#define WS_SERVER_H

#include "esp_http_server.h"

#define WEBSOCKET_CLIENT_MAX 5

/**
 * @brief Initialize the WebSocket server manager
 */
void ws_server_init(void);

/**
 * @brief Add a new WebSocket client
 * @param handle HTTP server handle
 * @param fd Socket file descriptor
 * @return Client index or -1 if full
 */
int ws_server_add_client(httpd_handle_t handle, int fd);

/**
 * @brief Remove a WebSocket client
 * @param fd Socket file descriptor
 */
void ws_server_remove_client(int fd);

/**
 * @brief Broadcast a message to all connected clients
 * @param message Null-terminated string message
 */
void ws_server_broadcast(const char *message);

#endif // WS_SERVER_H
