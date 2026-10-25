/* Host tests for the settings menu logic.  make test
 * B1 = the left button, B2 = the right one.  List: left short up, right short down, right long edit / run.
 * Edit: right short +, left short -, right long confirm, left long cancel. */
#include <stdio.h>
#include <string.h>
#include "menu.h"
#include "input.h"
#include "cal.h"
#include "limits.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define LEFT_SHORT   EV_B1_SHORT
#define LEFT_LONG    EV_B1_LONG
#define RIGHT_SHORT  EV_B2_SHORT
#define RIGHT_LONG   EV_B2_LONG

static void fresh(uint8_t run)
{
    menu_v.mode = 0; menu_v.cur = 21; menu_v.sound = 1; menu_v.ilim = 0; menu_v.vmin = 0; menu_v.vcut = 0; menu_v.lmode = LM_EQUAL;
    menu_open(run, CUR_MAX);
}
static void down(int n) { while (n--) menu_event(RIGHT_SHORT); }
static void more(int n) { while (n--) menu_event(RIGHT_SHORT); }       /* while editing */
static void less(int n) { while (n--) menu_event(LEFT_SHORT); }
static void edit(void) { menu_event(RIGHT_LONG); }                      /* enter the edit mode (or run the action) */
static void confirm(void) { menu_event(RIGHT_LONG); }
static void cancel(void) { menu_event(LEFT_LONG); }
static int has(const char *row, const char *s) { return strstr(row, s) != 0; }

static void t_nav(void)
{
    int k;
    fresh(0);
    CHECK(has(menu_text(0), "MENU") && has(menu_text(0), "1/9"));
    menu_event(LEFT_SHORT);                                   /* up from the first item: stays (no wrap) */
    CHECK(has(menu_text(0), "1/9") && menu_text(1)[0] == '>');
    down(20);                                                 /* down past the last item: stays on EXIT & SAVE */
    CHECK(has(menu_text(0), "9/9"));
    for (k = 1; k <= 8; k++) if (has(menu_text(k), "EXIT")) break;
    CHECK(k == 8 && menu_text(k)[0] == '>');
    CHECK(!has(menu_text(1), "MODE") && has(menu_text(1), "CURRENT"));          /* the list scrolled by one */
    CHECK(menu_event(RIGHT_LONG) == MA_EXIT);
    menu_event(LEFT_SHORT); CHECK(has(menu_text(0), "8/9"));  /* left goes back up */
    fresh(0); down(7);
    CHECK(menu_event(RIGHT_LONG) == MA_DIAG);
    fresh(0);
    CHECK(menu_event(LEFT_LONG) == MA_NONE && !menu_editing()); /* left long does nothing in the list */
}

static void t_edit(void)
{
    fresh(0);
    CHECK(menu_event(RIGHT_LONG) == MA_CHANGED && menu_editing());                 /* edit MODE */
    more(2);
    CHECK(menu_v.mode == 2 && has(menu_text(1), "3.80V"));                         /* live */
    less(1); CHECK(menu_v.mode == 1);
    cancel();                                                                      /* cancel: back to what it was */
    CHECK(menu_v.mode == 0 && !menu_editing());
    edit(); more(1); confirm();                                                    /* confirm */
    CHECK(menu_v.mode == 1 && !menu_editing());
    edit(); less(3);
    CHECK(menu_v.mode == 0);                                                       /* values stop at the ends */
    cancel();

    /* current: steps of 0.1 A within [0.3 A, the cap] */
    fresh(0); menu_context(0, 35); down(1);
    edit();
    less(1); CHECK(menu_v.cur == 21);
    more(4);
    CHECK(menu_v.cur == 35);                                                       /* 0.5 A cap */
    confirm();
}

