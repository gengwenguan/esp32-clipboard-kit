#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include "esp_err.h"

// Define constants
#define EXAMPLE_ESP_WIFI_SSID      "ESP32-S3-Prov"
#define EXAMPLE_ESP_WIFI_PASS      ""
#define EXAMPLE_MAX_STA_CONN       4

void wifi_prov_init(void);

#endif // WIFI_PROV_H
