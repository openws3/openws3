#include "portset.h"
#include "menu.h"
#include "input.h"
#include "cal.h"
#include "cfg.h"

static uint8_t  XD sel, editing, backup, running;
static uint16_t XD idle;

void portset_open(uint8_t run) { sel = 0; editing = 0; running = run; idle = 0; }
uint8_t portset_port(void) { return sel; }
uint8_t portset_editing(void) { return editing; }
uint8_t portset_locked(void) { return running; }

uint8_t portset_get(uint8_t k)
{
    if (settings.ch_off & (1u << k)) return PV_OFF;
    return settings.ch_cur[k] ? settings.ch_cur[k] : PV_AUTO;
}

uint8_t portset_valid(uint8_t v)
{
    return v == PV_OFF || v == PV_AUTO || (v >= CUR_MIN && v <= CUR_MAX && (v - CUR_MIN) % CUR_UNITS_PER_100MA == 0);
}

void portset_put(uint8_t k, uint8_t v)
{
    if (v == PV_OFF) { settings.ch_off |= (uint8_t)(1u << k); return; }
    settings.ch_off &= (uint8_t)~(1u << k);
    settings.ch_cur[k] = v == PV_AUTO ? 0 : v;
}

uint8_t portset_step(uint8_t v, uint8_t up)
{
    if (up) return v == PV_OFF ? PV_AUTO : v == PV_AUTO ? CUR_MIN : v + CUR_UNITS_PER_100MA <= CUR_MAX ? (uint8_t)(v + CUR_UNITS_PER_100MA) : v;
    return v == PV_OFF ? PV_OFF : v == PV_AUTO ? PV_OFF : v == CUR_MIN ? PV_AUTO : (uint8_t)(v - CUR_UNITS_PER_100MA);
}

static void cancel(void) { if (editing) { portset_put(sel, backup); editing = 0; } }

void portset_context(uint8_t run) { running = run; if (run) cancel(); }

uint8_t portset_pass(void)
{
    if (++idle < PORTSET_IDLE) return MA_NONE;
    idle = 0;
    cancel();
    return MA_EXIT;
}

uint8_t portset_event(uint8_t ev)
{
    uint8_t dn = ev == EV_B1 || ev == EV_B1_REP, up = ev == EV_B2 || ev == EV_B2_REP;
    idle = 0;
    if (editing) {
        if (up || dn) { portset_put(sel, portset_step(portset_get(sel), up)); return MA_CHANGED; }
        if (ev == EV_OK) { editing = 0; return MA_CHANGED; }
        if (ev == EV_BACK) { cancel(); return MA_CHANGED; }
        return MA_NONE;
    }
    /* taps only: the long press that opened the mode may still be held, and a repeat from it must not move the selection */
    if (ev == EV_B1) { sel = sel ? (uint8_t)(sel - 1) : (uint8_t)(NPORTS - 1); return MA_CHANGED; }
    if (ev == EV_B2) { sel = sel < NPORTS - 1 ? (uint8_t)(sel + 1) : 0; return MA_CHANGED; }
    if (ev == EV_BACK) return MA_EXIT;
    if (ev == EV_OK) {
        if (running) return MA_LOCKED;
        backup = portset_get(sel); editing = 1;
        return MA_CHANGED;
    }
    return MA_NONE;
}
