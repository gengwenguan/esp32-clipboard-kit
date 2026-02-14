#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "wifi_prov.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "dns_server.h"
#include "ui_manager.h"
#include "usb_hid.h"
#include "clipboard_service.h"
#include "ws_server.h"
#include "web_server.h"

static const char *TAG = "wifi_prov";

/* Event handler for WiFi events */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        
        // Initialize SNTP
        ESP_LOGI(TAG, "Initializing SNTP");
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        
        // Set timezone to China Standard Time
        setenv("TZ", "CST-8", 1);
        tzset();
        
        // Display Station info on screen
        wifi_config_t conf;
        esp_wifi_get_config(WIFI_IF_STA, &conf); // Get current config

        char ip_str[16];
        char gw_str[16];
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));
        sprintf(gw_str, IPSTR, IP2STR(&event->ip_info.gw));
        
        ui_update_wifi_sta((char*)conf.sta.ssid, (char*)conf.sta.password, ip_str, gw_str);

        // Disable SoftAP to optimize performance
        ESP_LOGI(TAG, "Connected to Router! Stopping SoftAP...");
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Station disconnected");
        
        ui_update_wifi_disconnected();

        // If connection is lost, ensure SoftAP is active for reconfiguration
        wifi_mode_t mode;
        esp_wifi_get_mode(&mode);
        if (mode != WIFI_MODE_APSTA) {
             ESP_LOGI(TAG, "Connection lost! Restarting SoftAP for reconfiguration...");
             esp_wifi_set_mode(WIFI_MODE_APSTA);
        }
        
        // Always redraw SoftAP info when STA disconnects, as we fallback to AP availability
        ui_update_wifi_ap(EXAMPLE_ESP_WIFI_SSID, "192.168.4.1");
    }
}

void wifi_prov_init(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create AP and STA netifs
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Load USB String
    usb_hid_load_string();

    // Initialize Services
    ESP_ERROR_CHECK(clipboard_service_init());
    ws_server_init();
    usb_hid_init();

    // Configure WiFi Mode
    // Check if we have saved STA config
    wifi_config_t wifi_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
        if (strlen((char *)wifi_config.sta.ssid) > 0) {
            ESP_LOGI(TAG, "Found saved credentials for SSID: %s", wifi_config.sta.ssid);
            
            // Display Connecting info
            ui_update_wifi_connecting((char *)wifi_config.sta.ssid);

            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_start());
            ESP_ERROR_CHECK(esp_wifi_connect());
            
            // Start DNS Server (even in STA mode, to be ready for AP fallback)
            start_dns_server();

            start_webserver();
            return;
        }
    }
    
    // If no config found, start APSTA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    
    // Configure SoftAP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = 1,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, 1);
             
    // Display AP info on screen
    ui_update_wifi_ap(EXAMPLE_ESP_WIFI_SSID, "192.168.4.1");

    // Start DNS Server (for Captive Portal)
    start_dns_server();

    // Start Web Server
    start_webserver();
}
