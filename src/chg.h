/*
 * Per-port charge controller.  Pure integer logic: no hardware, time = number of control passes (4.5 ms).
 *
 * Thresholds are in port-voltage counts (1 count = 4.5 V/4096) and current counts (1/68.25 A), and are named after
 * what they mean.
 *
 * Per port the state is:
 *   IDLE    no cell (V < 1.00 V) or not running                         duty 0, switch low
 *   PROBE   output off, settle, classify the cell                        duty 0, switch low
 *   CHARGE  constant current with soft ramp; ends in the rest test: after the tail (full voltages), at the step-down floor (storage),
 *           or with no current at all for 2 s (a pulled cell)
 *   REST    output off for ~1 s, then the rest voltage decides like a probe (an empty port reads below 1 V and is released)
 *   FULL    cell inside the target window; watched: below it for 2 s = test again, above it = bleed
 *   BLEED   cell above the target: switch HIGH (discharge path) down to the target, then the rest test
 *   FAULT   over-voltage / over-current latched; cleared only by removing the cell or stopping
 */
#ifndef CHG_H
#define CHG_H
#include "compat.h"
#include "board.h"
#include "sup.h"

enum { PS_IDLE = 0, PS_PROBE, PS_CHARGE, PS_REST, PS_FULL, PS_BLEED, PS_FAULT };

#define CAP_PARK  1           /* chg_in_t.cap value: hold the port's output off (it keeps its state and timers) */

/* events (one-shot, per port) for the UI / buzzer */
enum { CE_NONE = 0, CE_FULL = 1, CE_FAULT = 2 };

typedef struct {
    uint16_t adc_v[NPORTS];      /* raw port voltage ADC (ADC1..6) */
    uint16_t adc_i[NPORTS];      /* raw port current ADC (12,14,13,9,10,11) */
    uint16_t rail;               /* filtered rail ADC8 */
    uint16_t tgt;                /* target voltage T (port-voltage counts) */
    uint16_t setpoint;           /* rail setpoint S of that voltage */
    uint8_t  volt;               /* the target voltage: 0..3, sup.h VOLT_* */
    uint8_t  iset[NPORTS];       /* per-port current setting (counts) */
    uint8_t  run;                /* charging requested */
    uint8_t  en[NPORTS];         /* the port takes part in the run (0: it stays idle, its output off) */
    uint8_t  cap[NPORTS];        /* per-port current cap from the user limits (limits.h): 0 = none, CAP_PARK = output off, else counts */
} chg_in_t;

typedef struct {
    uint16_t duty[NPORTS];       /* PWM compare, 0..PWM_PERIOD */
    uint8_t  sw[NPORTS];         /* switch pin: 0 = charge path, 1 = bleed path */
    uint8_t  state[NPORTS];
    uint8_t  event[NPORTS];      /* CE_* raised on the pass it happens */
    uint16_t v[NPORTS];          /* filtered voltage (counts) */
    uint16_t i[NPORTS];          /* filtered current (counts) */
    uint8_t  any_active;         /* some port is charging or bleeding (global enable P5.2 must be on) */
    uint8_t  want[NPORTS];       /* CHARGE: the working current before the caps (counts), else 0 */
    uint8_t  set[NPORTS];        /* CHARGE: the current regulated to, after the caps (counts, 0 = parked), else 0 */
} chg_out_t;

void chg_init(void);
void chg_pass(const chg_in_t XD *in, chg_out_t XD *out);
void chg_abort(void);            /* drop everything to the idle state (stop) */
void chg_cut(void);              /* rail trip: duties to zero and the soft ramp restarts, port states and timers are kept */

/* thresholds, in counts, exposed for tests and documentation */
#define V_DEAD            910    /* 1.00 V: below this there is no cell */
#define V_FAULT           4004   /* 4.40 V: over-voltage latch while bleeding */
#define I_TAIL            5      /* tail-current threshold (73 mA) */
#define PASS_TICKS        9      /* control pass: 9 x 0.5 ms = 4.5 ms */

#endif
