#include "menu.h"
#include "input.h"
#include "cal.h"
#include "fmt.h"
#include "limits.h"

enum { IT_MODE, IT_CURRENT, IT_SOUND, IT_ILIM, IT_VMIN, IT_VCUT, IT_LMODE, IT_DIAG, IT_EXIT, NITEMS };
#define VISIBLE 8

menu_vals_t XD menu_v;
static uint8_t XD cursor, editing, backup, running, cur_max;
static char XD rowtxt[ROW_COLS + 1];
static char XD tmp[12];

static const char *const LABEL[NITEMS] = { "MODE", "CURRENT", "SOUND", "MAX IN AMPS", "MIN IN VOLTS", "BELOW MIN", "REGULATION", "DIAGNOSTICS", "EXIT & SAVE" };
static const char *const MODE_NAME[4] = { "4.20V", "4.35V", "3.80V STOR", "3.85V STOR" };
static const char *const LMODE_NAME[LM_NMODES] = { "EQUAL", "CH1 FIRST", "LOW V FIRST", "HIGH V FIRST" };

static uint8_t XD *slot(uint8_t it)
{
    switch (it) {
    case IT_MODE: return &menu_v.mode;
    case IT_CURRENT: return &menu_v.cur;
    case IT_SOUND: return &menu_v.sound;
    case IT_ILIM: return &menu_v.ilim;
    case IT_VMIN: return &menu_v.vmin;
    case IT_VCUT: return &menu_v.vcut;
    case IT_LMODE: return &menu_v.lmode;
    default: return 0;
    }
}

static uint8_t is_locked(uint8_t it) { return running && (it == IT_MODE || it == IT_CURRENT); }

void menu_open(uint8_t run, uint8_t cmax) { cursor = 0; editing = 0; running = run; cur_max = cmax; }
void menu_context(uint8_t run, uint8_t cmax) { running = run; cur_max = cmax; }
uint8_t menu_editing(void) { return editing; }

static void step(uint8_t it, uint8_t up)
{
    uint8_t XD *v = slot(it);
    switch (it) {
    case IT_MODE: if (up) { if (*v < 3) (*v)++; } else if (*v) (*v)--; break;
    case IT_CURRENT:
        if (up) { if (*v + CUR_UNITS_PER_100MA <= cur_max) *v = (uint8_t)(*v + CUR_UNITS_PER_100MA); }
        else if (*v >= CUR_MIN + CUR_UNITS_PER_100MA) *v = (uint8_t)(*v - CUR_UNITS_PER_100MA);
        break;
    case IT_SOUND: case IT_VCUT: *v = up; break;
    case IT_ILIM: *v = up ? lim_ilim_next(*v) : lim_ilim_prev(*v); break;
    case IT_VMIN: *v = up ? lim_vmin_next(*v) : lim_vmin_prev(*v); break;
    case IT_LMODE: if (up) { if (*v < LM_NMODES - 1) (*v)++; } else if (*v) (*v)--; break;
    }
}

uint8_t menu_event(uint8_t ev)
{
    if (editing) {
        switch (ev) {
        case EV_B2_SHORT: step(cursor, 1); return MA_CHANGED;
        case EV_B1_SHORT: step(cursor, 0); return MA_CHANGED;
        case EV_B2_LONG: editing = 0; return MA_CHANGED;
        case EV_B1_LONG: *slot(cursor) = backup; editing = 0; return MA_CHANGED;
        }
        return MA_NONE;
    }
    switch (ev) {
    case EV_B1_SHORT: if (cursor) { cursor--; return MA_CHANGED; } break;
    case EV_B2_SHORT: if (cursor < NITEMS - 1) { cursor++; return MA_CHANGED; } break;
    case EV_B2_LONG:
        if (cursor == IT_DIAG) return MA_DIAG;
        if (cursor == IT_EXIT) return MA_EXIT;
        if (is_locked(cursor)) return MA_LOCKED;
        backup = *slot(cursor); editing = 1;
        return MA_CHANGED;
    }
    return MA_NONE;
}

/* the value of an item as text */
static void value(uint8_t it)
{
    uint8_t v = *slot(it);
    switch (it) {
    case IT_MODE: rb_right(MODE_NAME[v & 3], ROW_COLS - 1); break;
    case IT_CURRENT: fmt_a1(tmp, cal_set_to_ma(v)); rb_right(tmp, ROW_COLS - 1); break;
    case IT_SOUND: rb_right(v ? "ON" : "OFF", ROW_COLS - 1); break;
    case IT_ILIM:
        if (v >= ILIM_MAX) rb_right("10.0A", ROW_COLS - 1);                      /* fmt_a1 stops at 9.9A */
        else if (v) { fmt_a1(tmp, (uint16_t)(v * 100u)); rb_right(tmp, ROW_COLS - 1); }
        else rb_right("OFF", ROW_COLS - 1);
        break;
    case IT_VMIN: if (v) { fmt_v1(tmp, (uint16_t)(v * 100u)); rb_right(tmp, ROW_COLS - 1); } else rb_right("OFF", ROW_COLS - 1); break;
    case IT_VCUT: rb_right(v ? "STOP RUN" : "LIMIT", ROW_COLS - 1); break;
    case IT_LMODE: rb_right(LMODE_NAME[v < LM_NMODES ? v : 0], ROW_COLS - 1); break;
    }
}

const char *menu_text(uint8_t row)
{
    rb_begin(rowtxt);
    if (row == 0) {
        rb_str("MENU");
        rb_pad(ROW_COLS - 4); rb_num((uint32_t)cursor + 1, 0); rb_ch('/'); rb_num(NITEMS, 0);
    } else if (row <= VISIBLE) {
        uint8_t first = cursor >= VISIBLE ? (uint8_t)(cursor - (VISIBLE - 1)) : 0, it = (uint8_t)(first + row - 1);
        if (it < NITEMS) {
            rb_ch(it != cursor ? ' ' : editing ? '*' : '>');
            rb_str(LABEL[it]);
            if (slot(it)) { rb_pad(13); value(it); }
        }
    } else if (is_locked(cursor) && !editing) {
        rb_str("LOCKED WHILE CHARGING");
    } else {                                             /* short cues in the corners, above the two buttons under the screen */
        rb_str(editing ? "< -" : "< UP");
        rb_pad(editing ? 6 : 7);
        rb_str(editing ? "<HOLD X HOLD> OK" : cursor == IT_DIAG ? "HOLD > OPEN" : cursor == IT_EXIT ? "HOLD > SAVE" : "HOLD > EDIT");
        rb_right(editing ? "+ >" : "DOWN >", ROW_COLS - 1);
    }
    return rb_end();
}
