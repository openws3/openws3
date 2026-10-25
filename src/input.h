/* Two push buttons -> short/long events.  Pure logic: feed it the pin level every tick (0.5 ms). */
#ifndef INPUT_H
#define INPUT_H
#include "compat.h"

enum { EV_NONE = 0, EV_B1_SHORT, EV_B1_LONG, EV_B2_SHORT, EV_B2_LONG, EV_CHORD };

/* thresholds in Timer0 ticks (0.5 ms): B1 short 5..699 (2.5 ms .. 350 ms), long >= 700; B2 short 10..999 (5 ms .. 500 ms), long >= 1000 */
#define B1_SHORT_MIN     5
#define B1_LONG          700
#define B2_SHORT_MIN     10
#define B2_LONG          1000

/* Both buttons pressed together: if the second goes down before either long press has fired (B1 350 ms, B2 500 ms, so "together"
 * means within ~300 ms), the pair becomes a chord: no short/long event is produced for either button, and EV_CHORD fires once
 * when both have been held for CHORD_TICKS.  A press that is not a chord behaves exactly as before. */
#define CHORD_TICKS      3000    /* 1.5 s */

void    input_reset(void);
void    input_tick(uint8_t b1_down, uint8_t b2_down);   /* call every tick (0.5 ms) with the (inverted) pin levels */
uint8_t input_get(void);                                 /* pop one event, EV_NONE if none */
uint8_t input_held(uint8_t btn);                         /* 1 or 2: currently pressed */

#endif
