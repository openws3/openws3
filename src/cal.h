/*
 * Calibration: ADC readings (adc.h, already corrected for VCC) -> engineering units.  Pure functions, no hardware.
 *
 * Status of each constant is marked.  Anything not [VERIFIED] should be checked on the hardware
 * before the number is trusted for a safety decision.
 */
#ifndef CAL_H
#define CAL_H
#include "compat.h"

/* Port voltage sense (ADC1..6) in mV: 1 vu = 4.5 V / 4096.  [VERIFIED: the 4.20 / 3.80 / 3.85 V targets sit at
 * 3822 / 3458 / 3504 on this scale.]  The hardware divider is still to be confirmed with a DMM. */
uint16_t cal_port_mv(uint16_t raw);

/* Port current sense (ADC 12,14,13,9,10,11) in mA: 1 iu = 1/68.25 A = 14.652 mA.  [VERIFIED: (raw+1)*4/273 A.  The regulator works on
 * this value, so the current setting is in iu too.] */
uint16_t cal_port_ma(uint16_t raw);
uint16_t cal_ma_to_port(uint16_t ma);              /* inverse, rounded down */

/* Input voltage in mV.  [UNVERIFIED] 19.7 V at raw 3000 fits
 * (raw+1)*4/61 in 0.1 V; i.e. ~26.9 V full scale.  Needs a bench sweep. */
uint16_t cal_vin_mv(uint16_t raw);

/* Current setting in iu: nominally 0.1 A = 7 iu (really 7/68.25 = 0.1026 A), range 21..91. */
#define CUR_UNITS_PER_100MA  7
#define CUR_MIN              21
#define CUR_MAX              91
uint16_t cal_set_to_ma(uint8_t units);          /* 21 -> 300 mA */
uint8_t  cal_cur_valid(uint8_t units);          /* a setting the steps can reach: 21, 28 ... 91 */
uint8_t  cal_ma_step_up(uint8_t units);         /* +0.1 A, wraps to CUR_MIN after CUR_MAX */

#endif
