/*
 * Per-port charge controller.  Pure integer logic: no hardware, time = number of control passes (4.5 ms).
 *
 * Thresholds are in the units of the ADC readings (adc.h): port voltage in 4.5 V/4096 (1.0986 mV), current in 1/68.25 A, and
 * are named after what they mean.
 *
 * Per port the state is:
 *   IDLE    no cell (V < 1.00 V) or not running                         duty 0, switch low
 *   PROBE   output off, settle, classify the cell                        duty 0, switch low
 *   CHARGE  constant current with soft ramp; ends in the rest test: after the tail (the rail sits just above the target, so it holds the
 *           cell there and the current tapers), or with no current at all for 2 s (a pulled cell)
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

/* events (one-shot, per port) for the UI / buzzer */
enum { CE_NONE = 0, CE_FULL = 1, CE_FAULT = 2 };

typedef struct {
    uint16_t v_now[NPORTS];      /* latest port voltage reading (adc.h: 4.5 V/4096 units) */
    uint16_t i_now[NPORTS];      /* latest port current reading (1/68.25 A units) */
    uint16_t rail;               /* filtered rail reading */
    uint16_t tgt;                /* target voltage T (vu) */
    uint16_t setpoint;           /* rail setpoint S of that voltage */
    uint8_t  iset[NPORTS];       /* per-port current setting (iu) */
    uint8_t  run;                /* charging requested */
    uint8_t  en[NPORTS];         /* the port takes part in the run (0: it stays idle, its output off) */
    /* the user limits' gate (limits.h) */
    uint8_t  lim;                /* a limit is in force: a duty rises at most one count per pass, and only on the ports in rise */
    uint8_t  rise;               /* bit k: port k may raise its duty this pass */
    uint8_t  cut;                /* the port to bring down this pass (NPORTS = none) ... */
    uint8_t  excess;             /* ... by about this much current (iu) */
} chg_in_t;

typedef struct {
    uint16_t duty[NPORTS];       /* PWM compare, 0..PWM_PERIOD */
    uint8_t  sw[NPORTS];         /* switch pin: 0 = charge path, 1 = bleed path */
    uint8_t  state[NPORTS];
    uint8_t  event[NPORTS];      /* CE_* raised on the pass it happens */
    uint16_t v[NPORTS];          /* filtered voltage (vu) */
    uint16_t i[NPORTS];          /* filtered current (iu) */
    uint8_t  any_active;         /* some port is charging or bleeding (global enable P5.2 must be on) */
    uint8_t  want[NPORTS];       /* CHARGE: the working current (iu), else 0 */
    uint8_t  up[NPORTS];         /* CHARGE, below the working current and the duty can still rise: about what one more count adds (iu,
                                    at least 1); else 0 */
    uint8_t  held[NPORTS];       /* CHARGE: the limits hold the port below its working current (now or within the last second):
                                    1, or 2 = and it draws nothing (waiting); else 0 */
    /* what each cell has had, since the run started or the cell was put in; it stays after a stop */
    uint32_t charge[NPORTS];     /* the raw current readings (iu) of the passes in CHARGE, summed: 54600 = 1 mAh */
    uint32_t bled[NPORTS];       /* the bleed load's share, estimated (its current cannot be sensed: the port voltage over 18 ohm):
                                    the voltage readings (vu / 4) of the passes in BLEED, summed: 3276800 = 1 mAh */
    uint16_t v_start[NPORTS];    /* the cell's voltage once the probe has settled (vu); 0 until then */
} chg_out_t;

void chg_init(void);
void chg_pass(const chg_in_t XD *in, chg_out_t XD *out);
void chg_abort(void);            /* drop everything to the idle state (stop) */
void chg_cut(void);              /* rail trip: duties to zero and the soft ramp restarts, port states and timers are kept */

/* thresholds, in vu / iu, exposed for tests and documentation */
#define V_DEAD            910    /* 1.00 V: below this there is no cell */
#define V_FAULT           4004   /* 4.40 V: over-voltage latch while bleeding */
#define I_TAIL            5      /* tail-current threshold (73 mA) */
#define PASS_TICKS        9      /* control pass: 9 x 0.5 ms = 4.5 ms */

#endif
