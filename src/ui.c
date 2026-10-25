#include "ui.h"
#include "layout.h"
#include "theme.h"
#include "lcd.h"
#include "cal.h"
#include "fmt.h"
#include "chg.h"
#include "dbgscr.h"

#define NELEM (3 + NPORTS * 3)           /* 3 header boxes, then per port: voltage, status text, dot */

static char    XD shown[NELEM][9];
static uint8_t XD shown_dot[NPORTS];
static uint8_t XD next_el;

static uint8_t box_x(uint8_t i)  { return UI_BOX_X0 + i * UI_BOX_PITCH; }
static uint8_t cell_y(uint8_t p) { return UI_CELL_Y0 + (p / 3) * UI_CELL_PITCH_Y; }

static uint8_t slen(const char *s) { uint8_t n = 0; while (s[n]) n++; return n; }

/* centre `text` on the 160 px wide screen at scale sx,sy */
static void centered(uint8_t y, const char *text, uint8_t sx, uint8_t sy, uint16_t fg, uint16_t bg)
{
    uint8_t w = (uint8_t)(slen(text) * 6 * sx);
    lcd_text(w >= LCD_PX_W ? 0 : (uint8_t)((LCD_PX_W - w) / 2), y, text, sx, sy, fg, bg);
}

void ui_splash(void)
{
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    lcd_fill(0, 14, LCD_PX_W, 3, th(R_BAD));
    lcd_fill(0, 63, LCD_PX_W, 3, th(R_BAD));
    centered(24, "OPENWS3", 2, 2, th(R_TEXT), th(R_BG));
    centered(44, "OPEN FW FOR WHOOPSTOR V3", 1, 1, th(R_INFO), th(R_BG));
}

void ui_message(const char *title, const char *line2, uint8_t has_n1, uint16_t n1, uint8_t has_n2, uint16_t n2)
{
    static char XD b[8];
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    centered(6, title, 1, 2, th(R_BAD), th(R_BG));
    centered(28, line2, 1, 1, th(R_TEXT), th(R_BG));
    if (has_n1) { fmt_u(b, n1); centered(42, b, 2, 2, th(R_WARN), th(R_BG)); }
    if (has_n2) { fmt_u(b, n2); centered(60, b, 1, 1, th(R_DIM), th(R_BG)); }
}

void ui_choose(uint8_t dc)
{
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    centered(2, "POWER INPUT", 1, 2, th(R_TEXT), th(R_BG));
    centered(22, "PLEASE CHOOSE:", 1, 1, th(R_DIM), th(R_BG));
    lcd_fill(14, 34, 132, 14, dc ? th(R_RUN) : th(R_BG));
    lcd_text(18, 37, "1:DC INPUT", 1, 1, th(R_TEXT), dc ? th(R_RUN) : th(R_BG));
    lcd_fill(14, 50, 132, 14, dc ? th(R_BG) : th(R_RUN));
    lcd_text(18, 53, "2:LIPO INPUT", 1, 1, th(R_TEXT), dc ? th(R_BG) : th(R_RUN));
    centered(70, "B1/B2 SWITCH  BOTH OK", 1, 1, th(R_DIM), th(R_BG));
}

static void put(uint8_t e, uint8_t x, uint8_t y, uint8_t sy, uint16_t fg, uint16_t bg, const char *s)
{
    uint8_t i = 0;
    while (s[i] && i < 7 && shown[e][i] == s[i]) i++;
    if (s[i] == 0 && shown[e][i] == 0) return;           /* unchanged */
    lcd_text(x, y, s, 1, sy, fg, bg);
    for (i = 0; i < 7 && s[i]; i++) shown[e][i] = s[i];
    shown[e][i] = 0;
}

void ui_main_init(void)
{
    uint8_t i;
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    for (i = 0; i < 3; i++) lcd_fill(box_x(i), UI_HDR_Y, UI_BOX_W, UI_HDR_H, th(R_BOX));
    for (i = 0; i < NPORTS; i++) {
        uint8_t x = box_x(i % 3), y = cell_y(i);
        char d[2];
        lcd_fill(x, y, UI_BOX_W, UI_CELL_H, th(R_BOX));
        lcd_fill(x + UI_BADGE_X, y + 2, UI_BADGE_W, UI_CELL_H - 4, th(R_BADGE0 + i));
        d[0] = (char)('1' + i); d[1] = 0;
        lcd_text(x + UI_BADGE_X + 2, y + 6, d, 1, 2, th(R_BG), th(R_BADGE0 + i));
        shown_dot[i] = 0xFF;
    }
    for (i = 0; i < NELEM; i++) shown[i][0] = 0;
    next_el = 0;
}

/* status text for a port */
static void status_text(char *s, uint8_t state, uint16_t cur_counts)      /* bit 4 of state: parked by a user limit */
{
    static const CODE char *names[8] = { "", "TEST", "", "REST", "WAIT", "FULL", "BLEED", "ERR" };
    uint8_t k = 0;
    if ((state & 0x17) == (0x10 | PS_CHARGE)) { s[0] = 'L'; s[1] = 'I'; s[2] = 'M'; s[3] = 'I'; s[4] = 'T'; s[5] = 0; return; }   /* held off by a user limit */
    if ((state & 7) == PS_CHARGE) { fmt_a2(s, cal_port_ma(cur_counts)); return; }
    while (names[state & 7][k]) { s[k] = names[state & 7][k]; k++; }
    s[k] = 0;
}

static uint16_t status_color(uint8_t state)
{
    switch (state) {
    case PS_CHARGE: return th(R_VOLT);
    case PS_FULL:   return th(R_GOOD);
    case PS_FAULT:  return th(R_BAD);
    case PS_BLEED:  return th(R_INFO);
    default:        return th(R_WARN);
    }
}

