#include "chg.h"
#include "ctl.h"

/* ---- thresholds relative to the target voltage T, in vu (adc.h: 1.0986 mV) -------------------------------------------- */
#define WIN_LO          20      /* probe / rest: the FULL window is [T-20, T+10) */
#define WIN_HI          10
#define V_OVER          45      /* charging, above the rail and above T+45 (~50 mV): over-voltage fault */
#define HOLD_LO         30      /* FULL holds while T-30 <= V <= T+45: wider than the window that leads into it, so it does not */
#define HOLD_HI         45      /* bounce between FULL and BLEED */
#define RAIL_MARGIN     30      /* "V <= rail+30" = the port is not above the supply */
#define RAMP_END_MARGIN 30      /* soft ramp ends when V > S-30 */

/* ---- timers, in control passes (4.5 ms) ------------------------------------------------------------------------- */
#define T_PROBE         4       /* settle with the output off before classifying  */
#define T_TAIL          8000    /* V>=T-20 and I<=5 for this long -> rest test (36 s) */
#define T_OPEN          450     /* CHARGE, past the soft ramp, no current at all for this long -> rest test (2 s) */
#define T_REST          230     /* output off while resting */
#define T_RAMP_STEP     100     /* soft start: a step every this many passes ... */
#define RAMP_GROW       8       /* ... of 1 + duty / 8 counts: a cell close to the rail needs many counts per amp, so the step follows the duty */
#define T_OC            200     /* over-current persistence */
#define T_OVER          100     /* over-voltage persistence */
#define T_FULL_OUT      500     /* FULL: outside the hold window this long -> test again (below) or bleed (above) */
#define T_BLEED_SETTLE  8       /* BLEED: the reading has settled under the bleed load after this many passes */
#define T_REMOVED       6       /* V < 1.00 V this long = cell removed */
#define I_OPEN          2       /* "no current": 29 mA, the offset of the sense input */
#define T_HELD          222     /* the limits held the port within this many passes (1 s): its current says nothing about the cell */
#define DUTY_MIN        2       /* the lowest duty while charging (1 is not used) */

typedef struct {
    uint8_t  st;
    uint16_t duty;
    uint8_t  sw;
    uint16_t v, i;
    uint16_t v_rest;             /* BLEED: the resting voltage it started from */
    uint8_t  t_bleed, drop;      /* BLEED: passes so far; how far the bleed load pulls the reading below the resting voltage (vu) */
    uint8_t  iset;               /* working current setting */
    uint8_t  ramp, ramp_div;
    uint16_t t_tail, t_open, t_rest, t_full, t_probe, t_gone;
    uint8_t  t_over, oc;
    uint8_t  announced;          /* the FULL beep was given for this fill */
    uint8_t  want, up, t_held;   /* for the limits: working current, chg_out_t.up, passes left of "held by the limits" */
} port_t;

static port_t XD P[NPORTS], W;

static void p_reset(port_t XD *p, uint8_t iset)
{
    p->st = PS_IDLE; p->duty = 0; p->sw = 0;
    p->want = p->up = p->t_held = 0;
    p->iset = iset; p->ramp = 0; p->ramp_div = 0;
    p->t_tail = p->t_open = p->t_rest = p->t_full = p->t_probe = 0;
    p->t_over = p->oc = 0;
    p->announced = 0;
}

void chg_init(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) { p_reset(&P[k], 0); P[k].v = P[k].i = 0; P[k].t_gone = 0; }
}

void chg_abort(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) p_reset(&P[k], 0);              /* keeps the filtered v, i */
}

void chg_cut(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) { P[k].duty = 0; P[k].ramp = 1; P[k].ramp_div = 0; }
}

/* per-port constant-current regulator: soft ramp (about 1/8 of the way to the setting per step, whatever the cell), then duty += (Iset - I) / 2 */
static void cc(port_t XD *p, uint16_t S)
{
    if (p->duty == 0 && !p->t_held) p->ramp = 1;           /* held by the limits: no soft start, the gate paces the rise */
    if (p->ramp) {
        if (p->duty == 0) p->duty = DUTY_MIN;
        if (++p->ramp_div >= T_RAMP_STEP) { p->ramp_div = 0; p->duty += 1 + p->duty / RAMP_GROW; }
        if (p->duty >= PWM_PERIOD) { p->duty = PWM_PERIOD; p->ramp = 0; }
        if (p->i >= p->iset) p->ramp = 0;
        if (p->v > S - RAMP_END_MARGIN) p->ramp = 0;
    } else {
        p->duty = (uint16_t)ctl_port_step((int16_t)p->duty, (int16_t)p->iset, (int16_t)p->i);
    }
    if (p->duty == 1) p->duty = DUTY_MIN;
}

