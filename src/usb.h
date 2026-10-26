/*
 * USB monitor interface: a full-speed HID device (VID 34bf, PID 1004, no driver needed on the host) with one 64-byte
 * IN report (telemetry, polled every 10 ms; two pages sent alternately, 20 ms each) and one 64-byte OUT report (commands).  See USB.md for the layout.
 *
 * The interrupt only moves bytes between the SIE and two buffers; commands are executed by the main loop (app.c), so the
 * power path is never touched from interrupt context.  The device works the same with or without a host: if USB is not
 * connected nothing happens.
 */
#ifndef USB_H
#define USB_H
#include "compat.h"

#define USB_REPORT 64

extern uint8_t XD usb_tx2[USB_REPORT];    /* the second page (limits and supervision), same head / tail rule */
extern uint8_t XD usb_tx[USB_REPORT];     /* telemetry, written by the app (head byte 2 / tail byte 63 = sequence, see tele_head in app.c) */

void    usb_init(void);
void    usb_isr(void);                    /* the vector is in main.c */
uint8_t usb_configured(void);             /* the host has selected the configuration */
uint8_t usb_cmd_get(uint8_t *cmd8);       /* copies the first 8 bytes of a pending command; 0 if none */

#endif