static void element(const ui_state_t *s, uint8_t e)
{
    static char XD t[8];                       /* external RAM: direct RAM is full */
    if (e < 3) {
        uint16_t col = th(R_TEXT), bg = s->run ? th(R_RUN) : th(R_BOX);
        if (e == 0) fmt_a1(t, cal_set_to_ma(s->iset));
        else if (e == 1) {
            if (s->alarm) { static const CODE char *nm[7] = { "", "RAIL!", "VIN!", "VCC!", "SND ON", "SND OFF", "V LIMIT" }; uint8_t k = 0; while (nm[s->alarm][k]) { t[k] = nm[s->alarm][k]; k++; } t[k] = 0; col = (s->alarm == UA_SND_ON || s->alarm == UA_SND_OFF) ? th(R_INFO) : th(R_WARN); }
            else { fmt_v1(t, cal_vin_mv(s->vin)); col = th(R_VIN); }
        } else fmt_v2(t, cal_port_mv(s->tgt));
        /* the box colour changes with run state: repaint the box when it does */
        if (shown[e][8] != (char)(s->run + 1)) { lcd_fill(box_x(e), UI_HDR_Y, UI_BOX_W, UI_HDR_H, bg); shown[e][0] = 0; shown[e][8] = (char)(s->run + 1); }
        put(e, box_x(e) + UI_HDR_TX, UI_HDR_Y + UI_HDR_TY, 2, col, bg, t);
    } else {
        uint8_t k = e - 3, p = k / 3, part = k % 3;
        uint8_t x = box_x(p % 3), y = cell_y(p);
        if (part == 0) {
            fmt_v2(t, cal_port_mv(s->v[p]));
            put(e, x + UI_CELL_VX, y + UI_CELL_VY, 2, th(R_VOLT), th(R_BOX), t);
        } else if (part == 1) {
            status_text(t, (uint8_t)(s->state[p] | (s->parked[p] ? 0x10 : 0)), s->i[p]);
            /* pad so a shorter text overwrites the previous one */
            { uint8_t n = slen(t); while (n < 5) t[n++] = ' '; t[n] = 0; }
            put(e, x + UI_CELL_IX, y + UI_CELL_IY, 1, s->parked[p] && s->state[p] == PS_CHARGE ? th(R_WARN) : status_color(s->state[p]), th(R_BOX), t);
        } else {
            uint8_t st = s->state[p] & 7;
            uint16_t c = th(R_DOT0 + st);
            if (st == PS_FAULT && !s->blink) c = th(R_BOX);               /* errors blink */
            if (shown_dot[p] != (uint8_t)(st | (c == th(R_BOX) ? 0x80 : 0))) {
                shown_dot[p] = (uint8_t)(st | (c == th(R_BOX) ? 0x80 : 0));
                lcd_fill(x + UI_DOT_X, y + UI_DOT_Y, UI_DOT_S, UI_DOT_S, c);
            }
        }
    }
}

static char XD dbg_shown[DBG_ROWS][DBG_COLS + 1];
static uint16_t XD dbg_col[DBG_ROWS];

void ui_text_enter(void)
{
    uint8_t r, i;
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    for (r = 0; r < DBG_ROWS; r++) for (i = 0; i <= DBG_COLS; i++) dbg_shown[r][i] = 0;   /* the whole row: a stale tail would count as drawn */
}

/* draws only the span of characters that changed: a whole row would take ~13 ms on this bus.  A different colour
 * redraws the whole row. */
void ui_text_row(uint8_t row, const char *text, uint16_t color)
{
    static char XD part[DBG_COLS + 1];
    uint8_t first = 0, last, n, i;
    if (row >= DBG_ROWS) return;
    if (dbg_col[row] != color) { dbg_col[row] = color; for (i = 0; i <= DBG_COLS; i++) dbg_shown[row][i] = 0; }
    n = slen(text);
    if (n > DBG_COLS) n = DBG_COLS;
    while (first < n && dbg_shown[row][first] == text[first]) first++;
    if (first == n && dbg_shown[row][n] == 0) return;                /* unchanged */
    if (dbg_shown[row][0] == 0) first = 0;                           /* nothing drawn yet */
    last = n;
    while (last > first && dbg_shown[row][last - 1] == text[last - 1] && dbg_shown[row][last - 1]) last--;
    for (i = first; i < last; i++) part[i - first] = text[i];
    part[last - first] = 0;
    lcd_text((uint8_t)(first * 6), (uint8_t)(row * 8), part, 1, 1, color, th(R_BG));
    for (i = 0; i < n; i++) dbg_shown[row][i] = text[i];
    dbg_shown[row][n] = 0;
}

/* Every REFRESH_EVERY-th step the element is redrawn even if it did not change: a glitch that corrupted a few pixels would
 * otherwise stay on screen forever (only changes are drawn). */
#define REFRESH_EVERY 8          /* must not share a factor with NELEM (21 = 3 x 7), or only some elements would ever be refreshed */
static uint8_t XD refresh_div;

void ui_main_step(const ui_state_t *s)
{
    uint8_t e = next_el;
    if (++next_el == NELEM) next_el = 0;
    if (++refresh_div >= REFRESH_EVERY) {
        refresh_div = 0;
        if (e >= 3 && (e - 3) % 3 == 2) shown_dot[(e - 3) / 3] = 0xFF;     /* the state dots have their own cache */
        else shown[e][0] = 0;
    }
    element(s, e);
}

void ui_main_all(const ui_state_t *s)
{
    uint8_t e;
    for (e = 0; e < NELEM; e++) element(s, e);
}
