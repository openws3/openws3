/* Host tests for the port settings mode.  make test
 * B1 = the left button, B2 = the right one.  Select: B1 previous, B2 next (wraps), OK edit, BACK leave.
 * Edit: B2 +, B1 -, OK confirm, BACK cancel. */
#include <stdio.h>
#include <string.h>
#include "portset.h"
#include "menu.h"
#include "input.h"
#include "cal.h"
#include "cfg.h"

#include "check.h"

static void fresh(uint8_t run)
{
    memset(settings.ch_cur, 0, sizeof settings.ch_cur); settings.ch_off = 0; settings.cur = 35;
    portset_open(run);
}
static void ev(uint8_t e, int n) { while (n--) portset_event(e); }

/* the values a port can take, in order, and their encoding into the settings */
static void t_values(void)
{
    uint8_t v = PV_OFF, n = 0, k;
    CHECK(portset_step(PV_OFF, 0) == PV_OFF);                                  /* no wrap at either end */
    while (n < 20) { uint8_t w = portset_step(v, 1); if (w == v) break; CHECK(w > v || v == PV_OFF); v = w; n++; }
    CHECK(v == CUR_MAX && n == 12);                                             /* OFF, AUTO, 0.3 .. 1.3 A */
    CHECK(portset_step(PV_AUTO, 1) == CUR_MIN && portset_step(CUR_MIN, 0) == PV_AUTO && portset_step(PV_AUTO, 0) == PV_OFF);
    CHECK(portset_step(28, 0) == 21 && portset_step(84, 1) == 91);

    CHECK(portset_valid(PV_OFF) && portset_valid(PV_AUTO) && portset_valid(21) && portset_valid(56) && portset_valid(91));
    CHECK(!portset_valid(2) && !portset_valid(20) && !portset_valid(22) && !portset_valid(92) && !portset_valid(255));

    fresh(0);
    for (k = 0; k < NPORTS; k++) CHECK(portset_get(k) == PV_AUTO);
    portset_put(1, 56); portset_put(3, PV_OFF);
    CHECK(settings.ch_cur[1] == 56 && settings.ch_off == 0x08 && portset_get(1) == 56 && portset_get(3) == PV_OFF);
    portset_put(3, PV_AUTO); portset_put(1, PV_AUTO);
    CHECK(settings.ch_off == 0 && settings.ch_cur[1] == 0 && portset_get(3) == PV_AUTO);
    portset_put(2, 42); portset_put(2, PV_OFF);                                  /* OFF keeps the current underneath */
    CHECK(portset_get(2) == PV_OFF && settings.ch_cur[2] == 42);
}

static void t_select(void)
{
    fresh(0);
    CHECK(portset_port() == 0 && !portset_editing());
    ev(EV_B2, 1); CHECK(portset_port() == 1);
    ev(EV_B1, 2); CHECK(portset_port() == 5);                                   /* wraps */
    ev(EV_B2, 1); CHECK(portset_port() == 0);
    ev(EV_B2_REP, 3); ev(EV_B2_LONG, 1); CHECK(portset_port() == 0);             /* the press that opened the mode does not move it */
    CHECK(portset_event(EV_BACK) == MA_EXIT);
}

static void t_edit(void)
{
    fresh(0);
    ev(EV_B2, 2);
    CHECK(portset_event(EV_OK) == MA_CHANGED && portset_editing());
    ev(EV_B2, 1); CHECK(portset_get(2) == 21);                                   /* AUTO -> 0.3 A */
    ev(EV_B2_REP, 4); CHECK(portset_get(2) == 49);                               /* holding repeats */
    ev(EV_OK, 1); CHECK(!portset_editing() && portset_get(2) == 49);             /* confirmed */
    ev(EV_OK, 1); ev(EV_B1, 9); CHECK(portset_get(2) == PV_OFF && (settings.ch_off & 4));
    ev(EV_BACK, 1); CHECK(!portset_editing() && portset_get(2) == 49 && !(settings.ch_off & 4));   /* cancelled: back to 0.7 A */
    CHECK(portset_get(0) == PV_AUTO && portset_get(1) == PV_AUTO);              /* the others are untouched */
}

static void t_idle(void)
{
    int n;
    fresh(0);
    for (n = 1; n < PORTSET_IDLE; n++) CHECK(portset_pass() == MA_NONE);
    CHECK(portset_pass() == MA_EXIT);                                            /* 10 s without a key */
    fresh(0);
    for (n = 0; n < PORTSET_IDLE - 10; n++) portset_pass();
    ev(EV_B2, 1);                                                                /* a key starts the time again */
    for (n = 1; n < PORTSET_IDLE; n++) CHECK(portset_pass() == MA_NONE);
    fresh(0);
    ev(EV_OK, 1); ev(EV_B2, 3);
    for (n = 0; n < PORTSET_IDLE; n++) portset_pass();
    CHECK(!portset_editing() && portset_get(0) == PV_AUTO);                      /* timed out while editing: the old value */
}

static void t_locked(void)
{
    fresh(1);
    CHECK(portset_locked());
    ev(EV_B2, 1); CHECK(portset_port() == 1);                                   /* the values can be looked through */
    CHECK(portset_event(EV_OK) == MA_LOCKED && !portset_editing());
    fresh(0);
    ev(EV_OK, 1); ev(EV_B2, 2);
    CHECK(portset_get(0) == 28);
    portset_context(1);                                                         /* charging started (over USB) during an edit */
    CHECK(!portset_editing() && portset_get(0) == PV_AUTO && portset_locked());
    portset_context(0);
    CHECK(!portset_locked() && portset_event(EV_OK) == MA_CHANGED);
}

int main(void)
{
    t_values(); t_select(); t_edit(); t_idle(); t_locked();
    return check_report("portset");
}
