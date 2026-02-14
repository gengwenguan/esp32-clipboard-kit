#include "ui_manager.h"
#include "lcd_display.h"
#include "usb_hid.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "ui_manager";

// UI State
static int s_current_page = 1;
static const int TOTAL_PAGES = 3;

// WiFi State Cache
static char s_ssid[33] = {0};
static char s_password[64] = {0};
static char s_ip[16] = {0};
static char s_gw[16] = {0};
static enum { DISP_IDLE, DISP_AP, DISP_STA, DISP_CONNECTING, DISP_DISCONNECTED } s_wifi_state = DISP_IDLE;

// Time State Cache
static char s_last_date[64] = {0};
static char s_last_time[64] = {0};
static char s_last_weekday[64] = {0};

// Forward Declarations
static void render_current_page(void);
static void render_page1_wifi(void);
static void render_page2_clock(void);
static void render_page3_usb(void);

// Task to update time on Page 2
static void ui_time_task(void *arg)
{
    while (1) {
        if (s_current_page == 2) {
            render_page2_clock();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void ui_init(void)
{
    // Start time update task
    xTaskCreate(ui_time_task, "ui_time", 4096, NULL, 5, NULL);
    
    // Initial render
    render_current_page();
}

void ui_toggle_inversion(void)
{
    lcd_toggle_inversion();
}

// ================= Navigation =================

void ui_set_page(int page)
{
    if (page < 1) page = 1;
    if (page > TOTAL_PAGES) page = TOTAL_PAGES;
    
    if (s_current_page != page) {
        s_current_page = page;
        
        // Reset time cache when entering page 2 to force full redraw
        if (s_current_page == 2) {
            memset(s_last_date, 0, sizeof(s_last_date));
            memset(s_last_time, 0, sizeof(s_last_time));
            memset(s_last_weekday, 0, sizeof(s_last_weekday));
        }
        
        render_current_page();
    }
}

void ui_next_page(void)
{
    if (s_current_page < TOTAL_PAGES) {
        // Leaving Page actions
        if (s_current_page == 3) {
             usb_hid_set_enabled(false);
        }
        
        s_current_page++;
        
        // Entering Page actions
        if (s_current_page == 2) {
            memset(s_last_date, 0, sizeof(s_last_date));
            memset(s_last_time, 0, sizeof(s_last_time));
            memset(s_last_weekday, 0, sizeof(s_last_weekday));
        } else if (s_current_page == 3) {
            // Start with USB disabled when entering page 3
            usb_hid_set_enabled(false);
        }
        
        render_current_page();
    }
}

void ui_prev_page(void)
{
    if (s_current_page > 1) {
        // Leaving Page actions
        if (s_current_page == 3) {
             usb_hid_set_enabled(false);
        }
        
        s_current_page--;
        
        // Entering Page actions
        if (s_current_page == 2) {
            memset(s_last_date, 0, sizeof(s_last_date));
            memset(s_last_time, 0, sizeof(s_last_time));
            memset(s_last_weekday, 0, sizeof(s_last_weekday));
        }
        
        render_current_page();
    }
}

void ui_enter_action(void)
{
    if (s_current_page == 2) {
        // Page 2: Refresh clock
        render_page2_clock();
    } else if (s_current_page == 3) {
        // Page 3: Enable USB / Send String
        if (usb_hid_is_active()) {
            usb_hid_send_string();
        } else {
            usb_hid_set_enabled(true);
            render_page3_usb();
        }
    }
}

// ================= State Updates =================

void ui_update_wifi_ap(const char *ssid, const char *ip)
{
    s_wifi_state = DISP_AP;
    strncpy(s_ssid, ssid, sizeof(s_ssid)-1);
    strncpy(s_ip, ip, sizeof(s_ip)-1);
    if (s_current_page == 1) render_page1_wifi();
}

void ui_update_wifi_sta(const char *ssid, const char *password, const char *ip, const char *gw)
{
    s_wifi_state = DISP_STA;
    strncpy(s_ssid, ssid, sizeof(s_ssid)-1);
    strncpy(s_password, password, sizeof(s_password)-1);
    strncpy(s_ip, ip, sizeof(s_ip)-1);
    strncpy(s_gw, gw, sizeof(s_gw)-1);
    if (s_current_page == 1) render_page1_wifi();
}

void ui_update_wifi_connecting(const char *ssid)
{
    s_wifi_state = DISP_CONNECTING;
    strncpy(s_ssid, ssid, sizeof(s_ssid)-1);
    if (s_current_page == 1) render_page1_wifi();
}

void ui_update_wifi_disconnected(void)
{
    s_wifi_state = DISP_DISCONNECTED;
    if (s_current_page == 1) render_page1_wifi();
}

void ui_refresh_usb_page(void)
{
    if (s_current_page == 3) {
        render_page3_usb();
    }
}

// ================= Rendering Logic =================

static void render_page_header(int page)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "Page(%d/%d)", page, TOTAL_PAGES);
    lcd_draw_string(28, 0, buf, 0xFFFF, 0x0000);
}

static void render_current_page(void)
{
    switch (s_current_page) {
        case 1: render_page1_wifi(); break;
        case 2: render_page2_clock(); break;
        case 3: render_page3_usb(); break;
        default: break;
    }
}

static void render_page1_wifi(void)
{
    lcd_clear();
    render_page_header(1);
    
    uint16_t y = 12;

    if (s_wifi_state == DISP_AP) {
        lcd_draw_string(0, y, "SoftAP Mode", 0x07E0, 0x0000); y+=12;
        lcd_draw_string(0, y, "SSID:", 0xFFFF, 0x0000); y+=9;
        lcd_draw_string(0, y, s_ssid, 0xFFFF, 0x0000); y+=12;
        lcd_draw_string(0, y, "IP:", 0xFFFF, 0x0000); y+=9;
        lcd_draw_string(0, y, s_ip, 0xFFFF, 0x0000); y+=12;
        lcd_draw_string(0, y, "Connect to config", 0xFFFF, 0x0000);
    } else if (s_wifi_state == DISP_STA) {
        lcd_draw_string(0, y, "Station Mode", 0x07E0, 0x0000); y+=12;
        lcd_draw_string(0, y, "SSID:", 0xFFFF, 0x0000); y+=9;
        lcd_draw_string(0, y, s_ssid, 0xFFFF, 0x0000); y+=12;
        lcd_draw_string(0, y, "Pwd:", 0xFFFF, 0x0000); y+=9;
        if (strlen(s_password) > 0) {
            lcd_draw_string(0, y, s_password, 0xFFFF, 0x0000);
        } else {
            lcd_draw_string(0, y, "<Open>", 0xAAAA, 0x0000);
        }
        y+=12;
        lcd_draw_string(0, y, "IP:", 0xFFFF, 0x0000); y+=9;
        lcd_draw_string(0, y, s_ip, 0xFFFF, 0x0000); y+=12;
        lcd_draw_string(0, y, "Gateway:", 0xFFFF, 0x0000); y+=9;
        lcd_draw_string(0, y, s_gw, 0xFFFF, 0x0000);
    } else if (s_wifi_state == DISP_CONNECTING) {
        lcd_draw_string(0, y, "Connecting to:", 0xFFFF, 0x0000); y+=16;
        lcd_draw_string(0, y, s_ssid, 0x07E0, 0x0000); y+=12;
        lcd_draw_string(0, y, "Pwd:", 0xFFFF, 0x0000); y+=9;
        if (strlen(s_password) > 0) {
            lcd_draw_string(0, y, s_password, 0xFFFF, 0x0000);
        } else {
            lcd_draw_string(0, y, "<Open>", 0xAAAA, 0x0000);
        }
    } else if (s_wifi_state == DISP_DISCONNECTED) {
        lcd_draw_string(0, y, "Disconnected", 0xF800, 0x0000); y+=8;
        lcd_draw_string(0, y, "Retrying...", 0xFFFF, 0x0000);
    } else {
        lcd_draw_string(0, y, "Initializing...", 0xFFFF, 0x0000);
    }
}

static void render_page2_clock(void)
{
    // Partial updates handled in loop, but full redraw needed for header
    // Only clear if we are doing a full page refresh (not just time update)
    // For simplicity, we can clear only if we are not just updating time digits
    // But since this is called from ui_time_task AND ui_set_page...
    // Let's check if we need to draw static elements.
    
    // NOTE: In the original code, lcd_render_time did partial updates.
    // lcd_draw_page2 did the clear.
    
    // We can use a flag or check if the background is dirty.
    // Simpler: Just draw header every time? No, that flickers.
    
    // Let's assume render_page2_clock is called periodically.
    // We should only draw the header if it's not there? 
    // Actually, we can split it. But to keep it simple and match original logic:
    
    // If this is called from ui_set_page (or manual refresh), we want to clear.
    // If called from timer, we don't want to clear.
    
    // But we don't know who called us.
    // Let's make render_page2_clock ONLY do the clock update, 
    // and have a separate function for the full page setup.
    // Refactoring...
    
    // Actually, checking s_last_date emptiness is a good proxy for "first draw".
    bool first_draw = (s_last_date[0] == 0);
    
    if (first_draw) {
        lcd_clear();
        render_page_header(2);
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char buf[64];
    
    // Date: YYYY-MM-DD
    strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
    if (strcmp(buf, s_last_date) != 0 || first_draw) {
        lcd_draw_string(24, 30, buf, 0xFFFF, 0x0000);
        strncpy(s_last_date, buf, sizeof(s_last_date) - 1);
    }
    
    // Time: HH:MM:SS (Scaled x2)
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    int scale = 2;
    int char_w = 8 * scale;
    int start_x = 0;
    int start_y = 46;
    
    for (int i = 0; buf[i] != '\0'; i++) {
        if (buf[i] != s_last_time[i] || first_draw) {
            lcd_draw_char_scaled(start_x + (i * char_w), start_y, buf[i], 0x07E0, 0x0000, scale);
        }
    }
    strncpy(s_last_time, buf, sizeof(s_last_time) - 1);
    
    // Weekday
    strftime(buf, sizeof(buf), "%A", &timeinfo);
    if (strcmp(buf, s_last_weekday) != 0 || first_draw) {
        lcd_draw_color_bar(0, 70, 127, 78, 0x0000); // Clear line
        lcd_draw_string(30, 70, buf, 0xAAAA, 0x0000);
        strncpy(s_last_weekday, buf, sizeof(s_last_weekday) - 1);
    }
}

static void render_page3_usb(void)
{
    lcd_clear();
    render_page_header(3);
    
    bool usb_active = usb_hid_is_active();
    
    if (usb_active) {
        // Active Mode: Show the content string
        lcd_draw_string(0, 15, "Input Content:", 0x07E0, 0x0000); // Green title
        
        const char *content = usb_hid_get_string();
        if (content && strlen(content) > 0) {
            lcd_draw_string(0, 28, content, 0xFFFF, 0x0000);
        } else {
            lcd_draw_string(0, 28, "<Empty>", 0xAAAA, 0x0000);
        }
    } else {
        // Inactive Mode: Show Enable prompt
        lcd_draw_string(15, 30, "USB Keyboard", 0x07E0, 0x0000);
        lcd_draw_string(10, 60, "Status: Off", 0xF800, 0x0000); // Red
        lcd_draw_string(0, 90, "KEY1: Enable USB", 0xFFFF, 0x0000);
    }
}
