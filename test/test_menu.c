/* Host tests for the settings menu logic.  make test
 * B1 = the left button, B2 = the right one.  List: B1 up, B2 down (wraps), OK edit / run, BACK close.
 * Edit: B2 +, B1 -, OK confirm, BACK cancel. */
#include <stdio.h>
#include <string.h>
#include "menu.h"
#include "input.h"
#include "cal.h"
#include "limits.h"
#include "theme.h"
#include "cfg.h"

#include "check.h"

#define LEFT_SHORT   EV_B1
#define RIGHT_SHORT  EV_B2
#define OK           EV_OK
#define BACK         EV_BACK

static void fresh(uint8_t run)
{
    settings.volt = 0; settings.cur = 21; settings.sound = 1; settings.theme = TH_CLASSIC; settings.ilim = 0; settings.vmin = 0; settings.vcut = 0; settings.lmode = LM_EQUAL;
    menu_open(run, CUR_MAX);
}
static void down(int n) { while (n--) menu_event(RIGHT_SHORT); }
static void more(int n) { while (n--) menu_event(RIGHT_SHORT); }       /* while editing */
static void less(int n) { while (n--) menu_event(LEFT_SHORT); }
static void edit(void) { menu_event(OK); }                              /* enter the edit mode */
static void confirm(void) { menu_event(OK); }
static void cancel(void) { menu_event(BACK); }
static int has(const char *row, const char *s) { return strstr(row, s) != 0; }
/* from a fresh menu: open category `c` and put the cursor on its item `i` */
static void go(int c, int i) { down(c); menu_event(OK); down(i); }
enum { C_CHARGE, C_LIMITS, C_PREFS, C_DIAG };

static void t_nav(void)
{
    fresh(0);
    CHECK(has(menu_text(0), "MENU") && has(menu_text(0), "1/4") && has(menu_text(0), "HOLD:SAVE"));
    CHECK(has(menu_text(1), "CHARGING") && has(menu_text(2), "LIMITS") && has(menu_text(3), "PREFERENCES") && has(menu_text(4), "DIAGNOSTICS"));
    CHECK(menu_text(1)[0] == '>' && menu_text(2)[0] == ' ' && menu_text(5)[0] == ' ');
    menu_event(LEFT_SHORT);                                   /* up from the first category: wraps to the last */
    CHECK(has(menu_text(0), "4/4") && menu_text(4)[0] == '>');
    menu_event(RIGHT_SHORT); CHECK(has(menu_text(0), "1/4")); /* down from the last: back to the first */
    menu_event(EV_B2_REP); CHECK(has(menu_text(0), "2/4"));   /* a repeat moves like a tap */
    menu_event(EV_B1_REP); CHECK(has(menu_text(0), "1/4"));

    fresh(0);
    CHECK(menu_event(OK) == MA_CHANGED);                      /* open CHARGING */
    CHECK(has(menu_text(0), "CHARGING") && has(menu_text(0), "1/2") && has(menu_text(0), "HOLD:BACK"));
    CHECK(has(menu_text(1), "VOLTAGE") && has(menu_text(2), "CURRENT") && menu_text(3)[0] == 0x20 && !has(menu_text(3), "M"));
    menu_event(LEFT_SHORT); CHECK(has(menu_text(0), "2/2"));  /* wraps inside the category too */
    CHECK(menu_event(BACK) == MA_CHANGED);                    /* back to the categories, the cursor on CHARGING */
    CHECK(has(menu_text(0), "MENU") && has(menu_text(0), "1/4") && menu_text(1)[0] == '>');
    CHECK(menu_event(BACK) == MA_EXIT);                       /* BACK at the top closes the menu */

    fresh(0); go(C_LIMITS, 0);
    CHECK(has(menu_text(0), "LIMITS") && has(menu_text(0), "1/4"));
    CHECK(has(menu_text(1), "MAX IN AMPS") && has(menu_text(2), "MIN IN VOLTS") && has(menu_text(3), "BELOW MIN") && has(menu_text(4), "REGULATION"));
    fresh(0); go(C_PREFS, 0);
    CHECK(has(menu_text(1), "SOUND") && has(menu_text(2), "THEME") && has(menu_text(0), "1/2"));
    menu_event(BACK); CHECK(menu_text(3)[0] == '>');          /* the cursor returns to the category it came from */
    fresh(0); down(C_DIAG);
    CHECK(menu_event(OK) == MA_DIAG);
    fresh(0); down(C_DIAG); menu_event(OK); menu_event(BACK);
    CHECK(menu_event(BACK) == MA_EXIT);
}

static void t_edit(void)
{
    fresh(0); go(C_CHARGE, 0);
    CHECK(menu_event(OK) == MA_CHANGED && menu_editing());                         /* edit the target voltage */
    more(2);
    CHECK(settings.volt == 2 && has(menu_text(1), "3.80V"));                         /* live */
    less(1); CHECK(settings.volt == 1);
    cancel();                                                                      /* cancel: back to what it was, still in the category */
    CHECK(settings.volt == 0 && !menu_editing() && has(menu_text(0), "CHARGING"));
    edit(); more(1); confirm();                                                    /* confirm */
    CHECK(settings.volt == 1 && !menu_editing());
    edit(); less(3);
    CHECK(settings.volt == 0);                                                       /* values stop at the ends */
    cancel();

    /* current: steps of 0.1 A within [0.3 A, the cap] */
    fresh(0); menu_context(0, 35); go(C_CHARGE, 1);
    edit();
    less(1); CHECK(settings.cur == 21);
    more(4);
    CHECK(settings.cur == 35);                                                       /* 0.5 A cap */
    confirm();
}

