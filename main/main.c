#include "wifi_prov.h"
#include "button.h"
#include "lcd_display.h"
#include "ui_manager.h"
#include "usb_hid.h"

void app_main(void)
{
    // Initialize LCD first so we can show status
    lcd_init();

    // Initialize UI Manager
    ui_init();

    // Initialize Buttons
    button_init();

    // Initialize WiFi Provisioning (and Networking)
    wifi_prov_init();
}