static void t_locked(void)
{
    fresh(1);
    CHECK(menu_event(RIGHT_LONG) == MA_LOCKED && !menu_editing());                 /* mode */
    CHECK(has(menu_text(9), "LOCKED"));
    down(1); CHECK(menu_event(RIGHT_LONG) == MA_LOCKED);                           /* current */
    down(1); CHECK(menu_event(RIGHT_LONG) == MA_CHANGED && menu_editing());        /* sound can change while charging */
    less(1); CHECK(menu_v.sound == 0 && has(menu_text(3), "OFF"));
    confirm();
    down(1);                                                                       /* limits can change while charging */
    CHECK(menu_event(RIGHT_LONG) == MA_CHANGED);
    more(1); CHECK(menu_v.ilim == 5);
    CHECK(has(menu_text(4), "0.5A"));
    confirm();
}

static void t_limits(void)
{
    int n;
    fresh(0); down(3);                                                             /* MAX IN AMPS */
    edit();
    more(200);
    CHECK(menu_v.ilim == ILIM_MAX && has(menu_text(4), "10.0A"));
    less(1); CHECK(menu_v.ilim == 95 && has(menu_text(4), "9.5A"));
    confirm();
    down(1); edit();                                                               /* MIN IN VOLTS */
    more(1); CHECK(menu_v.vmin == VMIN_MIN && has(menu_text(5), "4.8V"));
    more(4);
    CHECK(has(menu_text(5), "5.2V"));
    cancel();
    CHECK(menu_v.vmin == 0);
    edit();                                                                        /* 0.1 V steps to 6.0 V, then 0.5 V */
    more(13);
    CHECK(menu_v.vmin == 60 && has(menu_text(5), "6.0V"));
    more(1); CHECK(menu_v.vmin == 65);
    less(2); CHECK(menu_v.vmin == 59);
    more(300);
    CHECK(menu_v.vmin == VMIN_MAX && has(menu_text(5), "25.0V"));
    cancel();
    down(1); edit();                                                               /* BELOW MIN: limit smoothly or stop the run */
    CHECK(menu_v.vcut == 0 && has(menu_text(6), "LIMIT"));
    more(1); CHECK(menu_v.vcut == 1 && has(menu_text(6), "STOP RUN"));
    more(1); CHECK(menu_v.vcut == 1);                                              /* two values only */
    less(1); CHECK(menu_v.vcut == 0);
    confirm();
    down(1); edit();                                                               /* REGULATION, one setting for both limits */
    for (n = 0; n < LM_NMODES + 3; n++) more(1);
    CHECK(menu_v.lmode == LM_BY_VHIGH && has(menu_text(7), "HIGH V FIRST"));
    less(3); CHECK(menu_v.lmode == LM_EQUAL && has(menu_text(7), "EQUAL"));
    confirm();
}

static void t_rows(void)
{
    int page, k;
    fresh(0);
    for (page = 0; page < 9; page++) {
        for (k = 0; k < MENU_ROWS; k++) {
            const char *t = menu_text((uint8_t)k);
            int n = 0, bad = 0;
            while (t[n]) { if (t[n] < 32 || t[n] > 126) bad = 1; n++; }
            CHECK(n == ROW_COLS && !bad);
        }
        down(1);
    }
    fresh(0);
    CHECK(has(menu_text(9), "< UP") && has(menu_text(9), "DOWN >") && has(menu_text(9), "HOLD > EDIT"));      /* cues in the corners */
    CHECK(menu_text(9)[0] == '<' && menu_text(9)[ROW_COLS - 1] == '>');
    edit();
    CHECK(has(menu_text(9), "< -") && has(menu_text(9), "+ >") && has(menu_text(9), "<HOLD X") && has(menu_text(9), "HOLD> OK") && menu_text(1)[0] == '*');
    cancel();
    fresh(0); down(7);
    CHECK(has(menu_text(9), "HOLD > OPEN"));
    down(1);
    CHECK(has(menu_text(9), "HOLD > SAVE"));
}

int main(void)
{
    t_nav(); t_edit(); t_locked(); t_limits(); t_rows();
    printf("menu: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
