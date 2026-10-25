/*
 * ADC scanner.  Conversion time on the hardware is not measured yet, so a pass converts only what the control needs:
 *   fast (every pass):  6 port voltages, 6 port currents, the rail - 2 samples each
 *   slow (every 8th):   VIN, bandgap (VCC), ADC0 - 4 samples each
 * adc_raw() is the latest sample average; adc_get() is a light filter: (old + new) / 2 per scan of that channel.
 */
#ifndef ADC_H
#define ADC_H
#include "compat.h"
#include "board.h"

void     adc_scan(void);              /* one pass worth of conversions (fast, plus slow when due) */
void     adc_scan_slow_now(void);
uint16_t adc_raw(uint8_t ch);
uint16_t adc_get(uint8_t ch);
void     adc_prime(void);             /* scan everything twice so filtered values start at the real readings */

#endif
