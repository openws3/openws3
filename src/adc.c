#include "adc.h"
#include "hal.h"
#include "ports.h"

#define SLOW_EVERY 8
#define BG_NOM     1071       /* bandgap count at the VCC the calibration was made at */

static uint16_t XD raw[NADC], filt[NADC];
static uint16_t XD gain = 4096;                 /* 4096 * BG_NOM / bandgap */
static uint8_t  XD slow_div;

/* The mean of 2^sh samples, and a first-order filter on it (12-bit values: the sums fit 16 bits).  A conversion counts
 * against VCC, which sags when the input does under load: every voltage then reads high.  So the readings are scaled by
 * the bandgap, back to what they would be at BG_NOM.  Not the bandgap itself, nor the thermistor (its divider sits on VCC). */
static void conv(uint8_t ch, uint8_t sh)
{
    uint16_t s = 0;
    uint8_t k;
    for (k = 0; k < (uint8_t)(1u << sh); k++) s += hal_adc(ch);
    s >>= sh;
    if (ch == ADC_BANDGAP) { if (s) gain = (uint16_t)(((uint32_t)BG_NOM << 12) / s); }
    else if (ch != ADC_IIN) s = (uint16_t)(((uint32_t)s * gain + 2048) >> 12);
    raw[ch] = s;
    filt[ch] = (filt[ch] + raw[ch] + 1) >> 1;
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
    adc_scan(); adc_scan_slow_now();
    for (ch = 0; ch < NADC; ch++) filt[ch] = raw[ch];
    adc_scan(); adc_scan_slow_now();
}

uint16_t adc_raw(uint8_t ch) { return raw[ch & 0x0F]; }
uint16_t adc_get(uint8_t ch) { return filt[ch & 0x0F]; }