/* the limits' gate on what cc() wants: a rise only when the port is let, one count at most (from off: to DUTY_MIN); the port named
   comes down by about the excess (as if the current were proportional to the duty: a duty wound up in a taper comes down fast), from
   DUTY_MIN to off */
static void gate(const chg_in_t XD *in, uint8_t k, uint16_t d0)
{
    uint16_t c;
    if (W.t_held) W.t_held--;
    if (W.duty > d0) {
        if (!(in->rise & (1u << k))) { W.duty = d0; W.t_held = T_HELD; W.ramp = 0; }
        else if (W.duty > (d0 ? d0 + 1 : DUTY_MIN)) W.duty = d0 ? d0 + 1 : DUTY_MIN;
    }
    if (k == in->cut && W.duty) {
        c = W.i ? (uint16_t)((uint32_t)W.duty * in->excess / (2u * W.i)) + 1 : W.duty;     /* half per pass: the reading lags */
        W.duty = W.duty > c + DUTY_MIN ? W.duty - c : W.duty > DUTY_MIN ? DUTY_MIN : 0;
        W.t_held = T_HELD; W.ramp = 0;
    }
}

static void to_probe(port_t XD *p, uint8_t g) { p_reset(p, g); p->st = PS_PROBE; }
static void to_rest(port_t XD *p)   { p->st = PS_REST; p->t_rest = 0; p->duty = 0; }
static void to_bleed(port_t XD *p)  { p->st = PS_BLEED; p->duty = 0; p->v_rest = p->v; p->t_bleed = p->drop = 0; }

/* returns 1 if the cell is inside the FULL window used at probe/rest time */
static uint8_t in_full_window(uint16_t v, uint16_t T) { return v >= T - WIN_LO && v < T + WIN_HI; }
static uint8_t above_window(uint16_t v, uint16_t T)   { return v >= T + WIN_HI; }

static void go_full(port_t XD *p, uint8_t XD *ev)
{
    p->st = PS_FULL; p->t_full = 0;
    if (!p->announced) { *ev = CE_FULL; p->announced = 1; }
}

static void go_fault(port_t XD *p, uint8_t XD *ev)
{
    p->st = PS_FAULT; p->duty = 0; p->sw = 0;
    *ev = CE_FAULT;
}

/* probe and rest: where the resting cell is decides */
static void classify(uint16_t T, uint8_t XD *ev)
{
    if (in_full_window(W.v, T)) go_full(&W, ev);
    else if (above_window(W.v, T)) to_bleed(&W);
    else { W.st = PS_CHARGE; W.ramp = 1; W.ramp_div = 0; }
}

