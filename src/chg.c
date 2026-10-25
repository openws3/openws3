#include "chg.h"
#include "ctl.h"

/* ---- thresholds relative to the target voltage T, in port-voltage counts (1 count = 1.0986 mV) ----------------------- */
/* the full-charge voltages (4.20, 4.35 V) and the storage voltages (3.80, 3.85 V) use different windows */
#define WIN_LO_FULL     20      /* full voltages: FULL window is [T-20, T+10)              probe / rest classification */
#define WIN_HI_FULL     10
#define WIN_LO_STOR     30      /* storage voltages: [T-30, T+35]                                                    */
#define WIN_HI_STOR     35
#define REST_LO         15      /* after a rest, full voltages: FULL if T-15 <= V < T+10, PAUSE if V < T-17 */
#define REST_PAUSE      17
#define BLEED_WIN       20      /* bleed branch: FULL when T-20 <= V <= T+20 */
#define BLEED_LOW       40      /* bleed ends (pause) after V <= T-40 for T_BLEED_LOW passes */
#define HOLD_LO         30      /* FULL(probe/rest): droop below T-30 */
#define HOLD_WIN        45      /* FULL(bleed): window T-45 .. T+45 */
#define HOLD_EDGE       17
#define STOR_HI         35      /* storage: V >= T+35 starts the step-down counter */
#define RAIL_MARGIN     30      /* "V <= rail+30" = the port is not above the supply */
#define RAMP_END_MARGIN 30      /* soft ramp ends when V > S-30 */

/* ---- timers, in control passes (4.5 ms) ------------------------------------------------------------------------- */
#define T_PROBE         4       /* settle with the output off before classifying  */
#define T_TAIL          8000    /* full voltages: V>=T-20 and I<=5 for this long -> rest test (36 s) */
#define T_OPEN          450     /* CHARGE, past the soft ramp, no current at all for this long -> rest test (2 s) */
#define T_REST          230     /* output off while resting */
#define T_PAUSE         4000    /* pause before restarting after a rest below the target (18 s) */
#define T_RAMP_STEP     100     /* soft start: +1 duty per this many passes */
#define T_OC            200     /* over-current persistence */
#define T_OVER_FAULT    100     /* V>4.40 V above the rail */
#define T_OVER_420      200     /* 4.20 V target: V>4.25 V above the rail */
#define T_STOR_STEP1    500     /* storage: V>=T+35 and V>=3.90 V -> step the current down / pause */
#define T_STOR_STEP2    2000    /* storage: V>=T+35 -> step down */
#define T_FULL_RESTART  500     /* FULL(probe/rest): droop restart */
#define T_FULL_KICK     200     /* FULL: far below the target for this long -> restart */
#define T_FULL_WIN      200     /* FULL(bleed): outside the window this long -> restart */
#define T_FULL_WIN_MAX  500
#define T_BLEED_LOW     3000    /* bleed: stays this long below T-40 -> hold */
#define T_BLEED_HOLD    200     /* bleed: output off while holding */
#define T_REMOVED       6       /* V < 1.00 V this long = cell removed */
#define I_OPEN          2       /* "no current": 29 mA, the offset of the sense input */
#define I_STEP          7       /* storage current step-down */
#define I_FLOOR         15      /* ... until the setting is below this */
#define I_LOW_V_CAP     63      /* current cap at low cell voltage (0.92 A) */
#define V_CAP_FULLMODE  3458    /* full voltages: cap applies below 3.80 V when the setting is >= 70 */
#define V_CAP_STORAGE   3503    /* storage voltages: below 3.85 V */
#define I_CAP_THRESHOLD 70
#define V_VIN_LIMIT     3549    /* storage: step-down needs V >= 3.90 V */

