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
    uint8_t  parked[NPORTS];              /* the user limits hold this port's output off */
    uint8_t  blink;                       /* toggles at ~1 Hz */
} ui_state_t;

void ui_splash(void);
/* full-screen message: two text lines (1x2 / 1x1) and up to two numbers appended on lines 3/4 */
void ui_message(const char *title, const char *line2, uint8_t has_n1, uint16_t n1, uint8_t has_n2, uint16_t n2);
void ui_main_init(void);                  /* draws the frame; elements fill in over the next calls */
void ui_main_step(const ui_state_t *s);   /* refresh one element */

/* text screens (debug pages, menu): the app composes the rows, this draws them, skipping rows that did not change */
void ui_text_enter(void);                /* clear the screen and forget what was shown */
void ui_text_row(uint8_t row, const char *text, uint16_t color);   /* text: ROW_COLS characters (rowbuf.h) */

#endif
