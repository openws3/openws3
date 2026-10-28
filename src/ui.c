#include "ui.h"
#include "layout.h"
#include "theme.h"
#include "lcd.h"
#include "cal.h"
#include "fmt.h"
#include "chg.h"
#include "dbgscr.h"
#include "portset.h"
#include "version.h"
#include "version_build.h"

#define NELEM (3 + NPORTS * 3)           /* 3 header boxes, then per port: voltage, status text, dot */
#define NBOX  (3 + NPORTS)               /* the 3 header boxes, then the port cells */

/* What the screen shows, so that only changes are drawn.  An element is its text (or, for a dot, a mark that it is drawn) and its
 * colour; a box is its colour.  Repainting a box wipes its elements, which box_draw() marks as not drawn. */
static char    XD shown[NELEM][8];
static uint16_t XD shown_fg[NELEM];
static uint16_t XD box_bg[NBOX];
static uint8_t XD next_el;

static uint8_t box_x(uint8_t i)  { return UI_BOX_X0 + i * UI_BOX_PITCH; }
static uint8_t cell_y(uint8_t p) { return UI_CELL_Y0 + (p / 3) * UI_CELL_PITCH_Y; }

static uint8_t slen(const char *s) { uint8_t n = 0; while (s[n]) n++; return n; }
static void scopy(char XD *d, const char *s) { while ((*d++ = *s++) != 0) ; }

/* Only the characters that changed are drawn: at the bus rate a whole row takes ~13 ms, a 1x2 glyph ~1 ms.  diff() finds the span
 * [d_first, d_last) of `s` (n characters) that differs from `was`, what the screen shows; 0 = nothing changed. */
static uint8_t XD d_first, d_last;
static uint8_t diff(const char XD *was, const char *s, uint8_t n)
{
    d_first = 0;
    while (d_first < n && was[d_first] == s[d_first]) d_first++;
    if (d_first == n && was[n] == 0) return 0;
    d_last = n;
    if (slen(was) == n) while (d_last > d_first && was[d_last - 1] == s[d_last - 1]) d_last--;
    return 1;
}

/* draw that span of `s`, whose first character sits at x */
static void draw_span(uint8_t x, uint8_t y, uint8_t sy, const char *s, uint16_t fg, uint16_t bg)
{
    static char XD part[ROW_COLS + 1];
    uint8_t i;
    for (i = d_first; i < d_last; i++) part[i - d_first] = s[i];
    part[d_last - d_first] = 0;
    lcd_text((uint8_t)(x + d_first * 6), y, part, 1, sy, fg, bg);
}

/* centre `text` on the 160 px wide screen at scale sx,sy */
static void centered(uint8_t y, const char *text, uint8_t sx, uint8_t sy, uint16_t fg, uint16_t bg)
{
    uint8_t w = (uint8_t)(slen(text) * 6 * sx);
    lcd_text(w >= LCD_PX_W ? 0 : (uint8_t)((LCD_PX_W - w) / 2), y, text, sx, sy, fg, bg);
}

#define STR_(x) #x
#define STR(x)  STR_(x)
#if FW_PRE
#define FW_STAGE "-BETA." STR(FW_PRE)
#else
#define FW_STAGE ""
#endif

