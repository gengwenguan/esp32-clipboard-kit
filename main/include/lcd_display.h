#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

void lcd_init(void);
void lcd_clear(void);
void lcd_draw_string(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t bg_color);
void lcd_draw_string_scaled(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t bg_color, int scale);
void lcd_draw_char_scaled(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color, int scale);
void lcd_draw_color_bar(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color);
void lcd_toggle_inversion(void);

#endif // LCD_DISPLAY_H
