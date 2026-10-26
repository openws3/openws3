#include "menu.h"
#include "input.h"
#include "cal.h"
#include "fmt.h"
#include "limits.h"
#include "theme.h"
#include "cfg.h"

/* items are numbered category by category; CAT_FIRST[c] .. CAT_FIRST[c + 1] - 1 belong to category c (diagnostics is an action, no items) */
enum { IT_VOLT, IT_CURRENT, IT_ILIM, IT_VMIN, IT_VCUT, IT_LMODE, IT_SOUND, IT_THEME, NITEMS };
enum { CAT_CHARGE, CAT_LIMITS, CAT_PREFS, CAT_DIAG, NCATS };
static const uint8_t CAT_FIRST[NCATS + 1] = { IT_VOLT, IT_ILIM, IT_SOUND, NITEMS, NITEMS };

static uint8_t XD level, cat, cursor, editing, backup, running, cur_max;      /* level 0: the categories (cursor = category), 1: one category's items */
static char XD rowtxt[ROW_COLS + 1];
static char XD tmp[12];

static const char *const CAT_LABEL[NCATS] = { "CHARGING", "LIMITS", "PREFERENCES", "DIAGNOSTICS" };
static const char *const LABEL[NITEMS] = { "VOLTAGE", "CURRENT", "MAX IN AMPS", "MIN IN VOLTS", "BELOW MIN", "REGULATION", "SOUND", "THEME" };
static const char *const VOLT_NAME[4] = { "4.20V", "4.35V", "3.80V STOR", "3.85V STOR" };

static uint8_t XD *slot(uint8_t it)
{
    switch (it) {
    case IT_VOLT: return &settings.volt;
    case IT_CURRENT: return &settings.cur;
    case IT_SOUND: return &settings.sound;
    case IT_THEME: return &settings.theme;
    case IT_ILIM: return &settings.ilim;
    case IT_VMIN: return &settings.vmin;
    case IT_VCUT: return &settings.vcut;
    case IT_LMODE: return &settings.lmode;
    default: return 0;
    }
}

static uint8_t is_locked(uint8_t it) { return running && (it == IT_VOLT || it == IT_CURRENT); }

void menu_open(uint8_t run, uint8_t cmax) { level = cat = cursor = 0; editing = 0; running = run; cur_max = cmax; }
void menu_context(uint8_t run, uint8_t cmax) { running = run; cur_max = cmax; }
uint8_t menu_editing(void) { return editing; }

static uint8_t count(void) { return level ? (uint8_t)(CAT_FIRST[cat + 1] - CAT_FIRST[cat]) : NCATS; }
static uint8_t item(void) { return (uint8_t)(CAT_FIRST[cat] + cursor); }

static void clamp_step(uint8_t XD *v, uint8_t up, uint8_t max) { if (up) { if (*v < max) (*v)++; } else if (*v) (*v)--; }

static void step(uint8_t it, uint8_t up)
{
    uint8_t XD *v = slot(it);
    switch (it) {
    case IT_VOLT: clamp_step(v, up, NVOLTS - 1); break;
    case IT_CURRENT:
        if (up) { if (*v + CUR_UNITS_PER_100MA <= cur_max) *v = (uint8_t)(*v + CUR_UNITS_PER_100MA); }
        else if (*v >= CUR_MIN + CUR_UNITS_PER_100MA) *v = (uint8_t)(*v - CUR_UNITS_PER_100MA);
        break;
    case IT_SOUND: case IT_VCUT: *v = up; break;
    case IT_ILIM: *v = lim_step(*v, up, ILIM_MIN, ILIM_KNEE, ILIM_MAX); break;
    case IT_VMIN: *v = vmin_step(*v, up); break;
    case IT_LMODE: clamp_step(v, up, LM_NMODES - 1); break;
    case IT_THEME: clamp_step(v, up, TH_COUNT - 1); break;
    }
}

