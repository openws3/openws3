#include "menu.h"
#include "input.h"
#include "cal.h"
#include "fmt.h"
#include "limits.h"
#include "theme.h"
#include "cfg.h"

/* items are numbered category by category; CAT_FIRST[c] .. CAT_FIRST[c + 1] - 1 belong to category c (diagnostics is an action, no items) */
enum { IT_VOLT, IT_CURRENT, IT_ILIM, IT_VMIN, IT_VCUT, IT_LMODE, IT_SOUND, IT_THEME, IT_USBOFF, NITEMS };
enum { CAT_CHARGE, CAT_LIMITS, CAT_PREFS, CAT_DIAG, NCATS };
static const uint8_t CAT_FIRST[NCATS + 1] = { IT_VOLT, IT_ILIM, IT_SOUND, NITEMS, NITEMS };

static uint8_t XD level, cat, cursor, editing, backup, running;      /* level 0: the categories (cursor = category), 1: one category's items */
static char XD rowtxt[ROW_COLS + 1];
static char XD tmp[12];

static const char *const CAT_LABEL[NCATS] = { "CHARGING", "LIMITS", "PREFERENCES", "DIAGNOSTICS" };
static const char *const LABEL[NITEMS] = { "VOLTAGE", "CURRENT", "MAX IN AMPS", "MIN IN VOLTS", "BELOW MIN", "REGULATION", "SOUND", "THEME", "DISABLE USB POWER" };

/* the setting each item edits, and the largest value of the plain choices (the current and the two limits step by their own rules) */
static uint8_t XD *const CODE SLOT[NITEMS] = { &settings.volt, &settings.cur, &settings.ilim, &settings.vmin, &settings.vcut, &settings.lmode, &settings.sound, &settings.theme, &settings.usb_off };
static const CODE uint8_t MAX[NITEMS] = { NVOLTS - 1, 0, 0, 0, 1, LM_NMODES - 1, 1, TH_COUNT - 1, 1 };

static uint8_t is_locked(uint8_t it) { return running && (it == IT_VOLT || it == IT_CURRENT || it == IT_USBOFF); }

void menu_open(uint8_t run) { level = cat = cursor = 0; editing = 0; running = run; }
void menu_context(uint8_t run) { running = run; }
uint8_t menu_editing(void) { return editing; }

static uint8_t count(void) { return level ? (uint8_t)(CAT_FIRST[cat + 1] - CAT_FIRST[cat]) : NCATS; }
static uint8_t item(void) { return (uint8_t)(CAT_FIRST[cat] + cursor); }

static void step(uint8_t it, uint8_t up)
{
    uint8_t XD *v = SLOT[it];
    switch (it) {
    case IT_CURRENT:
        if (up) { if (*v + CUR_UNITS_PER_100MA <= CUR_MAX) *v = (uint8_t)(*v + CUR_UNITS_PER_100MA); }
        else if (*v >= CUR_MIN + CUR_UNITS_PER_100MA) *v = (uint8_t)(*v - CUR_UNITS_PER_100MA);
        break;
    case IT_ILIM: *v = ilim_step(*v, up); break;
    case IT_VMIN: *v = vmin_step(*v, up); break;
    default: if (up) { if (*v < MAX[it]) (*v)++; } else if (*v) (*v)--;
    }
}

uint8_t menu_event(uint8_t ev)
{
    uint8_t up = ev == EV_B1 || ev == EV_B1_REP, dn = ev == EV_B2 || ev == EV_B2_REP;
    if (editing) {
        if (up || dn) { step(item(), dn); return MA_CHANGED; }
        if (ev == EV_OK) { editing = 0; return MA_CHANGED; }
        if (ev == EV_BACK) { *SLOT[item()] = backup; editing = 0; return MA_CHANGED; }
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
        backup = *SLOT[item()]; editing = 1;
        return MA_CHANGED;
    }
    return MA_NONE;
}

/* the value of an item as text */
static void value(uint8_t it)
{
    uint8_t v = *SLOT[it];
    uint16_t mv;
    switch (it) {
    case IT_VOLT: rb_right(sup_volt_name(v), ROW_COLS - 1); break;
    case IT_CURRENT: fmt_a1(tmp, cal_set_to_ma(v)); rb_right(tmp, ROW_COLS - 1); break;
    case IT_SOUND: rb_right(v ? "ON" : "OFF", ROW_COLS - 1); break;
    case IT_THEME: rb_right(th_name(v), ROW_COLS - 1); break;
    case IT_USBOFF: rb_right(v ? "YES" : "NO", ROW_COLS - 1); break;
    case IT_ILIM:
        if (v) { fmt_a1(tmp, (uint16_t)(v * 100u)); rb_right(tmp, ROW_COLS - 1); }
        else rb_right("OFF", ROW_COLS - 1);
        break;
    case IT_VMIN:
        if (!v) { rb_right("OFF", ROW_COLS - 1); break; }
        mv = (uint16_t)VMIN_MV(v);
        if (mv < 10000) fmt_v2(tmp, mv); else fmt_v1(tmp, mv);                 /* 4.85V, then 12.5V */
        rb_right(tmp, ROW_COLS - 1);
        break;
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
