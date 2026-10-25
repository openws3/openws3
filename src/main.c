/*
 * WhoopStor V3 firmware entry point: boot, then one control pass every PASS_TICKS.
 * Build with CHARGE=0 (make CHARGE=0) for a bench image that does everything except energise a port.
 */
#include "hal.h"
#include "app.h"
#include "input.h"
#include "buzz.h"
#include "usb.h"

/* 0.5 ms tick (20000 clocks at 40 MHz).  The vector must be compiled together with main(). */
void timer0_isr(void) __interrupt(1)
{
    hal_tick();
    input_tick(hal_btn(1), hal_btn(2));
    hal_tone(buzz_tick());
    hal_alarm(buzz_alarm_level());
}

/* USB device (interrupt 25).  Moves bytes between the SIE and two buffers only; see usb.h. */
void usb_vector(void) __interrupt(25)
{
    usb_isr();
}

void main(void)
{
    hal_init();               /* __sdcc_external_startup already drove the power path low */
    usb_init();
    app_boot();
    for (;;) app_pass();
}
