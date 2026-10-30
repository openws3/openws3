#include "adc.h"
#include "hal.h"
#include "ports.h"

#define SLOW_EVERY 8
#define BG_NOM     1071       /* bandgap count at the VCC the input scale (cal.h) was made at */
#define FULL       4092       /* a raw mean this close to 4095 is at full scale: the input is beyond what the ADC measures */

static uint16_t XD raw[NADC], filt[NADC];       /* raw: counts as converted; filt: the filtered counts (adc.h) */
static uint16_t XD gain;                        /* 4096 * BG_NOM / bandgap (no initialiser: README build rule 1) */
static uint32_t XD tmp;                         /* the 32-bit products of adc_now and conv: external RAM, not direct */
static uint8_t  XD slow_div;

uint16_t adc_now(uint8_t ch)
{
    ch &= 0x0F;
    if (ch != ADC_VIN) return raw[ch];               /* only the input voltage is corrected (adc.h) */
    tmp = (uint32_t)raw[ch] * gain + 2048;
    return (uint16_t)(tmp >> 12);
}

/* the mean of 2^sh samples (12-bit values: the sums fit 16 bits), and a first-order filter on the reading made from it */
static void conv(uint8_t ch, uint8_t sh)
{
    uint16_t s = 0;
    uint8_t k;
    for (k = 0; k < (uint8_t)(1u << sh); k++) s += hal_adc(ch);
    raw[ch] = s >> sh;
    if (ch == ADC_BANDGAP && raw[ch]) {
        tmp = (uint32_t)BG_NOM << 12;
        gain = (uint16_t)(tmp / raw[ch]);
    }
    filt[ch] = (filt[ch] + adc_now(ch) + 1) >> 1;
}

void adc_scan_slow_now(void)
{
    conv(ADC_VIN, 2); conv(ADC_BANDGAP, 2); conv(ADC_IIN, 2);
}

void adc_scan(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) { conv(port_v_ch(k), 1); conv(port_i_ch(k), 1); }
    conv(ADC_RAIL, 1);
    if (++slow_div >= SLOW_EVERY) { slow_div = 0; adc_scan_slow_now(); }
}

void adc_prime(void)
{
    uint8_t ch;
    gain = 4096;
    adc_scan(); adc_scan_slow_now();
    for (ch = 0; ch < NADC; ch++) filt[ch] = adc_now(ch);
    adc_scan(); adc_scan_slow_now();
}

uint16_t adc_raw(uint8_t ch) { return raw[ch & 0x0F]; }
uint16_t adc_get(uint8_t ch) { return filt[ch & 0x0F]; }
uint8_t  adc_saturated(uint8_t ch) { return raw[ch & 0x0F] >= FULL; }