/* works on W, a copy of the port: fixed addresses are much cheaper on the 8051 than access through a pointer */
static void one_port(const chg_in_t XD *in, uint8_t k, uint8_t XD *ev) REENT
{
    const uint16_t T = in->tgt, S = in->setpoint;
    uint8_t g = in->iset[k];

    /* measurement filters: V = (V + 3*adc)/4 twice per pass, I = (I + adc)/2 */
    W.v = (uint16_t)((W.v + 3u * in->v_now[k]) >> 2);
    W.v = (uint16_t)((W.v + 3u * in->v_now[k]) >> 2);
    W.i = (uint16_t)((W.i + (uint32_t)in->i_now[k]) >> 1);

    if (!in->run || !in->en[k]) { p_reset(&W, g); return; }

    /* cell present? (1.00 V).  Removal clears everything including a latched fault. */
    if (W.v < V_DEAD) {
        if (W.st != PS_IDLE && ++W.t_gone >= T_REMOVED) p_reset(&W, g);
        else if (W.st == PS_IDLE) W.iset = g;
        W.duty = 0; W.sw = 0;
        return;
    }
    W.t_gone = 0;
    if (W.st == PS_IDLE) to_probe(&W, g);

    W.iset = g;                                       /* working current: the setting */

    W.sw = 0;
    W.want = 0; W.up = 0;
    switch (W.st) {

    case PS_PROBE:
        W.duty = 0;
        if (++W.t_probe < T_PROBE) break;
        classify(T, ev);
        break;

    case PS_CHARGE: {
        uint8_t over_rail = W.v > in->rail + RAIL_MARGIN;
        uint16_t d0 = W.duty;
        W.want = W.iset;
        /* current limit supervision: 2*Iset + 7 for 200 passes */
        if (W.i >= (uint16_t)(2u * g + 7u)) {
            if (++W.oc >= T_OC) { W.oc = 0; go_fault(&W, ev); break; }
        } else if (W.oc) W.oc--;

        /* a cell pulled while charging: the loop winds the duty up, the pin follows the rail and nothing flows, which looks like the end
           of a taper.  A real cell past its soft ramp always draws something; look at the pin with the output off (the rest test).  A port
           the limits hold down may draw nothing at all */
        if (!W.t_held && !W.ramp && W.i <= I_OPEN) {
            if (++W.t_open >= T_OPEN) { W.t_open = 0; to_rest(&W); break; }
        } else W.t_open = 0;

        if (!over_rail) {
            if (W.t_held) {
                /* held by the limits: the current says nothing about the cell, so the tail timer stands still */
            } else if (W.v < T - WIN_LO) W.t_tail = 0;
            else if (W.i > I_TAIL) { if (W.t_tail) W.t_tail--; }
            else if (++W.t_tail >= T_TAIL) { W.t_tail = 0; to_rest(&W); break; }
            if (W.t_over) W.t_over--;
        } else {
            if (++W.t_over >= T_OVER && W.v > T + V_OVER) { W.t_over = 0; go_fault(&W, ev); break; }
        }
        cc(&W, S);
        if (in->lim) gate(in, k, d0); else W.t_held = 0;
        if (W.i + 2 >= W.iset || W.duty >= PWM_PERIOD) W.up = 0;           /* within a reading's noise of the setting: it has what it wants */
        else if (!W.duty) W.up = DUTY_MIN;                                  /* off: unknown, the start is small */
        else { uint16_t n = 1 + (W.i + W.duty / 2) / W.duty; W.up = n > 60 ? 60 : (uint8_t)n; }
        break;
    }

    case PS_REST:
        W.duty = 0;
        if (++W.t_rest < T_REST) break;
        W.t_rest = 0;
        classify(T, ev);
        break;

    case PS_FULL:
        W.duty = 0;
        if (W.v >= T - HOLD_LO && W.v <= T + HOLD_HI) { if (W.t_full) W.t_full--; }
        else if (++W.t_full >= T_FULL_OUT) {
            if (W.v < T) to_probe(&W, g); else to_bleed(&W);
        }
        break;

    case PS_BLEED:
        W.duty = 0;
        if (W.v >= V_FAULT) { go_fault(&W, ev); break; }
        /* the load pulls the reading below the cell's own voltage by the series drop, and the cell springs back when it ends: measure
           the drop once the reading has settled and end the bleed when the cell itself is down to the target, so the rest test finds
           it in the window and does not bleed again */
        if (W.t_bleed < T_BLEED_SETTLE) {
            if (++W.t_bleed == T_BLEED_SETTLE && W.v_rest > W.v) W.drop = W.v_rest - W.v > 255 ? 255 : (uint8_t)(W.v_rest - W.v);
        } else if (W.v + W.drop <= T) { to_rest(&W); break; }
        W.sw = 1;                                       /* bleeding: switch high, PWM off */
        break;

    case PS_FAULT:
        W.duty = 0; W.sw = 0;
        break;
    }
}

void chg_pass(const chg_in_t XD *in, chg_out_t XD *out)
{
    uint8_t k;
    out->any_active = 0;
    for (k = 0; k < NPORTS; k++) {
        out->event[k] = CE_NONE;
        W = P[k];
        one_port(in, k, &out->event[k]);
        P[k] = W;
        out->duty[k] = W.duty;
        out->sw[k] = W.sw;
        out->state[k] = W.st;
        out->want[k] = W.want;
        out->up[k] = W.st == PS_CHARGE ? W.up : 0;
        out->held[k] = W.st != PS_CHARGE || !W.t_held ? 0 : W.i <= I_OPEN ? 2 : 1;
        out->v[k] = W.v;
        out->i[k] = W.i;
        if (W.st == PS_CHARGE || W.st == PS_BLEED) out->any_active = 1;
    }
}
