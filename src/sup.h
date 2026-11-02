/*
 * System supervision: rail, input voltage, VCC, input-dependent current caps, alarms.  Pure logic, called once per pass.
 * Voltages are filtered ADC readings (adc.h: vu for the rail, cal.h for VIN); the bandgap and ADC0 are counts.
 */
#ifndef SUP_H
#define SUP_H
#include "compat.h"

/* Input gate: ~4.6 V, so a plain 5 V USB supply runs the unit.  How much the source can give is the user's call: the current
 * setting and the input limits (MAX IN AMPS, MIN IN VOLTS; from a LiPo pack, MIN IN VOLTS on STOP RUN is its low-voltage cut). */
#define VIN_MIN       700

/* the four target voltages the user chooses between (the last two are for storage): the port-voltage target T and the rail
 * setpoint S, in vu.  The rail is held just above the target and is what holds a cell there, see chg.c */
enum { VOLT_4_20 = 0, VOLT_4_35, VOLT_3_80, VOLT_3_85 };
#define NVOLTS 4
uint16_t sup_target(uint8_t volt);
uint16_t sup_setpoint(uint8_t volt);
const char *sup_volt_name(uint8_t volt);   /* "4.20V" ... "3.85V STOR" */

typedef struct {
    uint16_t vin, rail, bandgap, adc0;  /* filtered ADC7, ADC8, ADC15, ADC0 */
    uint16_t setpoint;                  /* rail setpoint S */
    uint8_t  rail_sat;                  /* the rail ADC is at full scale: the rail is beyond what it can measure */
} sup_in_t;

enum { STOP_NONE = 0, STOP_VIN, STOP_VCC, STOP_RAIL, STOP_USER, STOP_VLIM };   /* VLIM: the user's minimum input voltage (limits.h) */

/* Two kinds of condition.  WAITING: a supply condition, the unit does nothing until it clears by itself (STOP_VIN, STOP_VCC, STOP_RAIL
 * below).  FATAL: something is wrong with the unit or its supply, everything stays off until the power is cycled (SF_*). */
enum { SF_NONE = 0, SF_VCC_HIGH = 0x10, SF_RAIL_OVER, SF_NO_RAIL, SF_TEST };   /* SF_TEST (the rail self-test failed) is set by the app */

typedef struct {
    uint8_t  stop;          /* STOP_* : charging must stop now (all ports off, P5.2 low); STOP_RAIL also sounds the rail alarm */
    uint8_t  fatal;         /* SF_* : VCC over 5.0 V, the rail beyond the ADC (its full scale for 0.22 s net) or never up: everything off */
    uint8_t  rail_up;       /* the rail is at least 3.5 V: the rail self-test (app.c) may run */
    uint8_t  fan;           /* P0.7 level: the board is warm (ADC0, the thermistor) */
} sup_out_t;

void sup_reset(void);
void sup_pass(const sup_in_t XD *in, sup_out_t XD *out);

#endif
