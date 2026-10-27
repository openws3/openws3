/* ST7735S 80x160 panel, used landscape 160x80, RGB565, bit-banged SPI.  No framebuffer (RAM is 8 KB). */
#ifndef LCD_H
#define LCD_H
#include "compat.h"

#define LCD_PX_W 160
#define LCD_PX_H 80

void lcd_init(void);
void lcd_wake(void);              /* sleep-out; the panel wants 5 ms before lcd_reconfig */
void lcd_reconfig(void);          /* re-send the panel registers (not the picture); see lcd.c */
void lcd_fill(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);
/* sx, sy: integer stretch of the 6x8 cell.  Characters outside the font draw as space. */
void lcd_text(uint8_t x, uint8_t y, const char *s, uint8_t sx, uint8_t sy, uint16_t fg, uint16_t bg);

#endif
