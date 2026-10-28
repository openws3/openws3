#include "limits.h"
#include "cal.h"

#define LIM_PERIOD     22       /* the budget is worked out every 22 passes (~100 ms) */
#define TRIP_UPDATES   10       /* cut mode: below the floor for this many updates (~1 s) before the run is stopped */
#define VIN_HYST_MV    100      /* the voltage budget grows again above the floor plus this: small enough for a 5 V supply that is
                                   only a little above a 4.9 V floor when unloaded */
#define EFF_PCT        90       /* assumed converter efficiency */
#define P_MARGIN       10       /* 1 W: where the voltage budget starts shrinking from, above what is really drawn */
#define DRAWS          2        /* iu: a port drawing more than this can be brought down (the sense offset reads about this) */

lim_cfg_t XD lim_cfg;
lim_in_t  XD lim_in;
lim_out_t XD lim_out;

/* All working data is static in external RAM: direct RAM is full on this chip, and locals / parameters live there. */
static uint16_t XD P_v;                              /* the voltage floor's power budget: the one stored number */
static uint8_t  XD tick, n_v;

static void gate_free(void) { lim_out.on = 0; lim_out.rise = 0xFF; lim_out.cut = NPORTS; lim_out.excess = 0; }

void lim_reset(void)
{
    P_v = LIM_P_BIG; tick = LIM_PERIOD - 1; n_v = 0;                /* the budget is worked out on the first pass */
    gate_free();
    lim_out.p_cur = lim_out.p_vol = lim_out.budget = LIM_P_BIG; lim_out.bcnt = 0; lim_out.est_ma = 0; lim_out.trip = LT_NONE;
}

/* The gate (limits.h), every pass, on the measured port currents against the budget lim_out.bcnt. */
static uint8_t XD g_ord[NPORTS], g_k, g_a, g_b, g_t, g_deny, g_slack;
static int16_t XD g_room;
static uint16_t XD g_sum;

/* port a goes before port b */
static uint8_t first(uint8_t a, uint8_t b)
{
    if (lim_cfg.mode == LM_BY_VHIGH && lim_in.v[a] != lim_in.v[b]) return lim_in.v[a] > lim_in.v[b];
    if (lim_cfg.mode == LM_EQUAL && lim_in.i[a] != lim_in.i[b]) return lim_in.i[a] < lim_in.i[b];
    return a < b;
}

static void gate(void)
{
    g_sum = 0;
    for (g_a = 0; g_a < NPORTS; g_a++) {                 /* the priority order (insertion sort, six ports) */
        g_t = g_a;
        for (g_b = g_a; g_b > 0 && first(g_t, g_ord[g_b - 1]); g_b--) g_ord[g_b] = g_ord[g_b - 1];
        g_ord[g_b] = g_t;
        if (lim_in.charging[g_a]) g_sum += lim_in.i[g_a];
    }
    lim_out.on = 1; lim_out.rise = 0; lim_out.cut = NPORTS; lim_out.excess = 0;
    g_room = (int16_t)lim_out.bcnt - (int16_t)g_sum;
    g_deny = g_slack = NPORTS;
    for (g_a = 0; g_a < NPORTS; g_a++) {
        g_k = g_ord[g_a];
        if (!lim_in.charging[g_k]) continue;
        if (lim_in.up[g_k]) {
            g_t = (uint8_t)(LIM_ROOM + 3u * lim_in.up[g_k]);
            if (g_room >= g_t && g_deny == NPORTS) { lim_out.rise |= (uint8_t)(1u << g_k); g_room -= g_t; }   /* none goes ahead of a port in front that waits */
            else if (g_deny == NPORTS) g_deny = g_k;      /* the first port that wants more and gets no room */
        }
        if (lim_in.i[g_k] > DRAWS) g_slack = g_k;          /* the last port that draws something */
    }
    if (g_slack == NPORTS) return;
    if (g_sum > lim_out.bcnt) {                          /* over the budget: the last port comes down */
        lim_out.cut = g_slack;
        lim_out.excess = g_sum - lim_out.bcnt > 255 ? 255 : (uint8_t)(g_sum - lim_out.bcnt);
    } else if (g_deny != NPORTS && g_deny != g_slack && first(g_deny, g_slack)
               && (lim_cfg.mode != LM_EQUAL || lim_in.i[g_slack] > lim_in.i[g_deny] + LIM_ROOM)) {
        lim_out.cut = g_slack; lim_out.excess = LIM_ROOM;  /* a port further up wants more: the last one makes room */
    }
}

static uint8_t  XD p_k, p_s;
static uint16_t XD p_P;
static uint32_t XD p_sum_ma, p_est, p_vin_mv, p_rail_mv, p_pin, p_t;