typedef struct {
    uint8_t  st;
    uint16_t duty;
    uint8_t  sw;
    uint16_t v, i;
    uint8_t  iset;               /* working current setting */
    uint8_t  ramp, ramp_div;
    uint16_t t_tail, t_open, t_stor, t_rest, t_pause, t_full, t_bleed_lo, t_bleed_hold, t_probe, t_gone;
    uint8_t  t_over, oc;
    uint8_t  full_kind;          /* 0: from probe/rest, 1: from the bleed window */
    uint8_t  bleed_hold;
    uint8_t  bleed_checked;
    uint8_t  announced;          /* the FULL beep was given for this fill */
    uint8_t  want, thr, ieff;    /* for the limits: working current before the cap, whether the cap bites, the current actually regulated to */
    uint16_t pduty;              /* the duty a parked port had: it restarts from there (the soft ramp is minutes long on this hardware) */
} port_t;

static port_t XD P[NPORTS];

static void p_reset(port_t XD *p, uint8_t iset)
{
    p->st = PS_IDLE; p->duty = 0; p->sw = 0;
    p->want = p->thr = p->ieff = 0; p->pduty = 0;
    p->iset = iset; p->ramp = 0; p->ramp_div = 0;
    p->t_tail = p->t_open = p->t_stor = p->t_rest = p->t_pause = p->t_full = p->t_bleed_lo = p->t_bleed_hold = p->t_probe = 0;
    p->t_over = p->oc = 0;
    p->full_kind = 0; p->bleed_hold = 0; p->bleed_checked = 0; p->announced = 0;
}

void chg_init(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) { p_reset(&P[k], 0); P[k].v = P[k].i = 0; P[k].t_gone = 0; }
}

void chg_abort(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) { uint16_t v = P[k].v, i = P[k].i; p_reset(&P[k], 0); P[k].v = v; P[k].i = i; }
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

static void to_probe(port_t XD *p, uint8_t g)
{
    uint16_t v = p->v, i = p->i;
    p_reset(p, g);
    p->v = v; p->i = i;
    p->st = PS_PROBE;
}

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

static void go_full(port_t XD *p, uint8_t kind, uint8_t XD *ev)
{
    p->st = PS_FULL; p->full_kind = kind; p->t_full = 0;
    if (!p->announced) { *ev = CE_FULL; p->announced = 1; }
}

static void go_fault(port_t XD *p, uint8_t XD *ev)
{
    p->st = PS_FAULT; p->duty = 0; p->sw = 0;
    *ev = CE_FAULT;
}

