/*
 * Port settings mode: each port's own charge current, or OFF, set on the main screen.  Pure logic (no hardware), like menu.c.
 *
 * Entered with a long press on B2 (the right button) on the main screen.  B1 is the left button, B2 the right one.
 *   select  B1: previous port   B2: next port (wraps; taps only)   OK: edit that port   BACK or 10 s idle: leave (saves)
 *   edit    B1: value down   B2: value up (holding repeats)   OK: confirm   BACK or 10 s idle: cancel (the old value comes back)
 * While charging nothing can be edited: the values are shown, OK only reports MA_LOCKED.
 *
 * A port's value, as one byte (also the USB encoding): PV_OFF, PV_AUTO (the global current), else a current in iu, 21..91 in
 * 0.1 A steps.  Stored in settings.ch_off / settings.ch_cur (cfg.h).
 */
#ifndef PORTSET_H
#define PORTSET_H
#include "compat.h"

#define PV_OFF    0
#define PV_AUTO   1
#define PORTSET_IDLE    2222        /* 10 s of control passes (4.5 ms) without a key: the mode closes */

void    portset_open(uint8_t run);    /* run: charging, which locks the values */
void    portset_context(uint8_t run); /* refresh run while the mode is open; an edit in progress is cancelled when charging starts */
uint8_t portset_event(uint8_t ev);    /* an input.h event; returns a menu.h MA_* (MA_EXIT: close the mode) */
uint8_t portset_pass(void);           /* once per control pass: MA_EXIT after PORTSET_IDLE without a key */
uint8_t portset_port(void);           /* the selected port, 0..5 */
uint8_t portset_editing(void);
uint8_t portset_locked(void);

uint8_t portset_get(uint8_t k);       /* port k's value from the settings */
uint8_t portset_valid(uint8_t v);     /* v is a value a port can take */
void    portset_put(uint8_t k, uint8_t v);   /* store a valid value */
uint8_t portset_step(uint8_t v, uint8_t up); /* the next value up / down: OFF < AUTO < 0.3 A ... 1.3 A, no wrap */

#endif
