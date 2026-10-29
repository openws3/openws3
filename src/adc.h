/*
 * ADC scanner.  Conversion time on the hardware is not measured yet, so a pass converts only what the control needs:
 *   fast (every pass):  6 port voltages, 6 port currents, the rail - 2 samples each
 *   slow (every 8th):   VIN, bandgap (VCC), ADC0 - 4 samples each
 *
 * Values are the ADC counts as converted (the mean of the samples), 0..4095 relative to VCC.  A count is in a fixed unit of the measured
 * quantity:
 *               vu  4.5 V / 4096 = 1.0986 mV   port voltage and rail channels
 *               iu  1/68.25 A    = 14.65 mA    port current channels
 *             and cal.h for VIN.  The control works on these.  The thermistor (ADC0) divider sits on VCC.
 * The port, rail and current counts are not corrected for VCC: when it sags they read high, and the controller backs off, which is the
 * safe way.  VIN is corrected: VCC follows the input down in a brown-out, which would make the input read high just when it falls, and the
 * minimum-input limit (limits.h) would not trip.  Its count is scaled by base / bandgap, where base is the lowest bandgap count seen since
 * boot (the most VCC), so a chip whose reference is a little off still reads the input right at its normal supply.
 *   adc_now() is the latest value, adc_get() a light filter on it: (old + new) / 2 per scan of that channel.
 */
#ifndef ADC_H
#define ADC_H
#include "compat.h"
#include "board.h"

void     adc_scan(void);              /* one pass worth of conversions (fast, plus slow when due) */
void     adc_scan_slow_now(void);
uint16_t adc_raw(uint8_t ch);         /* the latest counts (same as adc_now) */
uint16_t adc_now(uint8_t ch);         /* the latest value */
uint16_t adc_get(uint8_t ch);         /* the filtered value */
uint8_t  adc_saturated(uint8_t ch);   /* the latest counts are at full scale: the input is beyond what the ADC measures */
void     adc_prime(void);             /* scan everything twice so filtered values start at the real readings */

#endif
