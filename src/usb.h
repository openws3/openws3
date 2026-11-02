/*
 * USB monitor interface: a full-speed HID device (VID 34bf, PID 1004, no driver needed on the host) with one 64-byte
 * IN report (telemetry, polled every 10 ms; the pages are sent in turn, see arm_in) and one 64-byte OUT report (commands).  See USB.md for the layout.
 *
 * The interrupt only moves bytes between the SIE and two buffers; commands are executed by the main loop (app.c), so the
 * power path is never touched from interrupt context.  The device works the same with or without a host: if USB is not
 * connected nothing happens.
 */
#ifndef USB_H
#define USB_H
#include "compat.h"

#define USB_REPORT 64
#define USB_CMD    24                     /* bytes of a command report that are kept (the longest command, 8, uses 18) */

/* the telemetry pages the host reads; written by tele.c (head byte 2 / tail byte 63 = sequence) */
enum { UP_MAIN, UP_LIMITS, UP_CHARGE, UP_COUNT };     /* page 1; page 2 (page 3 in developer mode); page 4 */
extern uint8_t XD usb_page[UP_COUNT][USB_REPORT];

/* host commands, byte 0 of the OUT report (USB.md) */
enum { CMD_STOP = 1, CMD_START, CMD_SET_CURRENT, CMD_SET_VOLTAGE, CMD_PING, CMD_ISP, CMD_DEV_ENTER, CMD_DEV_SET, CMD_DEV_LEAVE, CMD_PORT_VALUE };

void    usb_init(void);
void    usb_isr(void);                    /* the vector is in main.c */
uint8_t usb_configured(void);             /* the host has selected the configuration */
uint8_t usb_cmd_get(uint8_t *cmd);        /* copies the first USB_CMD bytes of a pending command; 0 if none */

#endif
