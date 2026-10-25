/*
 * Settings menu: pure logic (no hardware).  Opened with the B1+B2 chord, navigated with the two buttons:
 *
 * B1 is the left button and B2 the right one: right is forward (down, more, confirm), left is back (up, less, cancel).
 *   list    left short: up   right short: down   (no wrap-around)   right long: edit the item / run the action
 *   edit    right short: value up   left short: value down   right long: confirm   left long: cancel (restores the old value)
 *
 * Values change live in menu_v; the app persists them when the menu is closed.  Mode and current cannot be changed while
 * charging (as on the main screen).  Rows are 26 characters, the same shape as the debug screens (rowbuf.h).
 */
#ifndef MENU_H
#define MENU_H
#include "compat.h"
#include "rowbuf.h"

#define MENU_ROWS   10                /* row 0 title, rows 1..8 items (scrolls), row 9 hint */

typedef struct {
    uint8_t mode, cur, sound, theme;
    uint8_t ilim, vmin, vcut, lmode;
} menu_vals_t;

extern menu_vals_t XD menu_v;

enum { MA_NONE = 0, MA_CHANGED, MA_LOCKED, MA_DIAG, MA_EXIT };

void        menu_open(uint8_t run, uint8_t cur_max);     /* menu_v already holds the current settings; cur_max: highest current allowed now */
void        menu_context(uint8_t run, uint8_t cur_max);  /* refresh run / cur_max while the menu is open */
uint8_t     menu_event(uint8_t ev);                      /* an input.h event; returns MA_* */
uint8_t     menu_editing(void);
const char *menu_text(uint8_t row);                      /* the text of one row, ROW_COLS characters, valid until the next call */

#endif
