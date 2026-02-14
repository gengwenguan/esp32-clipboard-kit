#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "web_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "pages.h"
#include "usb_hid.h"
#include "ui_manager.h"
#include "clipboard_service.h"
#include "ws_server.h"

static const char *TAG = "web_server";

static void url_decode(char *dst, const char *src, size_t max_len)
{
    char a, b;
    size_t len = 0;
    while (*src && len < max_len) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit((int)a) && isxdigit((int)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
        len++;
    }
    *dst++ = '\0';
}

static void broadcast_clipboard_update(void)
{
    size_t base64_len = SHARED_CLIPBOARD_MAX_LEN * 2;
    size_t resp_len = base64_len + 64;
    
    char *base64_content = malloc(base64_len);
    if (base64_content == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for base64 content in broadcast");
        return;
    }
    
    if (clipboard_service_get_base64(base64_content, base64_len) != ESP_OK) {
        free(base64_content);
        return;
    }
    
    char *response = malloc(resp_len);
    if (response == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for response in broadcast");
        free(base64_content);
        return;
    }
    
    snprintf(response, resp_len, "{\"type\":\"update\",\"content\":\"%s\"}", base64_content);
    ws_server_broadcast(response);
    
    free(base64_content);
    free(response);
}

static void ws_close_callback(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "WebSocket session closed, fd=%d", sockfd);
    ws_server_remove_client(sockfd);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_server_add_client(req->handle, fd);
        
        ESP_LOGI(TAG, "WebSocket client connected, fd=%d", fd);
        return ESP_OK;
    }
    
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %s", esp_err_to_name(ret));
        int fd = httpd_req_to_sockfd(req);
        ws_server_remove_client(fd);
        return ret;
    }
    
    if (ws_pkt.len) {
        // Limit max message size to prevent DoS
        if (ws_pkt.len > SHARED_CLIPBOARD_MAX_LEN * 3) {
             ESP_LOGE(TAG, "WebSocket message too large: %d", (int)ws_pkt.len);
             return ESP_ERR_INVALID_SIZE;
        }

        uint8_t *buf = malloc(ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for WebSocket message");
            return ESP_ERR_NO_MEM;
        }
        
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %s", esp_err_to_name(ret));
            free(buf);
            int fd = httpd_req_to_sockfd(req);
            ws_server_remove_client(fd);
            return ret;
        }
        buf[ws_pkt.len] = '\0';
        
        ESP_LOGI(TAG, "Received WebSocket message: %s", (char*)buf);
        
        if (strncmp((char*)buf, "{\"type\":\"update\",\"content\":\"", 28) == 0) {
            char *content_start = (char*)buf + 28;
            char *content_end = strstr(content_start, "\"}");
            if (content_end && content_end > content_start) {
                // Terminate the base64 string
                *content_end = '\0';
                
                if (clipboard_service_set_base64(content_start) == ESP_OK) {
                    ESP_LOGI(TAG, "Updated shared clipboard via WebSocket");
                    broadcast_clipboard_update();
                }
            }
        } else if (strncmp((char*)buf, "{\"type\":\"get_state\"}", 20) == 0) {
            size_t base64_len = SHARED_CLIPBOARD_MAX_LEN * 2;
            char *base64_content = malloc(base64_len);
            if (base64_content) {
                if (clipboard_service_get_base64(base64_content, base64_len) == ESP_OK) {
                    char *response = malloc(base64_len + 64);
                    if (response) {
                        snprintf(response, base64_len + 64, "{\"type\":\"update\",\"content\":\"%s\"}", base64_content);
                        
                        httpd_ws_frame_t response_pkt = {
                            .type = HTTPD_WS_TYPE_TEXT,
                            .payload = (uint8_t *)response,
                            .len = strlen(response),
                            .final = true
                        };
                        esp_err_t send_ret = httpd_ws_send_frame(req, &response_pkt);
                        if (send_ret != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to send initial state: %s", esp_err_to_name(send_ret));
                        }
                        free(response);
                    }
                }
                free(base64_content);
            }
        }
        
        free(buf);
    }
    
    return ESP_OK;
}

/* HTTP GET Handler for root "/" */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Simple 16x16 favicon.ico data */
static const uint8_t favicon_ico[] = {
    0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x68, 0x00,
    0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xe0, 0xe0, 0xe0, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff,
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico, sizeof(favicon_ico));
    return ESP_OK;
}

