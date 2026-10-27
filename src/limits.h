/*
 * User limits: a maximum input current and a minimum input voltage.  Pure integer logic, called once per control pass.
 *
 * One number rules both: the power budget P (0.1 W units, at the input): the most the charging ports may draw together.
 *   current limit  P_i = limit x input voltage: worked out from scratch every update, no stored state
 *   voltage floor  P_v is stored: it shrinks while the input voltage is below the floor (a weak source recovers when it is loaded
 *                  less) and grows back slowly once the input is 0.1 V above the floor.  With vcut set the floor does not
 *                  regulate: the run is stopped instead (the caller shows why) after a second below it.
 *   P = the smaller of the two; the port current budget follows from it: P x 90 % / rail voltage
 * The ports may only grow into the budget, never past it.  Every pass (the gate):
 *   - a port that wants more current (below its setting, duty not at the top) may raise its duty, by at most one count, only while
 *     the measured port currents leave LIM_ROOM per rising port under the budget; the room goes to the ports in priority order
 *   - above the budget, the lowest-priority port that draws current is brought down by about the excess
 *   - a port that wants more but gets no room takes it from a lower-priority port the same way
 * Budget is only handed out once it is free as measured, so a rising port cannot race a slow falling one.
 * One setting, the regulation, is the priority order:
 *   LM_EQUAL     the port with the lowest current first: the currents even out (a port that wants less leaves the rest)
 *   LM_BY_CH     port 1 first, the highest port number is cut first
 *   LM_BY_VHIGH  the cell with the highest voltage first
 * Input current is estimated, not measured: rail voltage x the sum of the measured port currents / (90 % x input voltage).
 */
#ifndef LIMITS_H
#define LIMITS_H
#include "compat.h"
#include "chg.h"

enum { LM_EQUAL = 0, LM_BY_CH, LM_BY_VHIGH, LM_NMODES };
enum { LT_NONE = 0, LT_VOLTAGE };

#define LIM_P_BIG      10000    /* budget value meaning "no limit" (1000 W) */
#define LIM_ROOM       3        /* iu (44 mA) under the budget per port that may rise this pass: what one duty count adds, with margin */

/* settings ranges (stored in these units); the steps between are in limits.c */
#define ILIM_OFF       0
#define ILIM_MIN       5        /* 0.5 A  (0.1 A units) */
#define ILIM_MAX       100      /* 10 A */
#define VMIN_OFF       0
#define VMIN_MIN       96       /* 4.80 V: 0.05 V per unit up to VMIN_KNEE, 0.5 V per unit above (one byte reaches big packs) */
#define VMIN_KNEE      200      /* 10.0 V */
#define VMIN_MAX       230      /* 25.0 V: 6S at 3.3 V per cell fits, and the input ADC reads to about 26.9 V */
#define VMIN_MV(v)     ((v) <= VMIN_KNEE ? (uint32_t)(v) * 50u : 10000u + ((uint32_t)(v) - VMIN_KNEE) * 500u)

typedef struct {
    uint8_t ilim;               /* input current limit, 0.1 A units, 0 = off */
    uint8_t vmin;               /* minimum input voltage, 0.05 V units, 0 = off */
    uint8_t vcut;               /* 1: below the floor stop the run; 0: limit the current smoothly */
    uint8_t mode;               /* LM_*: how the budget is shared */
} lim_cfg_t;

typedef struct {
    uint16_t rail, vin;                     /* filtered readings (adc.h) */
    uint8_t  charging[NPORTS];              /* the port is in the CHARGE state */
    uint8_t  want[NPORTS];                  /* its working current before the limits, iu */
    uint16_t v[NPORTS], i[NPORTS];          /* filtered port voltage (vu) / current (iu) */
    uint8_t  up[NPORTS];                    /* the port wants more current (chg_out_t.up) */
} lim_in_t;

typedef struct {
    uint8_t  on;                            /* a limit is in force: the gate below applies */
    uint8_t  rise;                          /* bit k: port k may raise its duty this pass */
    uint8_t  cut;                           /* the port to bring down this pass, NPORTS = none ... */
    uint8_t  excess;                        /* ... by about this much current, iu */
    uint8_t  trip;                          /* LT_VOLTAGE: the input was below the floor for a second and vcut is set: stop the run */
    uint16_t est_ma;                        /* estimated input current */
    uint16_t budget;                        /* the power budget, 0.1 W (diagnostics); LIM_P_BIG = no limit */
    uint16_t p_cur, p_vol;                  /* the two parts of it: from the current limit / from the voltage floor (LIM_P_BIG = not active) */
    uint16_t bcnt;                          /* the budget as a total port current, iu; 0 = no limit active */
} lim_out_t;

/* The data goes through these globals, not parameters: direct RAM is full on this chip and function parameters live there. */
extern lim_cfg_t XD lim_cfg;                /* settings */
extern lim_in_t  XD lim_in;                 /* filled by the caller before lim_pass */
extern lim_out_t XD lim_out;                /* the result */

void lim_reset(void);                       /* at the start of a run, and whenever nothing is charging */
void lim_pass(void);                        /* every control pass: the budget every ~100 ms, the gate every pass */

/* the values the menu steps through: 0 (off), then min .. max in steps that grow with the value; clamped, no wrap */
uint8_t ilim_step(uint8_t v, uint8_t up);        /* MAX IN AMPS: 0.5 .. 3.0 A by 0.1 A, then by 0.5 A */
uint8_t vmin_step(uint8_t v, uint8_t up);        /* MIN IN VOLTS: 4.80 .. 5.00 V by 0.05 V, .. 6.00 V by 0.1 V, then by 0.5 V to 25 V */
const char *lim_mode_name(uint8_t m);      /* REGULATION as shown: "EQUAL" ... (unknown -> "EQUAL") */

#endif
