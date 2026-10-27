/*
 * ADC scanner.  Conversion time on the hardware is not measured yet, so a pass converts only what the control needs:
 *   fast (every pass):  6 port voltages, 6 port currents, the rail - 2 samples each
 *   slow (every 8th):   VIN, bandgap (VCC), ADC0 - 4 samples each
 *
 * Two kinds of value:
 *   counts    what the ADC returned (the mean of the samples): 0..4095, relative to VCC.  adc_raw().
 *   readings  the counts scaled by BG_NOM / bandgap: what the channel would read at the VCC the calibration was made at.  VCC sags
 *             with the input under load, and a conversion is relative to it, so counts alone would make every voltage drift with
 *             the supply.  A reading is in a fixed unit of the measured quantity instead:
 *               vu  4.5 V / 4096 = 1.0986 mV   port voltage and rail channels
 *               iu  1/68.25 A    = 14.65 mA    port current channels
 *             and cal.h for VIN.  The control works on readings.
 *             The bandgap and ADC0 (the thermistor, whose divider sits on VCC) are not scaled: their readings are counts.
 *   adc_now() is the latest reading, adc_get() a light filter on it: (old + new) / 2 per scan of that channel.
 */
#ifndef ADC_H
#define ADC_H
#include "compat.h"
#include "board.h"

void     adc_scan(void);              /* one pass worth of conversions (fast, plus slow when due) */
void     adc_scan_slow_now(void);
uint16_t adc_raw(uint8_t ch);         /* counts */
uint16_t adc_now(uint8_t ch);         /* the latest reading */
uint16_t adc_get(uint8_t ch);         /* the filtered reading */
uint8_t  adc_saturated(uint8_t ch);   /* the latest counts are at full scale: the input is beyond what the ADC measures */
void     adc_prime(void);             /* scan everything twice so filtered values start at the real readings */

#endif
