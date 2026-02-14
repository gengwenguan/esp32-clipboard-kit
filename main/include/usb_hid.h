#ifndef USB_HID_H
#define USB_HID_H

#include "esp_err.h"
#include <stdbool.h>

#define USB_STRING_MAX_LEN 1024

/**
 * @brief Initialize the USB HID service (create task)
 */
void usb_hid_init(void);

/**
 * @brief Enable or disable USB HID (install/uninstall driver)
 */
void usb_hid_set_enabled(bool enabled);

// Check if USB HID is currently active
bool usb_hid_is_active(void);

// Set the string to be typed
void usb_hid_set_string(const char *string);

// Get the stored string
const char *usb_hid_get_string(void);

/**
 * @brief Save USB string to NVS and update internal state
 */
esp_err_t usb_hid_save_string(const char *string);

/**
 * @brief Load USB string from NVS to internal state
 */
esp_err_t usb_hid_load_string(void);

// Send the stored string via USB Keyboard
void usb_hid_send_string(void);

#endif // USB_HID_H
