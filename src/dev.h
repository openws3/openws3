/*
 * Developer mode (USB commands 7 / 8 / 9, tools/devtool.py): the host sets every output directly (port PWMs and switch pins,
 * global enable, fan, input profile, the rail as a setpoint or a raw duty) and watches every input (telemetry page 3).
 * The main power enable is not offered: switching it off would cut the unit's own supply.
 *
 * Only the host that entered the mode drives it: command 7 carries a session byte that every command 8 must repeat.  Another
 * host can take over only once the owner has gone quiet (the watchdog has switched the outputs off).
 *
 * What still protects the hardware:
 *   - the host must repeat its command: 0.5 s without one switches every port output off (the rail goes back to its setpoint)
 *   - a port above 4.40 V or 2.8 A is switched off and stays off until the host asks for duty 0 and switch low on it
 *   - an input or VCC stop of the supervisor switches the port outputs off; a rail over-voltage is the usual fatal error
 *   - the fan also runs whenever the board is warm, whatever the host asks
 *   - both buttons held (BACK) leave the mode on the unit itself
 */
#ifndef DEV_H
#define DEV_H
#include "compat.h"

/* command 8 flags (byte 1), also reported as applied in telemetry page 3 */
enum { DF_GLOBAL = 1, DF_FAN = 2, DF_PROFILE = 4, DF_BEEP = 8, DF_RAIL_RAW = 16 };

void     dev_reset(uint16_t setpoint, int16_t rail_duty, uint8_t flags, uint8_t session);   /* entering: every port output off, the rail
                                     kept regulated at setpoint, flags (DF_PROFILE) as they are: the input path depends on it */
uint8_t  dev_taken(uint8_t session);                        /* another host owns the mode and is still sending */
uint8_t  dev_set(const uint8_t *c);                         /* a command 8 report (layout in USB.md); 0 = not the owner's, ignored */
int16_t  dev_pass(uint8_t supply_stop, uint8_t warm);       /* one control pass; returns the rail duty it applied */
uint16_t dev_setpoint(void);                                /* the rail setpoint in force (PI mode) */
void     dev_tele(uint8_t XD *b);                           /* telemetry page 3, bytes 8..62 */

#endif
