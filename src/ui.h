/* Screens.  Everything draws incrementally (the panel has no framebuffer and the control loop must not stall). */
#ifndef UI_H
#define UI_H
#include "compat.h"
#include "board.h"

typedef struct {
    uint8_t  run, volt, iset;
    uint16_t vin;                         /* filtered ADC7 */
    uint16_t tgt;                         /* target voltage, vu */
    uint8_t  state[NPORTS];               /* PS_* */
    uint16_t v[NPORTS], i[NPORTS];        /* filtered readings: vu / iu */
    uint8_t  alarm;                       /* STOP_* reason shown in the input-voltage box, 0 = none */
    uint8_t  usb_off;                     /* DISABLE USB POWER is on: the input-voltage box is green and says so now and then */
    uint8_t  parked[NPORTS];              /* the user limits hold this port with no current (it waits for room) */
    uint8_t  blink;                       /* toggles at ~1 Hz */
    uint8_t  set[NPORTS];                 /* each port's value (portset.h): PV_OFF, PV_AUTO, else its own current (iu) */
    uint8_t  want[NPORTS];                /* the current each port charges at, or would (iu) */
    uint8_t  tphase;                      /* 1: charging ports show their target instead of the measured current (1 s in 4) */
    uint8_t  mixed;                       /* some port has its own value or is off: the current box gets a marker */
    uint8_t  ps;                          /* 0: main screen; port settings mode: 1 selecting, 2 editing */
    uint8_t  sel, locked;                 /* port settings: the selected port; charging, nothing can be edited */
} ui_state_t;

void ui_splash(void);
/* full-screen message: the title (1x2, in the colour of role), a line under it (1x1) and up to two more lines (big 2x2, small 1x1); 0 = none */
void ui_message(uint8_t role, const char *title, const char *line2, const char *big, const char *small);
void ui_message_big(const char *big);     /* only the big line of the message on the screen, again (a number that changes) */
void ui_main_init(void);                  /* draws the frame; elements fill in over the next calls (also when ps changes) */
void ui_main_step(const ui_state_t *s);   /* refresh one element: the main screen, or with ps set the port settings mode on it */

/* text screens (debug pages, menu): the app composes the rows, this draws them, skipping rows that did not change */
void ui_text_enter(void);                /* clear the screen and forget what was shown */
void ui_text_row(uint8_t row, const char *text, uint16_t color);   /* text: ROW_COLS characters (rowbuf.h) */

#endif
