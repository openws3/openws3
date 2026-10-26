#include "sup.h"
#include "cal.h"

static const CODE uint16_t TGT[NVOLTS] = { 3822, 3958, 3458, 3504 };
static const CODE uint16_t SET[NVOLTS] = { 3836, 3972, 3803, 3940 };
/* index = cells-2 */
static const CODE uint16_t E1[5] = { 971, 1456, 1942, 2427, 2912 };
static const CODE uint16_t B3[5] = { 910, 1365, 1820, 2123, 2730 };

#define VCC_STOP         1219    /* bandgap count: VCC <= 4.0 V */
#define RAIL_BAND_LO     35
#define RAIL_BAND_HI     36
#define RAIL_OV          4023
#define RAIL_CUT_PASSES  50
#define RAIL_ALARM_PASSES 250
#define RAIL_OK_BAND     30
#define VIN_CONFIRM      3       /* passes below the Lipo limit before stopping (several samples, so one bad one does not stop the run) */
#define ADC0_HOT         1300    /* board thermistor: the fan runs below this count (warm) ... */
#define ADC0_COOL        1451    /* ... until it is back above this */

uint16_t sup_target(uint8_t volt)   { return TGT[volt & 3]; }
uint16_t sup_setpoint(uint8_t volt) { return SET[volt & 3]; }

uint8_t sup_cells(uint16_t vin)
{
    uint16_t n = vin / 663u + 1u;
    if (n < 2) n = 2;
    if (n > 6) n = 6;
    return (uint8_t)n;
}
uint16_t sup_lim_stop(uint8_t cells) { return E1[(cells < 2 ? 2 : cells > 6 ? 6 : cells) - 2]; }
uint16_t sup_lim_off(uint8_t cells)  { return B3[(cells < 2 ? 2 : cells > 6 ? 6 : cells) - 2]; }

static uint16_t div_cnt, lim_stop, lim_off;
static uint8_t  rail_alarm, vin_low, fan, cells_boot;

void sup_set_input(uint16_t v) { cells_boot = sup_cells(v); lim_stop = sup_lim_stop(cells_boot); lim_off = sup_lim_off(cells_boot); }
uint8_t sup_input_cells(void) { return cells_boot; }

void sup_reset(void) { div_cnt = 0; rail_alarm = 0; vin_low = 0; fan = 0; sup_set_input(0); }

void sup_pass(const sup_in_t XD *in, sup_out_t XD *o)
{
    uint16_t S = in->setpoint;

    o->stop = STOP_NONE; o->poweroff = 0; o->rail_cut = 0; o->rail_alarm = 0;

    /* ---- input voltage ---- */
    if (in->vin < VIN_MIN) o->stop = STOP_VIN;
    if (in->strap && !in->dc) {
        if (in->vin < lim_stop) { if (vin_low < 255) vin_low++; } else vin_low = 0;
        if (vin_low >= VIN_CONFIRM) o->stop = STOP_VIN;
        if (in->vin < lim_off && vin_low >= VIN_CONFIRM) o->poweroff = 1;
    } else vin_low = 0;

    /* ---- VCC ---- */
    if (in->bandgap >= VCC_STOP) o->stop = STOP_VCC;

    /* ---- rail ---- */
    if (in->rail + RAIL_BAND_LO < S || in->rail >= S + RAIL_BAND_HI) {
        if (div_cnt < 0xFFFF) div_cnt++;
        if (div_cnt >= RAIL_CUT_PASSES && in->rail >= RAIL_OV) o->rail_cut = 1;
        if (div_cnt >= RAIL_ALARM_PASSES) rail_alarm = 1;
    } else if (div_cnt) div_cnt--;
    if (rail_alarm && in->rail + RAIL_OK_BAND >= S && in->rail <= S + RAIL_OK_BAND) { rail_alarm = 0; div_cnt = 0; }
    if (rail_alarm) { o->rail_alarm = 1; o->stop = STOP_RAIL; }

    /* ---- fan (P0.7): by the board temperature ---- */
    if (in->adc0 < ADC0_HOT) fan = 1;
    else if (in->adc0 >= ADC0_COOL) fan = 0;
    o->fan = fan;
}
