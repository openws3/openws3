/* Application: boot sequence and the 4.5 ms control pass.  Hardware-facing glue between the pure modules. */
#ifndef APP_H
#define APP_H
#include "compat.h"

void app_boot(void);      /* blocking: rail up, splash, self-test, input check; returns with the main screen drawn */
void app_pass(void);      /* one control pass; blocks until PASS_TICKS have elapsed since the previous one */

#endif
