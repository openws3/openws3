/*
 * Colour themes.  A theme only changes colours, never behaviour: every colour the UI uses is a role, and a theme is one table of
 * those roles.  Pure data, no hardware.  The role colours that carry meaning (run, good, bad, warn, info) keep their meaning in
 * every theme; the others (text, values, badges) are decoration.
 */
#ifndef THEME_H
#define THEME_H
#include "compat.h"

enum { TH_CLASSIC = 0, TH_CALM, TH_AMBER, TH_CONTRAST, TH_COUNT };

/* roles: the first 11 are the base palette, then the six port badges and the eight per-state dots (index = PS_*) */
enum { R_BG = 0, R_BOX, R_TEXT, R_VIN, R_VOLT, R_DIM, R_RUN, R_WARN, R_GOOD, R_BAD, R_INFO, R_BADGE0, R_DOT0 = R_BADGE0 + 6, R_COUNT = R_DOT0 + 8 };

void     th_set(uint8_t theme);          /* out of range -> classic */
uint8_t  th_get(void);
uint16_t th(uint8_t role);               /* RGB565 of a role in the active theme */
const char *th_name(uint8_t theme);      /* up to 13 characters */

#endif
