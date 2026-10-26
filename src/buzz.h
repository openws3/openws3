/*
 * Buzzer.  Two outputs:
 *   P4.6  tone (1 kHz square wave from the 0.5 ms Timer0 tick) for beep patterns
 *   P0.7  steady level: continuous alarm / start-up indication
 * The pattern engine is pure; buzz_tick() runs from the 0.5 ms interrupt.
 */
#ifndef BUZZ_H
#define BUZZ_H
#include "compat.h"

enum { PAT_NONE = 0, PAT_BOOT, PAT_KEY, PAT_DONE, PAT_ERROR, PAT_RAIL_ALARM };

void    buzz_reset(void);
void    buzz_enable(uint8_t on);            /* user setting: when off, tones are muted (the fan level P0.7 is not) */
void    buzz_play(uint8_t pattern);
void    buzz_stop(void);
void    buzz_alarm(uint8_t on);             /* P0.7 level */
uint8_t buzz_busy(void);

/* 0.5 ms tick: advances the pattern and returns the level for the tone pin (alternating while a tone segment is on) */
uint8_t buzz_tick(void);
uint8_t buzz_alarm_level(void);

#endif
