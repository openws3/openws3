#include "sup.h"
#include "cal.h"

#define VU(mv)           ((uint16_t)((mv) * 4096UL / 4500))     /* mV -> vu (1 vu = 4.5 V / 4096) */
#define RAIL_ABOVE_TARGET 14     /* the rail setpoint is 15 mV above the cell target */

static const CODE uint16_t TGT[NVOLTS] = { VU(4200), VU(4350), VU(3800), VU(3850) };

#define VCC_STOP         1108    /* bandgap count: VCC <= 4.4 V: an undervoltage, and the rail ADC (full scale = VCC) can no longer tell an over-voltage */
#define VCC_HIGH         976     /* bandgap count: VCC > 5.0 V (the chip takes 5.5 V) */
#define VCC_HIGH_PASSES  20      /* ... for this many passes in a row */
#define HOLD_PASSES      222     /* a waiting condition changes to a lesser one, or clears, only after this long (~1 s): no flicker on a falling supply */
#define RAIL_UP          3185    /* vu (3.5 V): lower than this with a good input the rail is not working */
#define NO_RAIL_PASSES   2200    /* ... for this many passes (10 s) */
#define RAIL_BAND_LO     35
#define RAIL_BAND_HI     36
#define RAIL_CUT_PASSES  50      /* net passes with the rail ADC at full scale (+1 at it, -1 below): 0.22 s held, short overshoots pass */
#define RAIL_ALARM_PASSES 250
#define RAIL_OK_BAND     30
#define ADC0_HOT         1300    /* board thermistor: the fan runs below this count (warm) ... */
#define ADC0_COOL        1451    /* ... until it is back above this */

uint16_t sup_target(uint8_t volt)   { return TGT[volt < NVOLTS ? volt : 0]; }
uint16_t sup_setpoint(uint8_t volt) { return sup_target(volt) + RAIL_ABOVE_TARGET; }
const char *sup_volt_name(uint8_t volt)
{
    static const char *const NAME[NVOLTS] = { "4.20V", "4.35V", "3.80V STOR", "3.85V STOR" };
    return NAME[volt < NVOLTS ? volt : 0];
}

static uint16_t div_cnt, nr_cnt;
static uint8_t  ov_cnt, vh_cnt, st_cur, st_cnt;
static uint8_t  rail_alarm, fan;

void sup_reset(void) { div_cnt = 0; nr_cnt = 0; ov_cnt = 0; vh_cnt = 0; st_cur = 0; st_cnt = 0; rail_alarm = 0; fan = 0; }

void sup_pass(const sup_in_t XD *in, sup_out_t XD *o)
{
    uint16_t S = in->setpoint;

    o->stop = STOP_NONE; o->fatal = SF_NONE;
    o->rail_up = in->rail >= RAIL_UP;

    /* ---- VCC, and the input voltage it follows down: the input is the cause, so it is said first ---- */
    if (in->bandgap >= VCC_STOP) o->stop = STOP_VCC;
    if (in->vin < VIN_MIN) o->stop = STOP_VIN;
    if (in->bandgap < VCC_HIGH) { if (vh_cnt < VCC_HIGH_PASSES) vh_cnt++; } else vh_cnt = 0;
    if (vh_cnt >= VCC_HIGH_PASSES) o->fatal = SF_VCC_HIGH;

    /* ---- rail ---- */
    if (in->rail + RAIL_BAND_LO < S || in->rail >= S + RAIL_BAND_HI) {
        if (div_cnt < 0xFFFF) div_cnt++;
        if (div_cnt >= RAIL_ALARM_PASSES) rail_alarm = 1;
    } else if (div_cnt) div_cnt--;
    /* over-voltage = the rail beyond what the ADC can measure (about 4.5 V); its own count, so time spent low (a sagging input)
       does not make one overshoot fatal */
    if (in->rail_sat && in->bandgap < VCC_STOP) { if (++ov_cnt >= RAIL_CUT_PASSES) { ov_cnt = RAIL_CUT_PASSES; o->fatal = SF_RAIL_OVER; } }
    else if (ov_cnt) ov_cnt--;
    if (rail_alarm && in->rail + RAIL_OK_BAND >= S && in->rail <= S + RAIL_OK_BAND) { rail_alarm = 0; div_cnt = 0; }
    if (rail_alarm && o->stop == STOP_NONE) o->stop = STOP_RAIL;   /* out of regulation for long; a low input or VCC is the cause, and is said first */
    /* never up although the input and VCC are fine: the buck stage or its sense is broken (a low input is the waiting STOP_VIN) */
    if (!o->rail_up && in->vin >= VIN_MIN && in->bandgap < VCC_STOP) { if (nr_cnt < NO_RAIL_PASSES) nr_cnt++; } else nr_cnt = 0;
    if (nr_cnt >= NO_RAIL_PASSES) o->fatal = SF_NO_RAIL;

    /* the condition shown: a more important one at once (the input, then VCC, then the rail), a lesser one or none only when it has stayed
       so for a while, so a supply that falls through the thresholds (VCC and the input cross them at different moments) does not flicker */
    {
        static const CODE uint8_t PRIO[4] = { 0, 3, 2, 1 };               /* by STOP_NONE, STOP_VIN, STOP_VCC, STOP_RAIL */
        if (PRIO[o->stop] > PRIO[st_cur]) { st_cur = o->stop; st_cnt = 0; }
        else if (o->stop == st_cur) st_cnt = 0;
        else if (++st_cnt >= HOLD_PASSES) { st_cur = o->stop; st_cnt = 0; }
        o->stop = st_cur;
    }

    /* ---- fan (P0.7): by the board temperature ---- */
    if (in->adc0 < ADC0_HOT) fan = 1;
    else if (in->adc0 >= ADC0_COOL) fan = 0;
    o->fan = fan;
}
