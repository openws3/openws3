#include "adc.h"
#include "hal.h"
#include "ports.h"

#define SLOW_EVERY 8

static uint16_t XD raw[NADC], filt[NADC];
static uint8_t  XD slow_div;

static void conv(uint8_t ch, uint8_t n)
{
    uint16_t s = 0;
    uint8_t k;
    for (k = 0; k < n; k++) s += hal_adc(ch);
    raw[ch] = (uint16_t)(s / n);
    filt[ch] = (uint16_t)(((uint32_t)filt[ch] + raw[ch] + 1) >> 1);
}

void adc_scan_slow_now(void)
{
    conv(ADC_VIN, 4); conv(ADC_BANDGAP, 4); conv(ADC_IIN, 4);
}

void adc_scan(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) { conv(port_v_ch(k), 2); conv(port_i_ch(k), 2); }
    conv(ADC_RAIL, 2);
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
