/* Two push buttons -> taps, auto-repeat and a two-button OK / BACK gesture.  Pure logic: feed it the pin level every tick (0.5 ms). */
#ifndef INPUT_H
#define INPUT_H
#include "compat.h"

/* EV_B1 / EV_B2: one button tapped (reported on release).  EV_OK: both buttons pressed together and released.
 * EV_BACK: both held together for CHORD_LONG.  What holding a single button does depends on the style (input_style):
 * IS_NAV (menus): EV_B1_REP / EV_B2_REP, repeated while it stays down; IS_MAIN (main screen): EV_B1_LONG / EV_B2_LONG, once.
 * Either way a held button gives no tap on release. */
enum { EV_NONE = 0, EV_B1, EV_B2, EV_B1_REP, EV_B2_REP, EV_B1_LONG, EV_B2_LONG, EV_OK, EV_BACK };
enum { IS_NAV = 0, IS_MAIN };

/* thresholds in Timer0 ticks (0.5 ms) */
#define DEBOUNCE         10      /* shortest press that counts: 5 ms */
#define CHORD_WIN        400     /* the second button must follow the first within 200 ms to make a chord */
#define CHORD_LONG       1600    /* both held 0.8 s: EV_BACK */
#define REP_DELAY        800     /* held 0.4 s (longer than CHORD_WIN, so a chord never starts a repeat): first repeat */
#define REP_SLOW         300     /* interval of repeats 2..5: 150 ms */
#define REP_MID          120     /* repeats 6..13: 60 ms */
#define REP_FAST         50      /* then 25 ms */
#define B1_LONG          700     /* IS_MAIN: held 350 ms */
#define B2_LONG          1000    /* held 500 ms */

/* Once both buttons are down inside the chord window the pair owns the press until both are released: no tap or repeat
 * is produced for either button. */

void    input_reset(void);                              /* also back to IS_NAV */
void    input_style(uint8_t style);                      /* IS_NAV or IS_MAIN */
void    input_tick(uint8_t b1_down, uint8_t b2_down);   /* call every tick (0.5 ms) with the (inverted) pin levels */
uint8_t input_get(void);                                 /* pop one event, EV_NONE if none */
uint8_t input_held(uint8_t btn);                         /* 1 or 2: currently pressed */

#endif
