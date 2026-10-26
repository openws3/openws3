#include "chg.h"
#include "ctl.h"

/* ---- thresholds relative to the target voltage T, in port-voltage counts (1 count = 1.0986 mV) ----------------------- */
/* the full-charge voltages (4.20, 4.35 V) and the storage voltages (3.80, 3.85 V) use different windows */
#define WIN_LO_FULL     20      /* probe / rest: FULL window, full voltages [T-20, T+10) */
#define WIN_HI_FULL     10
#define WIN_LO_STOR     30      /*                    storage voltages [T-30, T+35] */
#define WIN_HI_STOR     35
#define V_OVER          45      /* charging, above the rail and above T+45 (~50 mV): over-voltage fault */
#define HOLD_LO         30      /* FULL holds while T-30 <= V <= T+45: wider than the windows that lead into it, so it does not */
#define HOLD_HI         45      /* bounce between FULL and BLEED */
#define STOR_HI         35      /* storage: V >= T+35 while charging steps the current down */
#define RAIL_MARGIN     30      /* "V <= rail+30" = the port is not above the supply */
#define RAMP_END_MARGIN 30      /* soft ramp ends when V > S-30 */

/* ---- timers, in control passes (4.5 ms) ------------------------------------------------------------------------- */
#define T_PROBE         4       /* settle with the output off before classifying  */
#define T_TAIL          8000    /* full voltages: V>=T-20 and I<=5 for this long -> rest test (36 s) */
#define T_OPEN          450     /* CHARGE, past the soft ramp, no current at all for this long -> rest test (2 s) */
#define T_REST          230     /* output off while resting */
#define T_RAMP_STEP     100     /* soft start: +1 duty per this many passes */
#define T_OC            200     /* over-current persistence */
#define T_OVER          100     /* over-voltage persistence */
#define T_STOR_STEP     500     /* storage: V>=T+35 this long -> the current one step down (2.2 s) */
#define T_FULL_OUT      500     /* FULL: outside the hold window this long -> test again (below) or bleed (above) */
#define T_REMOVED       6       /* V < 1.00 V this long = cell removed */
#define I_OPEN          2       /* "no current": 29 mA, the offset of the sense input */
#define I_STEP          7       /* storage current step-down (0.1 A) */
#define I_FLOOR         15      /* ... until the setting is below this, then the rest test */

typedef struct {
    uint8_t  st;
    uint16_t duty;
    uint8_t  sw;
    uint16_t v, i;
    uint8_t  iset;               /* working current setting */
    uint8_t  ramp, ramp_div;
    uint16_t t_tail, t_open, t_stor, t_rest, t_full, t_probe, t_gone;
    uint8_t  t_over, oc;
    uint8_t  announced;          /* the FULL beep was given for this fill */
    uint8_t  want, thr, ieff;    /* for the limits: working current before the cap, whether the cap bites, the current actually regulated to */
    uint16_t pduty;              /* the duty a parked port had: it restarts from there (the soft ramp is minutes long on this hardware) */
} port_t;

static port_t XD P[NPORTS], W;