/* HTTP POST Handler for "/save_usb" */
static esp_err_t save_usb_post_handler(httpd_req_t *req)
{
    // Use heap allocation instead of stack for large buffer
    char *buf = malloc(4096); // Increased buffer size to accommodate larger USB string
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for USB string");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int ret, remaining = req->content_len;
    int cur_len = 0;

    if (remaining >= 4096) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    while (cur_len < remaining) {
        ret = httpd_req_recv(req, buf + cur_len, remaining - cur_len);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            free(buf);
            return ESP_FAIL;
        }
        cur_len += ret;
    }
    buf[cur_len] = '\0';

    ESP_LOGI(TAG, "Received USB String: %s", buf);

    // Save USB String (and update HID module)
    usb_hid_save_string(buf);
    
    // Refresh LCD if currently showing Page 3
    ui_refresh_usb_page();
    
    free(buf); // Free the buffer
    
    httpd_resp_send(req, usb_saved_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* HTTP GET Handler for "/clipboard" - Get shared clipboard content */
static esp_err_t clipboard_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Handling /clipboard GET request");
    
    // Allocate memory for the response buffer
    // Estimate: Template (~2KB) + Content (~1.5KB) + Margin
    size_t buf_len = 8192;
    char *resp_buf = malloc(buf_len);
    if (resp_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    size_t base64_len = SHARED_CLIPBOARD_MAX_LEN * 2;
    char *base64_content = malloc(base64_len);
    if (base64_content == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for base64 content");
        free(resp_buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    if (clipboard_service_get_base64(base64_content, base64_len) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get base64 content");
        free(base64_content);
        free(resp_buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Base64 content length: %lu", (unsigned long)strlen(base64_content));
    
    int printed_len = snprintf(resp_buf, buf_len, clipboard_html_template, base64_content);
    
    if (printed_len < 0 || printed_len >= buf_len) {
        ESP_LOGE(TAG, "Response buffer too small or snprintf error");
        free(base64_content);
        free(resp_buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    esp_err_t res = httpd_resp_send(req, resp_buf, printed_len);
    
    free(base64_content);
    free(resp_buf);

    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send response: %s", esp_err_to_name(res));
        return res;
    }
    
    ESP_LOGI(TAG, "Finished handling /clipboard GET request (len=%d)", printed_len);
    return ESP_OK;
}

/* HTTP POST Handler for "/connect" */
static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char buf[256]; // Increased buffer size to accommodate encoded SSID/Password
    int ret, remaining = req->content_len;
    int cur_len = 0;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    while (cur_len < remaining) {
        ret = httpd_req_recv(req, buf + cur_len, remaining - cur_len);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        cur_len += ret;
    }
    buf[cur_len] = '\0';

    char ssid[32] = {0};
    char password[64] = {0};
    
    char *ssid_ptr = strstr(buf, "ssid=");
    char *pass_ptr = strstr(buf, "password=");
    
    if (ssid_ptr) {
        ssid_ptr += 5; // Skip "ssid="
        char *end = strchr(ssid_ptr, '&');
        if (end) *end = '\0'; // Terminate for decoding
        
        url_decode(ssid, ssid_ptr, sizeof(ssid));
        
        if (end) *end = '&'; // Restore if needed (though we don't strictly need to here)
    }
    
    if (pass_ptr) {
        pass_ptr += 9; // Skip "password="
        char *end = strchr(pass_ptr, '&');
        if (end) *end = '\0';
        
        url_decode(password, pass_ptr, sizeof(password));
    }
    
    ESP_LOGI(TAG, "Received SSID: %s", ssid);
    ESP_LOGI(TAG, "Received Password: %s", password);

    // Configure STA
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Disconnect from current AP if connected
    esp_wifi_disconnect();

    ESP_ERROR_CHECK(esp_wifi_connect());

    httpd_resp_send(req, success_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* HTTP Error (404) Handler - Redirects to root for Captive Portal */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t connect_uri = {
    .uri       = "/connect",
    .method    = HTTP_POST,
    .handler   = connect_post_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t favicon_uri = {
    .uri       = "/favicon.ico",
    .method    = HTTP_GET,
    .handler   = favicon_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t save_usb_uri = {
    .uri       = "/save_usb",
    .method    = HTTP_POST,
    .handler   = save_usb_post_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t clipboard_uri = {
    .uri       = "/clipboard",
    .method    = HTTP_GET,
    .handler   = clipboard_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t ws_uri = {
    .uri       = "/ws",
    .method    = HTTP_GET,
    .handler   = ws_handler,
    .user_ctx  = NULL,
    .is_websocket = true
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.stack_size = 8192; // Increase stack size to handle large buffers in save_usb_post_handler
    config.max_uri_handlers = 12; // Ensure enough slots for all URI handlers
    config.close_fn = ws_close_callback;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &connect_uri);
        httpd_register_uri_handler(server, &favicon_uri);
        httpd_register_uri_handler(server, &save_usb_uri);
        httpd_register_uri_handler(server, &clipboard_uri);
        esp_err_t ws_ret = httpd_register_uri_handler(server, &ws_uri);
        if (ws_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register WebSocket handler: %s", esp_err_to_name(ws_ret));
        }
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}
