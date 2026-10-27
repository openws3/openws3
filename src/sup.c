#include "sup.h"
#include "cal.h"

static const CODE uint16_t TGT[NVOLTS] = { 3822, 3958, 3458, 3504 };
static const CODE uint16_t SET[NVOLTS] = { 3836, 3972, 3803, 3940 };

#define VCC_STOP         1219    /* bandgap count: VCC <= 4.0 V */
#define RAIL_BAND_LO     35
#define RAIL_BAND_HI     36
#define RAIL_CUT_PASSES  50      /* net passes with the rail ADC at full scale (+1 at it, -1 below): 0.22 s held, short overshoots pass */
#define RAIL_ALARM_PASSES 250
#define RAIL_OK_BAND     30
#define ADC0_HOT         1300    /* board thermistor: the fan runs below this count (warm) ... */
#define ADC0_COOL        1451    /* ... until it is back above this */

uint16_t sup_target(uint8_t volt)   { return TGT[volt < NVOLTS ? volt : 0]; }
uint16_t sup_setpoint(uint8_t volt) { return SET[volt < NVOLTS ? volt : 0]; }
const char *sup_volt_name(uint8_t volt)
{
    static const char *const NAME[NVOLTS] = { "4.20V", "4.35V", "3.80V STOR", "3.85V STOR" };
    return NAME[volt < NVOLTS ? volt : 0];
}

static uint16_t div_cnt;
static uint8_t  ov_cnt;
static uint8_t  rail_alarm, fan;

void sup_reset(void) { div_cnt = 0; ov_cnt = 0; rail_alarm = 0; fan = 0; }

void sup_pass(const sup_in_t XD *in, sup_out_t XD *o)
{
    uint16_t S = in->setpoint;

    o->stop = STOP_NONE; o->rail_cut = 0;

    /* ---- input voltage ---- */
    if (in->vin < VIN_MIN) o->stop = STOP_VIN;

    /* ---- VCC ---- */
    if (in->bandgap >= VCC_STOP) o->stop = STOP_VCC;

    /* ---- rail ---- */
    if (in->rail + RAIL_BAND_LO < S || in->rail >= S + RAIL_BAND_HI) {
        if (div_cnt < 0xFFFF) div_cnt++;
        if (div_cnt >= RAIL_ALARM_PASSES) rail_alarm = 1;
    } else if (div_cnt) div_cnt--;
    /* over-voltage = the rail beyond what the ADC can measure (about 4.5 V); its own count, so time spent low (a sagging input)
       does not make one overshoot fatal */
    if (in->rail_sat) { if (++ov_cnt >= RAIL_CUT_PASSES) { ov_cnt = RAIL_CUT_PASSES; o->rail_cut = 1; } }
    else if (ov_cnt) ov_cnt--;
    if (rail_alarm && in->rail + RAIL_OK_BAND >= S && in->rail <= S + RAIL_OK_BAND) { rail_alarm = 0; div_cnt = 0; }
    if (rail_alarm) o->stop = STOP_RAIL;                 /* out of regulation for long */

    /* ---- fan (P0.7): by the board temperature ---- */
    if (in->adc0 < ADC0_HOT) fan = 1;
    else if (in->adc0 >= ADC0_COOL) fan = 0;
    o->fan = fan;
}