static void p_reset(port_t XD *p, uint8_t iset)
{
    p->st = PS_IDLE; p->duty = 0; p->sw = 0;
    p->want = p->thr = p->ieff = 0; p->pduty = 0;
    p->iset = iset; p->ramp = 0; p->ramp_div = 0;
    p->t_tail = p->t_open = p->t_stor = p->t_rest = p->t_full = p->t_probe = 0;
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

/* per-port constant-current regulator: soft ramp, then duty += (Iset - I) / 2 */
static void cc(port_t XD *p, uint16_t S)
{
    if (p->duty == 0) p->ramp = 1;
    if (p->ramp) {
        if (p->duty == 0) p->duty = 2;
        if (++p->ramp_div >= T_RAMP_STEP) { p->ramp_div = 0; p->duty++; }
        if (p->duty >= PWM_PERIOD) { p->duty = PWM_PERIOD; p->ramp = 0; }
        if (p->i >= p->ieff) p->ramp = 0;
        if (p->v > S - RAMP_END_MARGIN) p->ramp = 0;
    } else {
        p->duty = (uint16_t)ctl_port_step((int16_t)p->duty, (int16_t)p->ieff, (int16_t)p->i);
    }
    if (p->duty == 1) p->duty = 2;
}

static void to_probe(port_t XD *p, uint8_t g) { p_reset(p, g); p->st = PS_PROBE; }
static void to_rest(port_t XD *p)   { p->st = PS_REST; p->t_rest = 0; p->duty = 0; }
static void to_bleed(port_t XD *p)  { p->st = PS_BLEED; p->duty = 0; }

/* returns 1 if the cell is inside the FULL window used at probe/rest time */
static uint8_t in_full_window(uint16_t v, uint16_t T, uint8_t volt)
{
    if (volt < VOLT_STORAGE) return v >= T - WIN_LO_FULL && v < T + WIN_HI_FULL;
    return v >= T - WIN_LO_STOR && v <= T + WIN_HI_STOR;
}
static uint8_t above_window(uint16_t v, uint16_t T, uint8_t volt)
{
    if (volt < VOLT_STORAGE) return v >= T + WIN_HI_FULL;
    return v > T + WIN_HI_STOR;
}

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
static void classify(uint16_t T, uint8_t volt, uint8_t XD *ev)
{
    if (in_full_window(W.v, T, volt)) go_full(&W, ev);
    else if (above_window(W.v, T, volt)) to_bleed(&W);
    else { W.st = PS_CHARGE; W.ramp = 1; W.ramp_div = 0; }      /* storage after a rest: tops up at the stepped-down current */
}

/* works on W, a copy of the port: fixed addresses are much cheaper on the 8051 than access through a pointer */
static void one_port(const chg_in_t XD *in, uint8_t k, uint8_t XD *ev) REENT
{
    const uint16_t T = in->tgt, S = in->setpoint;
    const uint8_t volt = in->volt;
    uint8_t g = in->iset[k];

    /* measurement filters: V = (V + 3*adc)/4 twice per pass, I = (I + adc)/2 */
    W.v = (uint16_t)((W.v + 3u * in->adc_v[k]) >> 2);
    W.v = (uint16_t)((W.v + 3u * in->adc_v[k]) >> 2);
    W.i = (uint16_t)((W.i + (uint32_t)in->adc_i[k]) >> 1);

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

    if (volt < VOLT_STORAGE) W.iset = g;              /* working current: the setting (storage keeps its step-downs until the next probe) */

    W.sw = 0;
    W.want = W.thr = 0;
    switch (W.st) {

    case PS_PROBE:
        W.duty = 0;
        if (++W.t_probe < T_PROBE) break;
        classify(T, volt, ev);
        break;

    case PS_CHARGE: {
        uint8_t over_rail = W.v > in->rail + RAIL_MARGIN;
        W.want = W.iset; W.ieff = W.iset; W.thr = 0;      /* the user limits may hold the current below the wanted one, or park the port */
        if (in->cap[k] == CAP_PARK) { W.ieff = 0; W.thr = 1; }
        else if (in->cap[k] && in->cap[k] < W.iset) { W.ieff = in->cap[k]; W.thr = 1; }
        /* current limit supervision: 2*Iset + 7 for 200 passes */
        if (W.i >= (uint16_t)(2u * g + 7u)) {
            if (++W.oc >= T_OC) { W.oc = 0; go_fault(&W, ev); break; }
        } else if (W.oc) W.oc--;

        /* a cell pulled while charging: the loop winds the duty up, the pin follows the rail and nothing flows, which looks like the end
           of a taper.  A real cell past its soft ramp always draws something; look at the pin with the output off (the rest test).  A port held at a cap still draws that cap, only a parked one (output off) says nothing */
        if (W.ieff && !W.ramp && W.i <= I_OPEN) {
            if (++W.t_open >= T_OPEN) { W.t_open = 0; to_rest(&W); break; }
        } else W.t_open = 0;

        if (!over_rail) {
            if (W.thr) {
                /* throttled: the current says nothing about the cell, so the tail and step-down timers stand still */
            } else if (volt < VOLT_STORAGE) {
                if (W.v < T - WIN_LO_FULL) W.t_tail = 0;
                else if (W.i > I_TAIL) { if (W.t_tail) W.t_tail--; }
                else if (++W.t_tail >= T_TAIL) { W.t_tail = 0; to_rest(&W); break; }
            } else {
                /* storage: the rail sits well above the target, so it is no voltage clamp.  While the loaded cell is above the
                   target the current steps down; at the floor the rest test decides */
                if (W.v < T + STOR_HI) { if (W.t_stor) W.t_stor--; }
                else if (++W.t_stor >= T_STOR_STEP) {
                    W.t_stor = 0;
                    if (W.iset >= I_FLOOR) W.iset -= I_STEP;
                    else { to_rest(&W); break; }
                }
            }
            if (W.t_over) W.t_over--;
        } else {
            if (volt >= VOLT_STORAGE) { to_bleed(&W); break; }          /* a storage cell above the rail: far above its target */
            if (++W.t_over >= T_OVER && W.v > T + V_OVER) { W.t_over = 0; go_fault(&W, ev); break; }
        }
        if (W.ieff) {
            if (!W.duty && W.pduty) { W.duty = W.pduty; W.ramp = 0; }      /* released: carry on from where it was parked */
            W.pduty = 0;
            cc(&W, S);
        } else {                                                                /* parked: output off */
            if (W.duty) W.pduty = W.duty;
            W.duty = 0; W.ramp = 1; W.ramp_div = 0;
        }
        break;
    }

    case PS_REST:
        W.duty = 0;
        if (++W.t_rest < T_REST) break;
        W.t_rest = 0;
        classify(T, volt, ev);
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
        if (W.v <= T) { to_rest(&W); break; }          /* down to the target under the bleed load: the rest test decides */
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
        out->thr[k] = W.thr;
        out->v[k] = W.v;
        out->i[k] = W.i;
        if (W.st == PS_CHARGE || W.st == PS_BLEED) out->any_active = 1;
    }
}