void ui_splash(void)
{
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    lcd_fill(0, 14, LCD_PX_W, 3, th(R_BAD));
    lcd_fill(0, 63, LCD_PX_W, 3, th(R_BAD));
    centered(24, "OPENWS3", 2, 2, th(R_TEXT), th(R_BG));
    centered(44, "OPEN FW FOR WHOOPSTOR V3", 1, 1, th(R_INFO), th(R_BG));
    centered(54, "V" STR(FW_MAJOR) "." STR(FW_MINOR) FW_STAGE " " FW_BUILD_STR, 1, 1, th(R_DIM), th(R_BG));
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

static void put(uint8_t e, uint8_t x, uint8_t y, uint8_t sy, uint16_t fg, uint16_t bg, const char *s)
{
    uint8_t i, n = slen(s);
    if (n > 7) n = 7;
    if (shown_fg[e] != fg) { shown_fg[e] = fg; shown[e][0] = 0; }     /* a new colour redraws it all */
    if (!diff(shown[e], s, n)) return;
    draw_span(x, y, sy, s, fg, bg);
    for (i = 0; i < n; i++) shown[e][i] = s[i];
    shown[e][n] = 0;
}

/* a coloured square (a dot, a frame) is drawn again only when its colour changed or it was wiped */
static uint8_t changed(uint8_t e, uint16_t c)
{
    if (shown[e][0] && shown_fg[e] == c) return 0;
    shown[e][0] = 1; shown_fg[e] = c;
    return 1;
}

/* box b (a header box, or port b - 3) painted in bg, its elements marked as wiped */
static void box_draw(uint8_t b, uint16_t bg)
{
    uint8_t p, x, y, k;
    char d[2];
    box_bg[b] = bg;
    if (b < 3) {
        lcd_fill(box_x(b), UI_HDR_Y, UI_BOX_W, UI_HDR_H, bg);
        shown[b][0] = 0;
        return;
    }
    p = b - 3; x = box_x(p % 3); y = cell_y(p);
    lcd_fill(x, y, UI_BOX_W, UI_CELL_H, bg);
    lcd_fill(x + UI_BADGE_X, y + 2, UI_BADGE_W, UI_CELL_H - 4, th(R_BADGE0 + p));
    d[0] = (char)('1' + p); d[1] = 0;
    lcd_text(x + UI_BADGE_X + 2, y + 6, d, 1, 2, th(R_BG), th(R_BADGE0 + p));
    for (k = 0; k < 3; k++) shown[3 + p * 3 + k][0] = 0;
}

static void box(uint8_t b, uint16_t bg) { if (box_bg[b] != bg) box_draw(b, bg); }

void ui_main_init(void)
{
    uint8_t b;
    lcd_fill(0, 0, LCD_PX_W, LCD_PX_H, th(R_BG));
    for (b = 0; b < NBOX; b++) box_draw(b, th(R_BOX));
    next_el = 0;
}

/* pad with spaces to n characters, so a shorter text overwrites the previous one */
static void pad(char XD *t, uint8_t n) { uint8_t i = slen(t); while (i < n) t[i++] = ' '; t[i] = 0; }

static void frame(uint8_t x, uint8_t y, uint16_t c)
{
    lcd_fill(x, y, UI_BOX_W, UI_FRAME, c);
    lcd_fill(x, (uint8_t)(y + UI_CELL_H - UI_FRAME), UI_BOX_W, UI_FRAME, c);
    lcd_fill(x, y, UI_FRAME, UI_CELL_H, c);
    lcd_fill((uint8_t)(x + UI_BOX_W - UI_FRAME), y, UI_FRAME, UI_CELL_H, c);
}

static uint16_t status_color(uint8_t state)
{
    switch (state) {
    case PS_CHARGE: return th(R_VOLT);
    case PS_FULL:   return th(R_FULLTX);
    case PS_FAULT:  return th(R_BAD);
    case PS_BLEED:  return th(R_INFO);
    default:        return th(R_WARN);
    }
}

static void header(const ui_state_t *s, uint8_t e, char XD *t) REENT
{
    static const CODE char *const ALARM[] = { "", "VIN!", "VCC!", "RAIL!", "", "V LIMIT" };   /* by STOP_* */
    uint16_t col = th(R_TEXT), bg = s->run ? th(R_RUN) : th(R_BOX);
    if (s->ps) {                                     /* port settings: what this is, the port, what the keys do */
        if (e == 0) scopy(t, "PORTS");
        else if (e == 1) {
            if (s->locked) { scopy(t, "LOCKED"); col = th(R_WARN); }
            else { scopy(t, "PORT "); t[5] = (char)('1' + s->sel); t[6] = 0; }
        } else scopy(t, s->ps == 2 ? "- + OK" : "< > OK");
    } else if (e == 0) {
        fmt_a1(t, cal_set_to_ma(s->iset));
        if (s->mixed) { uint8_t n = slen(t); t[n] = '*'; t[n + 1] = 0; }
    } else if (e == 1) {
        if (s->alarm) { scopy(t, ALARM[s->alarm]); col = th(R_WARN); }
        else { fmt_v1(t, cal_vin_mv(s->vin)); col = th(R_VIN); }
    } else fmt_v2(t, cal_port_mv(s->tgt));
    pad(t, 7);
    box(e, bg);
    put(e, box_x(e) + UI_HDR_TX, UI_HDR_Y + UI_HDR_TY, 2, col, bg, t);
}

/* "0.5A", the current a port charges at */
static void amps1(char XD *t, uint8_t iu) { fmt_a1(t, cal_set_to_ma(iu)); }

static void element(const ui_state_t *s, uint8_t e) REENT
{
    static char XD t[10];                      /* external RAM: direct RAM is full */
    uint8_t k, p, part, x, y, st, off, full;
    uint16_t bg;
    if (e < 3) { header(s, e, t); return; }
    k = e - 3; p = k / 3; part = k % 3;
    x = box_x(p % 3); y = cell_y(p);
    st = s->state[p]; off = s->set[p] == PV_OFF;
    full = !s->ps && st == PS_FULL;                  /* a full port's cell is tinted, so it shows from across the room */
    bg = full ? th(R_FULL) : th(R_BOX);
    box(3 + p, bg);
    if (s->ps) {                                     /* port settings: the value big, what kind of value small, a frame round the selection */
        uint8_t edit = s->ps == 2 && p == s->sel;
        if (part == 0) {
            if (off) scopy(t, "OFF"); else amps1(t, s->want[p]);
            if (edit) { uint8_t n = slen(t), i; for (i = n; i > 0; i--) t[i] = t[i - 1]; t[0] = '<'; t[n + 1] = '>'; t[n + 2] = 0; }
            pad(t, 6);
            put(e, x + UI_CELL_VX, y + UI_CELL_VY, 2, edit ? th(R_WARN) : off ? th(R_DIM) : th(R_TEXT), bg, t);
        } else if (part == 1) {
            scopy(t, off ? "OFF" : s->set[p] == PV_AUTO ? "AUTO" : "SET");
            pad(t, 5);
            put(e, x + UI_CELL_IX, y + UI_CELL_IY, 1, off || s->set[p] == PV_AUTO ? th(R_DIM) : th(R_INFO), bg, t);
        } else {
            uint8_t f = p != s->sel ? 0 : !edit ? 1 : s->blink ? 2 : 3;      /* none, selected, editing (blinks) */
            uint16_t fc = f == 1 ? th(R_INFO) : f == 2 ? th(R_WARN) : th(R_BOX);
            if (changed(e, fc)) frame(x, y, fc);
        }
        return;
    }
    if (part == 0) {
        fmt_v2(t, cal_port_mv(s->v[p]));
        put(e, x + UI_CELL_VX, y + UI_CELL_VY, 2, off ? th(R_DIM) : full ? th(R_FULLTX) : th(R_VOLT), bg, t);
    } else if (part == 1) {
        /* charging: the measured current, 1 s in 4 the target; idle: the target; an OFF port says so; else the state */
        static const CODE char *const NAME[] = { "", "TEST", "", "REST", "FULL", "BLEED", "ERR" };   /* by PS_* */
        uint16_t col = status_color(st);
        if (st == PS_IDLE && off) { scopy(t, "OFF"); col = th(R_DIM); }
        else if ((st == PS_CHARGE && s->tphase) || st == PS_IDLE) { t[0] = '>'; amps1(t + 1, s->want[p]); col = th(R_INFO); }
        else if (st == PS_CHARGE && s->parked[p]) { scopy(t, "LIMIT"); col = th(R_WARN); }      /* waiting for room under a user limit */
        else if (st == PS_CHARGE) fmt_a2(t, cal_port_ma(s->i[p]));
        else scopy(t, NAME[st]);
        pad(t, 5);
        put(e, x + UI_CELL_IX, y + UI_CELL_IY, 1, col, bg, t);
    } else {
        uint16_t c;
        st &= 7;
        c = th(R_DOT0 + st);
        if (st == PS_FAULT && !s->blink) c = bg;               /* errors blink */
        if (changed(e, c)) lcd_fill(x + UI_DOT_X, y + UI_DOT_Y, UI_DOT_S, UI_DOT_S, c);
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

/* a different colour redraws the whole row */
void ui_text_row(uint8_t row, const char *text, uint16_t color)
{
    uint8_t n, i;
    if (row >= DBG_ROWS) return;
    if (dbg_col[row] != color) { dbg_col[row] = color; for (i = 0; i <= DBG_COLS; i++) dbg_shown[row][i] = 0; }
    n = slen(text);
    if (n > DBG_COLS) n = DBG_COLS;
    if (!diff(dbg_shown[row], text, n)) return;
    draw_span(0, (uint8_t)(row * 8), 1, text, color, th(R_BG));
    for (i = 0; i < n; i++) dbg_shown[row][i] = text[i];
    dbg_shown[row][n] = 0;
}

/* Every REFRESH_EVERY-th step the element is redrawn even if it did not change: a glitch that corrupted a few pixels would
 * otherwise stay on screen forever (only changes are drawn). */
#define REFRESH_EVERY 64         /* must not share a factor with NELEM (21 = 3 x 7), or only some elements would ever be refreshed */
static uint8_t XD refresh_div;

void ui_main_step(const ui_state_t *s)
{
    uint8_t e = next_el;
    if (++next_el == NELEM) next_el = 0;
    if (++refresh_div >= REFRESH_EVERY) {
        refresh_div = 0;
        shown[e][0] = 0;
    }
    element(s, e);
}