uint8_t menu_event(uint8_t ev)
{
    uint8_t up = ev == EV_B1 || ev == EV_B1_REP, dn = ev == EV_B2 || ev == EV_B2_REP;
    if (editing) {
        if (up || dn) { step(item(), dn); return MA_CHANGED; }
        if (ev == EV_OK) { editing = 0; return MA_CHANGED; }
        if (ev == EV_BACK) { *slot(item()) = backup; editing = 0; return MA_CHANGED; }
        return MA_NONE;
    }
    if (up) { cursor = cursor ? (uint8_t)(cursor - 1) : (uint8_t)(count() - 1); return MA_CHANGED; }
    if (dn) { cursor = cursor < count() - 1 ? (uint8_t)(cursor + 1) : 0; return MA_CHANGED; }
    if (ev == EV_BACK) {
        if (!level) return MA_EXIT;
        level = 0; cursor = cat;
        return MA_CHANGED;
    }
    if (ev == EV_OK) {
        if (!level) {
            if (cursor == CAT_DIAG) return MA_DIAG;
            cat = cursor; level = 1; cursor = 0;
            return MA_CHANGED;
        }
        if (is_locked(item())) return MA_LOCKED;
        backup = *slot(item()); editing = 1;
        return MA_CHANGED;
    }
    return MA_NONE;
}

/* the value of an item as text */
static void value(uint8_t it)
{
    uint8_t v = *slot(it);
    switch (it) {
    case IT_VOLT: rb_right(VOLT_NAME[v & 3], ROW_COLS - 1); break;
    case IT_CURRENT: fmt_a1(tmp, cal_set_to_ma(v)); rb_right(tmp, ROW_COLS - 1); break;
    case IT_SOUND: rb_right(v ? "ON" : "OFF", ROW_COLS - 1); break;
    case IT_THEME: rb_right(th_name(v), ROW_COLS - 1); break;
    case IT_ILIM:
        if (v) { fmt_a1(tmp, (uint16_t)(v * 100u)); rb_right(tmp, ROW_COLS - 1); }
        else rb_right("OFF", ROW_COLS - 1);
        break;
    case IT_VMIN: if (v) { if (v < 200) fmt_v2(tmp, (uint16_t)(v * 50u)); else fmt_v1(tmp, (uint16_t)(v * 50u)); rb_right(tmp, ROW_COLS - 1); } else rb_right("OFF", ROW_COLS - 1); break;
    case IT_VCUT: rb_right(v ? "STOP RUN" : "LIMIT", ROW_COLS - 1); break;
    case IT_LMODE: rb_right(lim_mode_name(v), ROW_COLS - 1); break;
    }
}

const char *menu_text(uint8_t row)
{
    rb_begin(rowtxt);
    if (row == 0) {
        rb_str(level ? CAT_LABEL[cat] : "MENU");
        rb_pad(12); rb_str(level ? "HOLD:BACK" : "HOLD:SAVE");
        rb_pad(ROW_COLS - 3); rb_num((uint32_t)cursor + 1, 0); rb_ch('/'); rb_num(count(), 0);
    } else if (row <= count()) {
        uint8_t i = (uint8_t)(row - 1);
        rb_ch(i != cursor ? ' ' : editing ? '*' : '>');
        if (!level) rb_str(CAT_LABEL[i]);
        else { rb_str(LABEL[CAT_FIRST[cat] + i]); rb_pad(13); value((uint8_t)(CAT_FIRST[cat] + i)); }
    } else if (row < MENU_ROWS - 1) {
        /* empty */
    } else if (level && is_locked(item()) && !editing) {
        rb_str("LOCKED WHILE CHARGING");
    } else {                                             /* short cues in the corners, above the two buttons under the screen */
        rb_str(editing ? "< -" : "< UP");
        rb_pad(editing ? 5 : 7);
        rb_str(editing ? "BOTH OK HOLD:UNDO" : level ? "BOTH = EDIT" : "BOTH = OPEN");
        rb_right(editing ? "+ >" : "DOWN >", ROW_COLS - 1);
    }
    return rb_end();
}
