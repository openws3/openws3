/*
 * ST7735S driver.  Wiring: SDA P3.5, SCL P3.7, CS P4.1, DC P5.1, RST P5.0.
 * Init values for the panel.
 * CASET = screen x (0..159), RASET = screen y + 24, MADCTL 0xA8.
 */
#include "lcd.h"
#include "hal.h"
#include "font.h"

__sbit __at (0xB5) SDA;
__sbit __at (0xB7) SCL;
__sbit __at (0xC1) CS;
__sbit __at (0xC9) DC;
__sbit __at (0xC8) RST;

/* Shift one byte out, MSB first.  CS is held by the caller (low for a whole command + pixel run).
 * Hand-written: the C version cost ~330 clocks per byte in the large model (a full-screen fill took 0.8 s).
 * The nops set the bit rate (~26 clocks per bit, about 210 per byte, ~1.5 Mbit/s at 40 MHz): SDA and SCL are
 * open-drain pins with pull-ups, so their rising edges are slow, and a bus 2.7x faster was marginal when the
 * supply sagged (random glyphs, wrong colours, rotated picture). */
static void tx(uint8_t b) __naked
{
    (void)b;
    __asm
        mov  a, dpl
        rlc  a
        clr  _SCL
        mov  _SDA, c
        .rept 8
        nop
        .endm
        setb _SCL
        .rept 8
        nop
        .endm
        rlc  a
        clr  _SCL
        mov  _SDA, c
        .rept 8
        nop
        .endm
        setb _SCL
        .rept 8
        nop
        .endm
        rlc  a
        clr  _SCL
        mov  _SDA, c
        .rept 8
        nop
        .endm
        setb _SCL
        .rept 8
        nop
        .endm
        rlc  a
        clr  _SCL
        mov  _SDA, c
        .rept 8
        nop
        .endm
        setb _SCL
        .rept 8
        nop
        .endm
        rlc  a
        clr  _SCL
        mov  _SDA, c
        .rept 8
        nop
        .endm
        setb _SCL
        .rept 8
        nop
        .endm
        rlc  a
        clr  _SCL
        mov  _SDA, c
        .rept 8
        nop
        .endm
        setb _SCL
        .rept 8
        nop
        .endm
        rlc  a
        clr  _SCL
        mov  _SDA, c
        .rept 8
        nop
        .endm
        setb _SCL
        .rept 8
        nop
        .endm
        rlc  a
        clr  _SCL
        mov  _SDA, c
        .rept 8
        nop
        .endm
        setb _SCL
        .rept 8
        nop
        .endm
        ret
    __endasm;
}

/* command: DC low for the opcode, then high for any parameters/pixels that follow */
static void cmd(uint8_t c) { DC = 0; tx(c); DC = 1; }

static void cmd_args(uint8_t c, const __code uint8_t *a, uint8_t n)
{
    CS = 0;
    cmd(c);
    while (n--) tx(*a++);
    CS = 1;
}

/* open a pixel window and leave CS low; the caller streams pixels, then raises CS */
static void window(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    uint8_t x1 = x + w - 1, y0 = y + 24, y1 = y + h - 1 + 24;
    CS = 0;
    cmd(0x2A); tx(0); tx(x); tx(0); tx(x1);
    cmd(0x2B); tx(0); tx(y0); tx(0); tx(y1);
    cmd(0x2C);
}

