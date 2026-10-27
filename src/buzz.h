/*
 * Buzzer on P4.6: a 1 kHz square wave from the 0.5 ms Timer0 tick, in beep patterns.
 * The pattern engine is pure; buzz_tick() runs from the 0.5 ms interrupt.
 */
#ifndef BUZZ_H
#define BUZZ_H
#include "compat.h"

enum { PAT_NONE = 0, PAT_BOOT, PAT_KEY, PAT_DONE, PAT_ERROR, PAT_RAIL_ALARM };

void    buzz_reset(void);
void    buzz_enable(uint8_t on);            /* user setting: when off, tones are muted */
void    buzz_play(uint8_t pattern);         /* PAT_NONE stops */
uint8_t buzz_busy(void);

/* 0.5 ms tick: advances the pattern and returns the level for the tone pin (alternating while a tone segment is on) */
uint8_t buzz_tick(void);

#endif