static void t_locked(void)
{
    fresh(1); go(C_CHARGE, 0);
    CHECK(menu_event(OK) == MA_LOCKED && !menu_editing());                         /* voltage */
    CHECK(has(menu_text(9), "LOCKED"));
    down(1); CHECK(menu_event(OK) == MA_LOCKED);                                   /* current */
    menu_event(BACK);
    down(1); menu_event(OK);                                                       /* limits can change while charging */
    CHECK(menu_event(OK) == MA_CHANGED && menu_editing());
    more(1); CHECK(settings.ilim == 5);
    CHECK(has(menu_text(1), "0.5A"));
    confirm();
    menu_event(BACK); down(1); menu_event(OK);                                     /* sound and theme too */
    CHECK(menu_event(OK) == MA_CHANGED && menu_editing());
    less(1); CHECK(settings.sound == 0 && has(menu_text(1), "OFF"));
    confirm();
    down(1); CHECK(menu_event(OK) == MA_CHANGED);
    more(1); CHECK(settings.theme == TH_CALM && has(menu_text(2), "CALM"));
    confirm();
}

static void t_limits(void)
{
    int n;
    fresh(0); go(C_LIMITS, 0);                                                     /* MAX IN AMPS */
    edit();
    more(200);
    CHECK(settings.ilim == ILIM_MAX && has(menu_text(1), "10.0A"));
    less(1); CHECK(settings.ilim == 95 && has(menu_text(1), "9.5A"));
    confirm();
    down(1); edit();                                                               /* MIN IN VOLTS */
    more(1); CHECK(settings.vmin == VMIN_MIN && has(menu_text(2), "4.80V"));
    more(4);
    CHECK(has(menu_text(2), "5.00V"));
    cancel();
    CHECK(settings.vmin == 0);
    edit();                                                                        /* 0.05 V steps to 5.00 V, 0.1 V to 6.00 V, then 0.5 V */
    more(5);
    CHECK(settings.vmin == 100 && has(menu_text(2), "5.00V"));
    more(1); CHECK(settings.vmin == 102);
    more(9); CHECK(settings.vmin == 120 && has(menu_text(2), "6.00V"));
    more(1); CHECK(settings.vmin == 130);
    less(2); CHECK(settings.vmin == 118);
    more(300);
    CHECK(settings.vmin == VMIN_MAX && has(menu_text(2), "12.5V"));
    cancel();
    down(1); edit();                                                               /* BELOW MIN: limit smoothly or stop the run */
    CHECK(settings.vcut == 0 && has(menu_text(3), "LIMIT"));
    more(1); CHECK(settings.vcut == 1 && has(menu_text(3), "STOP RUN"));
    more(1); CHECK(settings.vcut == 1);                                              /* two values only */
    less(1); CHECK(settings.vcut == 0);
    confirm();
    down(1); edit();                                                               /* REGULATION, one setting for both limits */
    for (n = 0; n < LM_NMODES + 3; n++) more(1);
    CHECK(settings.lmode == LM_BY_VHIGH && has(menu_text(4), "HIGH V FIRST"));
    less(3); CHECK(settings.lmode == LM_EQUAL && has(menu_text(4), "EQUAL"));
    confirm();
}

static void t_theme(void)
{
    int n;
    fresh(0); go(C_PREFS, 1);                                                      /* THEME */
    CHECK(has(menu_text(2), "THEME") && has(menu_text(2), "CLASSIC"));
    edit();
    for (n = 0; n < TH_COUNT + 2; n++) more(1);
    CHECK(settings.theme == TH_COUNT - 1 && has(menu_text(2), "HIGH CONTRAST"));
    less(1); CHECK(settings.theme == TH_CONTRAST - 1);
    cancel(); CHECK(settings.theme == TH_CLASSIC);
}

static void check_rows(void)
{
    int k;
    for (k = 0; k < MENU_ROWS; k++) {
        const char *t = menu_text((uint8_t)k);
        int n = 0, bad = 0;
        while (t[n]) { if (t[n] < 32 || t[n] > 126) bad = 1; n++; }
        CHECK(n == ROW_COLS && !bad);
    }
}

static void t_rows(void)
{
    int c, i;
    fresh(0); check_rows();
    for (c = C_CHARGE; c <= C_PREFS; c++) {
        for (i = 0; i < 4; i++) { fresh(0); go(c, i); check_rows(); edit(); check_rows(); }
    }
    fresh(0);
    CHECK(has(menu_text(9), "< UP") && has(menu_text(9), "DOWN >") && has(menu_text(9), "BOTH = OPEN"));      /* cues in the corners */
    CHECK(menu_text(9)[0] == '<' && menu_text(9)[ROW_COLS - 1] == '>');
    go(C_CHARGE, 0);
    CHECK(has(menu_text(9), "BOTH = EDIT"));
    edit();
    CHECK(has(menu_text(9), "< -") && has(menu_text(9), "+ >") && has(menu_text(9), "BOTH OK HOLD:UNDO") && menu_text(1)[0] == '*');
    cancel();
}

int main(void)
{
    t_nav(); t_edit(); t_locked(); t_limits(); t_theme(); t_rows();
    return check_report("menu");
}