static const __code uint8_t i_b1[] = { 0x05, 0x3C, 0x3C };
static const __code uint8_t i_b3[] = { 0x05, 0x3C, 0x3C, 0x05, 0x3C, 0x3C };
static const __code uint8_t i_c0[] = { 0x0E, 0x0E, 0x04 };
static const __code uint8_t i_c2[] = { 0x0D, 0x00 };
static const __code uint8_t i_c3[] = { 0x8D, 0x2A };
static const __code uint8_t i_c4[] = { 0x8D, 0xEE };
static const __code uint8_t i_e0[] = { 0x05, 0x1A, 0x0B, 0x15, 0x3D, 0x38, 0x2E, 0x30, 0x2D, 0x28, 0x30, 0x3B, 0x00, 0x01, 0x02, 0x10 };
static const __code uint8_t i_e1[] = { 0x05, 0x1A, 0x0B, 0x15, 0x36, 0x2E, 0x28, 0x2B, 0x2B, 0x28, 0x30, 0x3B, 0x00, 0x01, 0x02, 0x10 };
static const __code uint8_t i_b4 = 0x03, i_c1 = 0xC0, i_c5 = 0x04, i_36 = 0xA8, i_3a = 0x05;

/* every register setting the panel needs, except reset / sleep-out / display-on / the picture itself */
static void config(void)
{
    cmd_args(0xB1, i_b1, 3); cmd_args(0xB2, i_b1, 3); cmd_args(0xB3, i_b3, 6);
    cmd_args(0xB4, &i_b4, 1);
    cmd_args(0xC0, i_c0, 3); cmd_args(0xC1, &i_c1, 1); cmd_args(0xC2, i_c2, 2);
    cmd_args(0xC3, i_c3, 2); cmd_args(0xC4, i_c4, 2); cmd_args(0xC5, &i_c5, 1);
    cmd_args(0x36, &i_36, 1);                        /* landscape */
    cmd_args(0x3A, &i_3a, 1);                        /* RGB565 */
    cmd_args(0xE0, i_e0, 16); cmd_args(0xE1, i_e1, 16);
}

void lcd_init(void)
{
    CS = 1; SCL = 1; DC = 1;
    RST = 0; hal_delay_ms(20);
    RST = 1; hal_delay_ms(120);
    CS = 0; cmd(0x11); CS = 1; hal_delay_ms(120);     /* sleep out */
    config();
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, 0);                 /* clear before the panel turns on */
    CS = 0; cmd(0x29); CS = 1;                       /* display on */
}

/* Re-send the settings.  A supply glitch can scramble the panel's registers (rotation, colour order, ...) or drop it back into
 * sleep (black, backlight still on), and the picture is only drawn where it changes, so the app calls this about once a second.
 * Sleep-out does nothing on a panel that is already awake.  Leaves the picture alone. */
void lcd_reconfig(void)
{
    CS = 1; DC = 1;
    CS = 0; cmd(0x11); CS = 1; hal_delay_ms(5);       /* sleep out; the panel wants 5 ms before the next command */
    config();
    CS = 0; cmd(0x29); CS = 1;
}

void lcd_fill(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
    uint16_t n = (uint16_t)w * h;
    uint8_t hi = color >> 8, lo = color & 0xFF;
    window(x, y, w, h);
    while (n--) { tx(hi); tx(lo); }
    CS = 1;
}

void lcd_text(uint8_t x, uint8_t y, const char *s, uint8_t sx, uint8_t sy, uint16_t fg, uint16_t bg)
{
    uint8_t col[5], c, r, i, j, k, rows = FONT_H_PX * sy, cols = FONT_W * sx;
    uint8_t fh = fg >> 8, fl = fg & 0xFF, bh = bg >> 8, bl = bg & 0xFF;

    for (; *s; s++, x += cols) {
        c = (uint8_t)*s;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
        for (i = 0; i < 5; i++) col[i] = font5x7[(uint8_t)(c - FONT_FIRST) * 5 + i];
        window(x, y, cols, rows);
        for (r = 0; r < rows; r++) {
            k = r / sy;
            for (i = 0; i < 5; i++) {
                uint8_t on = k < 7 && ((col[i] >> k) & 1);
                for (j = 0; j < sx; j++) { if (on) { tx(fh); tx(fl); } else { tx(bh); tx(bl); } }
            }
            for (j = 0; j < sx; j++) { tx(bh); tx(bl); }         /* spacing column */
        }
        CS = 1;
    }
}
