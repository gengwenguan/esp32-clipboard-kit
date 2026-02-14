#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <stdbool.h>

// UI Initialization
void ui_init(void);

// Page Navigation
void ui_next_page(void);
void ui_prev_page(void);
void ui_set_page(int page);

// User Actions
void ui_enter_action(void); // For KEY1 (Enter/Action)

// State Updates
void ui_update_wifi_ap(const char *ssid, const char *ip);
void ui_update_wifi_sta(const char *ssid, const char *password, const char *ip, const char *gw);
void ui_update_wifi_connecting(const char *ssid);
void ui_update_wifi_disconnected(void);

void ui_refresh_usb_page(void);

// Global Display Settings
void ui_toggle_inversion(void);

#endif // UI_MANAGER_H