static void one_port(port_t XD *p, const chg_in_t XD *in, uint8_t k, uint8_t XD *ev) REENT
{
    const uint16_t T = in->tgt, S = in->setpoint;
    const uint8_t volt = in->volt;
    uint8_t g = in->iset[k];

    /* measurement filters: V = (V + 3*adc)/4 twice per pass, I = (I + adc)/2 */
    p->v = (uint16_t)((p->v + 3u * in->adc_v[k]) >> 2);
    p->v = (uint16_t)((p->v + 3u * in->adc_v[k]) >> 2);
    p->i = (uint16_t)((p->i + (uint32_t)in->adc_i[k]) >> 1);

    if (!in->run || !in->en[k]) { p_reset(p, g); return; }

    /* cell present? (1.00 V).  Removal clears everything including a latched fault. */
    if (p->v < V_DEAD) {
        if (p->st != PS_IDLE && ++p->t_gone >= T_REMOVED) p_reset(p, g);
        else if (p->st == PS_IDLE) p->iset = g;
        p->duty = 0; p->sw = 0;
        return;
    }
    p->t_gone = 0;
    if (p->st == PS_IDLE) to_probe(p, g);

    /* working current setting: caps by input voltage and by cell voltage */
    if (in->vin < 1062 && p->iset > I_CAP_THRESHOLD) p->iset = I_CAP_THRESHOLD;
    if (volt < VOLT_STORAGE) {
        if (g >= I_CAP_THRESHOLD && p->v < V_CAP_FULLMODE) p->iset = I_LOW_V_CAP; else p->iset = g;
    } else if (p->iset >= I_CAP_THRESHOLD && p->v < V_CAP_STORAGE) {
        p->iset = I_LOW_V_CAP;
    }

    p->sw = 0;
    p->want = p->thr = 0;
    switch (p->st) {

    case PS_PROBE:
        p->duty = 0;
        if (++p->t_probe < T_PROBE) break;
        if (in_full_window(p->v, T, volt)) go_full(p, 0, ev);
        else if (above_window(p->v, T, volt)) { p->st = PS_BLEED; p->bleed_checked = 0; p->bleed_hold = 0; }
        else { p->st = PS_CHARGE; p->ramp = 1; p->ramp_div = 0; p->iset = (volt < VOLT_STORAGE) ? p->iset : g; }
        break;

    case PS_CHARGE: {
        uint8_t over_rail = p->v > in->rail + RAIL_MARGIN;
        p->want = p->iset; p->ieff = p->iset; p->thr = 0;      /* the user limits may hold the current below the wanted one, or park the port */
        if (in->cap[k] == CAP_PARK) { p->ieff = 0; p->thr = 1; }
        else if (in->cap[k] && in->cap[k] < p->iset) { p->ieff = in->cap[k]; p->thr = 1; }
        /* current limit supervision: 2*Iset + 7 for 200 passes */
        if (p->i >= (uint16_t)(2u * g + 7u)) {
            if (++p->oc >= T_OC) { p->oc = 0; go_fault(p, ev); break; }
        } else if (p->oc) p->oc--;

        /* a cell pulled while charging: the loop winds the duty up, the pin follows the rail and nothing flows, which looks like the end
           of a taper.  A real cell past its soft ramp always draws something; look at the pin with the output off (the rest test).  A port held at a cap still draws that cap, only a parked one (output off) says nothing */
        if (p->ieff && !p->ramp && p->i <= I_OPEN) {
            if (++p->t_open >= T_OPEN) { p->t_open = 0; p->st = PS_REST; p->t_rest = 0; p->duty = 0; break; }
        } else p->t_open = 0;

        if (!over_rail) {
            if (p->thr) {
                /* throttled: the current says nothing about the cell, so the tail and step-down timers stand still */
            } else if (volt < VOLT_STORAGE) {
                if (p->v < T - WIN_LO_FULL) p->t_tail = 0;
                else if (p->i > I_TAIL) { if (p->t_tail) p->t_tail--; }
                else if (++p->t_tail >= T_TAIL) { p->t_tail = 0; p->st = PS_REST; p->t_rest = 0; p->duty = 0; break; }
            } else {
                if (p->v < T + STOR_HI) { if (p->t_stor) p->t_stor--; }
                else {
                    p->t_stor++;
                    if (p->t_stor >= T_STOR_STEP1 && p->v >= V_VIN_LIMIT) {
                        p->t_stor = 0;
                        if (p->i > I_TAIL) {
                            if (p->iset >= I_FLOOR) p->iset -= I_STEP;
                            else { p->st = PS_REST; p->t_rest = 0; p->duty = 0; break; }
                        } else { p->st = PS_PAUSE; p->t_pause = 0; p->duty = 0; break; }
                    }
                    if (p->t_stor >= T_STOR_STEP2) {
                        p->t_stor = 0;
                        if (p->iset >= I_FLOOR) p->iset -= I_STEP;
                        else { p->st = PS_REST; p->t_rest = 0; p->duty = 0; break; }
                    }
                }
            }
            if (p->t_over) p->t_over--;
        } else {
            if (volt == VOLT_3_80 && p->v < 3991) { p->st = PS_BLEED; p->bleed_checked = 0; p->bleed_hold = 0; p->duty = 0; break; }
            p->t_over++;
            if (p->t_over >= T_OVER_FAULT && p->v > V_FAULT) { p->t_over = 0; go_fault(p, ev); break; }
            if (p->t_over >= T_OVER_420 && volt == VOLT_4_20 && p->v > V_LATCH_420) { p->t_over = 0; go_fault(p, ev); break; }
        }
        if (p->ieff) {
            if (!p->duty && p->pduty) { p->duty = p->pduty; p->ramp = 0; }      /* released: carry on from where it was parked */
            p->pduty = 0;
            cc(p, S);
        } else {                                                                /* parked: output off */
            if (p->duty) p->pduty = p->duty;
            p->duty = 0; p->ramp = 1; p->ramp_div = 0;
        }
        break;
    }

    case PS_REST:
        p->duty = 0;
        if (++p->t_rest < T_REST) break;
        p->t_rest = 0;
        if (volt < VOLT_STORAGE) {
            if (p->v >= T - REST_LO && p->v < T + WIN_HI_FULL) { go_full(p, 0, ev); break; }
            if (p->v < T - REST_PAUSE) { p->st = PS_PAUSE; p->t_pause = 0; break; }
        } else {
            if (p->v >= T - WIN_LO_STOR && p->v < T + WIN_HI_STOR) { go_full(p, 0, ev); break; }
            if (p->v < T - REST_PAUSE) { p->st = PS_PAUSE; p->t_pause = 0; }
            if (p->v < T - WIN_LO_STOR) p->iset = 14;
        }
        if (above_window(p->v, T, volt)) { p->st = PS_BLEED; p->bleed_checked = 0; p->bleed_hold = 0; }
        else if (p->st == PS_REST) { p->st = PS_CHARGE; p->ramp = 1; }
        break;

    case PS_PAUSE:
        p->duty = 0;
        if (++p->t_pause >= T_PAUSE) to_probe(p, g);
        break;

    case PS_FULL:
        p->duty = 0;
        if (p->full_kind == 0) {
            if (above_window(p->v, T, volt)) { p->st = PS_BLEED; p->bleed_checked = 0; p->bleed_hold = 0; break; }
            if (p->v < T - HOLD_LO) {
                p->t_full++;
                if (p->t_full >= T_FULL_RESTART || (p->v < T - 455 && p->t_full >= T_FULL_KICK)) {
                    to_probe(p, g); break;
                }
            } else if (p->t_full) p->t_full--;
        } else {
            if (p->v >= T - HOLD_WIN && p->v <= T + HOLD_WIN) { if (p->t_full) p->t_full--; }
            else {
                p->t_full++;
                if (p->t_full >= T_FULL_WIN_MAX || ((p->v < T - HOLD_EDGE || p->v > T + HOLD_EDGE) && p->t_full >= T_FULL_WIN)) {
                    p->st = PS_BLEED; p->bleed_checked = 0; p->bleed_hold = 0; p->t_full = 0; p->announced = 0;
                }
            }
        }
        break;

    case PS_BLEED:
        p->duty = 0;
        if (p->v >= V_FAULT) { go_fault(p, ev); break; }
        if (!p->bleed_checked) {                         /* one look at the rest voltage on entry */
            p->bleed_checked = 1;
            if (p->v >= T - BLEED_WIN && p->v <= T + BLEED_WIN) { go_full(p, 1, ev); break; }
        }
        if (p->v <= T - BLEED_LOW) {
            if (++p->t_bleed_lo >= T_BLEED_LOW) {
                if (p->v > 20) { p->bleed_hold = 1; p->t_bleed_lo = 0; }
            }
        } else if (p->t_bleed_lo) p->t_bleed_lo--;
        if (!p->bleed_hold) { p->sw = 1; break; }        /* bleeding: switch high, PWM off */
        if (++p->t_bleed_hold > T_BLEED_HOLD) {          /* held long enough: look again */
            p->t_bleed_hold = 0; p->bleed_hold = 0;
            if (p->v <= T + BLEED_WIN && p->v >= T - BLEED_WIN) go_full(p, 1, ev);
            else if (p->v < T - BLEED_WIN) to_probe(p, g);
        }
        break;

    case PS_FAULT:
        p->duty = 0; p->sw = 0;
        break;
    }
}

void chg_pass(const chg_in_t XD *in, chg_out_t XD *out)
{
    uint8_t k;
    out->any_active = 0;
    for (k = 0; k < NPORTS; k++) {
        port_t XD *p = &P[k];
        out->event[k] = CE_NONE;
        one_port(p, in, k, &out->event[k]);
        out->duty[k] = p->duty;
        out->sw[k] = p->sw;
        out->state[k] = p->st;
        out->want[k] = p->want;
        out->thr[k] = p->thr;
        out->v[k] = p->v;
        out->i[k] = p->i;
        if (p->st == PS_CHARGE || p->st == PS_BLEED) out->any_active = 1;
    }
}
