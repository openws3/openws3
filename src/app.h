/* Application: boot sequence and the 4.5 ms control pass.  Hardware-facing glue between the pure modules. */
#ifndef APP_H
#define APP_H
#include "compat.h"

/* the screens: 0 = main, 1..DBG_PAGES = a diagnostics page, then the modes drawn over the main screen */
#define SCR_PORTS  13     /* the port settings (portset.h) */
#define SCR_DEV    14     /* developer mode (dev.h) */
#define SCR_MENU   15     /* the menu (menu.h) */

void app_boot(void);      /* blocking: rail up, splash, self-test, input check; returns with the main screen drawn */
void app_pass(void);      /* one control pass; blocks until PASS_TICKS have elapsed since the previous one */

#endif
