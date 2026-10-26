/*
 * User limits: a maximum input current and a minimum input voltage.  Pure integer logic, called once per control pass.
 *
 * One number rules both: the power budget P (0.1 W units, at the input): the most the charging ports may draw together.
 *   current limit  P_i = limit x input voltage: worked out from scratch every update, no stored state
 *   voltage floor  P_v is stored: it shrinks while the input voltage is below the floor (a weak source recovers when it is loaded
 *                  less) and grows back slowly once the input is 0.1 V above the floor.  With vcut set the floor does not
 *                  regulate: the run is stopped instead (the caller shows why) after a second below it.
 *   P = the smaller of the two; the port current budget follows from it: P x 90 % / rail voltage
 * One setting, the regulation, decides how the budget is shared out over the charging ports:
 *   LM_EQUAL     every port gets the same share (a port that wants less frees the rest); if the share would be too small to
 *                drive, the highest-numbered ports are parked so that the others get a usable one
 *   LM_BY_CH     one by one: port 1 keeps its current longest, the highest port number is cut first
 *   LM_BY_VHIGH  one by one: the cell with the highest voltage keeps its current longest
 * Input current is estimated, not measured: rail voltage x the sum of the measured port currents / (90 % x input voltage).
 * The sharing is a pure function of the budget and the ports' want, voltage and measured current: higher-priority ports reserve
 * what they really draw (so a port in its taper makes room for the next one).  The result is a cap per port:
 * 0 = none, LIM_PARK = parked (output off), else a current in counts (1/68.25 A).
 */
#ifndef LIMITS_H
#define LIMITS_H
#include "compat.h"
#include "board.h"

enum { LM_EQUAL = 0, LM_BY_CH, LM_BY_VHIGH, LM_NMODES };
enum { LT_NONE = 0, LT_VOLTAGE };

#define LIM_PARK       1        /* cap value: the port is switched off until the budget allows its current again */
#define LIM_P_BIG      10000    /* budget value meaning "no limit" (1000 W) */
#define LIM_CAP_MIN    7        /* a share below this (0.1 A) is not worth driving: the port is parked instead */

/* settings ranges (stored in these units) */
#define ILIM_OFF       0
#define ILIM_MIN       5        /* 0.5 A  (0.1 A units) */
#define ILIM_KNEE      30       /* 0.1 A steps up to 3 A, 0.5 A above */
#define ILIM_MAX       100      /* 10 A */
#define VMIN_OFF       0
#define VMIN_MIN       96       /* 4.80 V  (0.05 V units) */
#define VMIN_FINE      100      /* 0.05 V steps up to 5.00 V, 0.1 V steps up to 6.00 V, 0.5 V above */
#define VMIN_COARSE    120
#define VMIN_MAX       250      /* 12.50 V */

typedef struct {
    uint8_t ilim;               /* input current limit, 0.1 A units, 0 = off */
    uint8_t vmin;               /* minimum input voltage, 0.05 V units, 0 = off */
    uint8_t vcut;               /* 1: below the floor stop the run; 0: limit the current smoothly */
    uint8_t mode;               /* LM_*: how the budget is shared */
} lim_cfg_t;

typedef struct {
    uint16_t rail, vin;                     /* filtered ADC counts */
    uint8_t  charging[NPORTS];              /* the port is in the CHARGE state */
    uint8_t  want[NPORTS];                  /* its working current before the limits, counts */
    uint16_t v[NPORTS], i[NPORTS];          /* filtered port voltage / current, counts */
} lim_in_t;

typedef struct {
    uint8_t  cap[NPORTS];                   /* 0 none, LIM_PARK, else counts */
    uint8_t  trip;                          /* LT_VOLTAGE: the input was below the floor for a second and vcut is set: stop the run */
    uint16_t est_ma;                        /* estimated input current */
    uint16_t budget;                        /* the power budget, 0.1 W (diagnostics); LIM_P_BIG = no limit */
    uint16_t p_cur, p_vol;                  /* the two parts of it: from the current limit / from the voltage floor (LIM_P_BIG = not active) */
    uint16_t bcnt;                          /* the budget as a total port current, counts; 0 = no limit active */
} lim_out_t;

/* The data goes through these globals, not parameters: direct RAM is full on this chip and function parameters live there. */
extern lim_cfg_t XD lim_cfg;                /* settings */
extern lim_in_t  XD lim_in;                 /* filled by the caller before lim_pass */
extern lim_out_t XD lim_out;                /* the result */

void lim_reset(void);                       /* at the start of a run, and whenever nothing is charging */
void lim_pass(void);

/* the values the menu steps through: 0 (off), then min .. knee in 0.1 steps, then 0.5 steps up to max; clamped, no wrap */
uint8_t vmin_step(uint8_t v, uint8_t up);        /* the same for MIN IN VOLTS: off, VMIN_MIN .. VMIN_MAX in the steps above */
uint8_t lim_step(uint8_t v, uint8_t up, uint8_t min, uint8_t knee, uint8_t max);
const char *lim_mode_name(uint8_t m);      /* REGULATION as shown: "EQUAL" ... (unknown -> "EQUAL") */

#endif
