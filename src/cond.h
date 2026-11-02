/* The screens of the conditions (sup.h): a fatal one (SF_*, red) or one that waits to clear (STOP_*, amber), and the rail self-test's retry. */
#ifndef COND_H
#define COND_H
#include "compat.h"

#define C_TESTING 8      /* the waiting condition between two tries of the rail self-test (with STOP_VIN .. STOP_RAIL, SF_*) */

void    cond_test_readings(uint16_t first, uint16_t second);   /* the self-test's last two rail readings (vu), shown by SF_TEST and C_TESTING */
void    cond_show(uint8_t c);       /* draws the screen of c once; then keeps its number up to date, about four times a second */
void    cond_forget(void);          /* another screen has taken over: c is drawn afresh the next time */
uint8_t cond_shown(void);           /* a condition's screen is the one on the display */

#endif
