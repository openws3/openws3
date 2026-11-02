#include "cond.h"
#include "ui.h"
#include "theme.h"
#include "hal.h"
#include "adc.h"
#include "cal.h"
#include "cfg.h"
#include "fmt.h"
#include "sup.h"
#include "dbgscr.h"

static char XD v1[8], v2[8];                          /* the numbers of the screen, in volts */
static uint16_t XD t1, t2;                            /* the self-test's last two readings */
static uint8_t XD shown, tick;                        /* the condition on the display (0 = none); the quarter second its number was drawn in */

void cond_test_readings(uint16_t first, uint16_t second) { t1 = first; t2 = second; }

static void numbers(uint8_t c)
{
    switch (c) {
    case SF_VCC_HIGH: case STOP_VCC: fmt_v2(v1, dbg_vcc_mv(adc_get(ADC_BANDGAP))); break;
    case STOP_VIN:                   fmt_vin(v1, cal_vin_mv(adc_get(ADC_VIN))); break;
    case SF_RAIL_OVER:               fmt_v2(v1, cal_port_mv(adc_raw(ADC_RAIL))); break;
    case SF_TEST: case C_TESTING:    fmt_v2(v1, cal_port_mv(t1)); fmt_v2(v2, cal_port_mv(t2)); break;
    default:                         fmt_v2(v1, cal_port_mv(adc_get(ADC_RAIL))); break;
    }
}

static void draw(uint8_t c)
{
    numbers(c);
    switch (c) {
    case SF_VCC_HIGH:  ui_message(R_BAD, "VCC HIGH", "OVER 5.0V, POWER CYCLE", v1, 0); break;
    case SF_RAIL_OVER: ui_message(R_BAD, "RAIL OVERVOLTAGE", "ADC FULL, POWER CYCLE", v1, 0); break;
    case SF_NO_RAIL:   ui_message(R_BAD, "NO RAIL", "NOT UP, POWER CYCLE", v1, 0); break;
    case SF_TEST:      ui_message(R_BAD, "RAIL TEST FAIL", "POWER CYCLE", v1, v2); break;
    case STOP_VIN:     ui_message(R_WAIT, "INPUT LOW", settings.usb_off ? "USB POWER OFF, USE XT60" : "CHECK THE SUPPLY", v1, 0); break;
    case STOP_VCC:     ui_message(R_WAIT, "VCC LOW", "TRY ANOTHER SUPPLY", v1, 0); break;
    case STOP_RAIL:    ui_message(R_WAIT, "RAIL OUT OF RANGE", "WAITING TO RECOVER", v1, 0); break;
    case C_TESTING:    ui_message(R_WAIT, "RAIL TEST", "FAILED, TRYING AGAIN", v1, v2); break;
    }
}

void cond_show(uint8_t c)
{
    uint8_t q = (uint8_t)(hal_ms() >> 8);
    if (shown != c) { draw(c); shown = c; }
    else if (c != C_TESTING && c != SF_TEST && q != tick) { tick = q; numbers(c); ui_message_big(v1); }     /* two numbers: they do not move */
}

void cond_forget(void) { shown = 0; }
uint8_t cond_shown(void) { return shown != 0; }