static void budget(void)
{
    p_sum_ma = 0; p_est = 0; p_vin_mv = cal_vin_mv(lim_in.vin); p_rail_mv = cal_port_mv(lim_in.rail);
    for (p_k = 0; p_k < NPORTS; p_k++) if (lim_in.charging[p_k]) p_sum_ma += cal_port_ma(lim_in.i[p_k]);
    if (p_vin_mv) p_est = p_rail_mv * p_sum_ma * 100u / ((uint32_t)EFF_PCT * p_vin_mv);       /* input current, mA */
    if (p_est > 65535u) p_est = 65535u;
    lim_out.est_ma = (uint16_t)p_est;

    p_P = LIM_P_BIG; lim_out.p_cur = lim_out.p_vol = LIM_P_BIG;
    if (lim_cfg.ilim) {
        /* input current limit: input power = limit x input voltage (0.1 W); worked out from scratch, no stored state */
        p_t = (uint32_t)lim_cfg.ilim * p_vin_mv / 1000u;
        if (p_t < p_P) p_P = (uint16_t)p_t;
        lim_out.p_cur = (uint16_t)p_t;
    }

    if (!lim_cfg.vmin) { P_v = LIM_P_BIG; n_v = 0; }
    else if (lim_cfg.vcut) {                             /* cut: the run stops after a second below the floor */
        if (p_vin_mv >= VMIN_MV(lim_cfg.vmin)) n_v = 0;
        else if (++n_v >= TRIP_UPDATES) { n_v = TRIP_UPDATES; lim_out.trip = LT_VOLTAGE; }
    } else {                                             /* smooth: the budget shrinks while the input is below the floor ... */
        if (p_vin_mv < VMIN_MV(lim_cfg.vmin)) {
            p_pin = p_est * p_vin_mv / 100000u;                                        /* what is drawn now, 0.1 W */
            if (P_v > p_pin + P_MARGIN) P_v = (uint16_t)(p_pin + P_MARGIN);            /* start from what is really drawn */
            p_s = (uint8_t)(P_v / 16 > 255 ? 255 : P_v / 16); if (p_s < 2) p_s = 2;
            P_v = P_v > p_s ? (uint16_t)(P_v - p_s) : 0;
        } else if (p_vin_mv > VMIN_MV(lim_cfg.vmin) + VIN_HYST_MV && P_v < LIM_P_BIG) {
            P_v = (uint16_t)(P_v + 4);                   /* ... and grows back slowly above floor + margin */
        }
        lim_out.p_vol = P_v;
        if (P_v < p_P) p_P = P_v;
    }

    lim_out.budget = p_P;
    if (p_P >= LIM_P_BIG || !p_rail_mv) { lim_out.bcnt = 0; return; }     /* no limit active */
    /* power budget -> port current budget: P x 90 % / rail voltage, in iu (14.65 mA) */
    p_t = (uint32_t)p_P * (EFF_PCT * 1000UL) / p_rail_mv;                              /* mA */
    p_t = cal_ma_to_port(p_t > 60000u ? 60000u : (uint16_t)p_t);
    lim_out.bcnt = p_t > 1000u ? 1000u : p_t ? (uint16_t)p_t : 1;            /* at least 1: 0 means no limit */
}

void lim_pass(void)
{
    lim_out.trip = LT_NONE;
    if (++tick >= LIM_PERIOD) { tick = 0; budget(); }
    if (lim_out.bcnt) gate(); else gate_free();
}

/* a step ladder: { value from which this step applies, step } ..., closed by { max, 0 } */
typedef struct { uint8_t from, step; } rung_t;
static const CODE rung_t ILIM_STEPS[] = { { ILIM_MIN, 1 }, { 30, 5 }, { ILIM_MAX, 0 } };
static const CODE rung_t VMIN_STEPS[] = { { VMIN_MIN, 1 }, { 100, 2 }, { 120, 10 }, { VMIN_KNEE, 1 }, { VMIN_MAX, 0 } };   /* all 0.5 V from 6 V */

static uint8_t ladder(uint8_t v, uint8_t up, const CODE rung_t *l)
{
    uint8_t k = 0, top = 0;
    uint16_t n;
    while (l[top].step) top++;                           /* l[top] closes the ladder: its value is the maximum */
    if (up) {
        if (!v) return l[0].from;
        while (k + 1 < top && v >= l[k + 1].from) k++;
        n = (uint16_t)v + l[k].step;
        return (uint8_t)(n > l[top].from ? l[top].from : n);
    }
    if (v <= l[0].from) return 0;
    while (k + 1 < top && v > l[k + 1].from) k++;
    return (uint8_t)(v - l[k].step < l[k].from ? l[k].from : v - l[k].step);
}

uint8_t ilim_step(uint8_t v, uint8_t up) { return ladder(v, up, ILIM_STEPS); }
uint8_t vmin_step(uint8_t v, uint8_t up) { return ladder(v, up, VMIN_STEPS); }

const char *lim_mode_name(uint8_t m)
{
    static const CODE char *const NAME[LM_NMODES] = { "EQUAL", "CH1 FIRST", "HIGH V FIRST" };
    return NAME[m < LM_NMODES ? m : LM_EQUAL];
}
