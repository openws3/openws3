#include "limits.h"
#include "cal.h"

#define LIM_PERIOD     22       /* the caps are recomputed every 22 passes (~100 ms) */
#define TRIP_UPDATES   10       /* cut mode: below the floor for this many updates (~1 s) before the run is stopped */
#define VIN_HYST_MV    300      /* the voltage budget grows again above the floor plus this */
#define EFF_PCT        90       /* assumed converter efficiency */
#define P_MARGIN       10       /* 1 W: where the voltage budget starts shrinking from, above what is really drawn */
#define RESERVE_MARGIN 3        /* a port drawing less than it wants (taper, ramp) is assumed to need what it draws plus this */
#define UNPARK_MIN     14       /* a parked port restarts only with this much available: no flicker at LIM_CAP_MIN */

lim_cfg_t XD lim_cfg;
lim_in_t  XD lim_in;
lim_out_t XD lim_out;

/* All working data is static in external RAM: direct RAM is full on this chip, and locals / parameters live there. */
static uint16_t XD P_v;                              /* the voltage floor's power budget: the one stored number besides the caps */
static uint8_t  XD tick, n_v;

void lim_reset(void)
{
    uint8_t k;
    P_v = LIM_P_BIG; tick = 0; n_v = 0;
    for (k = 0; k < NPORTS; k++) lim_out.cap[k] = 0;
}

/* Share a budget B (counts: the most the charging ports may draw together) over the charging ports.  The caps are written to
 * lim_out.cap; their previous values give the park hysteresis.
 *   ordered modes  in priority order: a port that fits keeps its current, the first that does not gets what is left, the rest
 *                  are parked.  Higher-priority ports reserve what they really draw, not what they want.
 *   equal          every port gets the same share; a port that wants less than the share frees the rest for the others. */
static uint16_t XD s_B, s_rem, s_share, s_W;
static uint8_t  XD s_order[NPORTS], s_was[NPORTS], s_open[NPORTS];
static uint8_t  XD s_n, s_k, s_a, s_b, s_t, s_w, s_r, s_changed, s_nopen;

static uint8_t give(uint16_t c, uint8_t was_parked)
{
    return c >= (was_parked ? UNPARK_MIN : LIM_CAP_MIN) ? (uint8_t)c : LIM_PARK;
}

static void share(void)
{
    s_n = 0; s_W = 0;
    for (s_k = 0; s_k < NPORTS; s_k++) {
        s_was[s_k] = lim_out.cap[s_k] == LIM_PARK;
        if (lim_in.charging[s_k]) { s_order[s_n++] = s_k; s_W += lim_in.want[s_k]; }
        else lim_out.cap[s_k] = 0;
    }
    if (s_B >= s_W) {                                    /* everything fits: no caps */
        for (s_a = 0; s_a < s_n; s_a++) lim_out.cap[s_order[s_a]] = 0;
        return;
    }

    if (lim_cfg.mode == LM_EQUAL) {
        s_rem = s_B; s_nopen = s_n;
        for (s_a = 0; s_a < s_n; s_a++) s_open[s_order[s_a]] = 1;
        do {                                             /* settle the ports that want less than the share */
            s_changed = 0; s_share = s_rem / s_nopen;
            for (s_a = 0; s_a < s_n; s_a++) {
                s_k = s_order[s_a];
                if (s_open[s_k] && lim_in.want[s_k] <= s_share) {
                    s_open[s_k] = 0; lim_out.cap[s_k] = 0; s_rem -= lim_in.want[s_k]; s_nopen--; s_changed = 1;
                }
            }
        } while (s_changed && s_nopen);
        s_share = s_nopen ? s_rem / s_nopen : 0;
        for (s_a = 0; s_a < s_n; s_a++) {
            s_k = s_order[s_a];
            if (s_open[s_k]) lim_out.cap[s_k] = give(s_share, s_was[s_k]);
        }
        return;
    }

    if (lim_cfg.mode != LM_BY_CH) {                      /* order by cell voltage (stable insertion sort, at most six) */
        for (s_a = 1; s_a < s_n; s_a++) {
            s_t = s_order[s_a];
            for (s_b = s_a; s_b > 0; s_b--) {
                uint16_t u = lim_in.v[s_order[s_b - 1]];
                if (lim_cfg.mode == LM_BY_VHIGH ? lim_in.v[s_t] <= u : lim_in.v[s_t] >= u) break;
                s_order[s_b] = s_order[s_b - 1];
            }
            s_order[s_b] = s_t;
        }
    }
    s_rem = s_B;
    for (s_a = 0; s_a < s_n; s_a++) {
        s_k = s_order[s_a]; s_w = lim_in.want[s_k];
        s_r = lim_in.i[s_k] + RESERVE_MARGIN < s_w ? (uint8_t)(lim_in.i[s_k] + RESERVE_MARGIN) : s_w;   /* what it really needs */
        if (s_rem >= s_w) lim_out.cap[s_k] = 0;                                                         /* fits: no cap */
        else {
            lim_out.cap[s_k] = give(s_rem, s_was[s_k]);
            if (lim_out.cap[s_k] == LIM_PARK) s_r = 0;
            else if (s_r > lim_out.cap[s_k]) s_r = lim_out.cap[s_k];
        }
        s_rem = s_rem > s_r ? (uint16_t)(s_rem - s_r) : 0;
    }
}

static uint8_t  XD p_k;
static uint16_t XD p_S, p_P;
static uint32_t XD p_sum_ma, p_est, p_vin_mv, p_rail_mv, p_pin, p_t;

void lim_pass(void)
{
    p_S = 0; p_sum_ma = 0; p_est = 0; p_vin_mv = cal_vin_mv(lim_in.vin); p_rail_mv = cal_port_mv(lim_in.rail);
    for (p_k = 0; p_k < NPORTS; p_k++) {
        if (!lim_in.charging[p_k]) continue;
        p_S += lim_in.i[p_k]; p_sum_ma += cal_port_ma(lim_in.i[p_k]);
    }
    if (p_vin_mv) p_est = p_rail_mv * p_sum_ma * 100u / ((uint32_t)EFF_PCT * p_vin_mv);       /* input current, mA */
    if (p_est > 65535u) p_est = 65535u;
    lim_out.est_ma = (uint16_t)p_est;
    lim_out.trip = LT_NONE;

    if (++tick < LIM_PERIOD) return;
    tick = 0;

    p_P = LIM_P_BIG;
    if (lim_cfg.ilim) {
        /* input current limit: input power = limit x input voltage (0.1 W); worked out from scratch, no stored state */
        p_t = (uint32_t)lim_cfg.ilim * p_vin_mv / 1000u;
        if (p_t < p_P) p_P = (uint16_t)p_t;
    }

    if (lim_cfg.vmin) {
        p_pin = p_est * p_vin_mv / 100000u;                                            /* what is drawn now, 0.1 W */
        if (p_vin_mv < (uint32_t)lim_cfg.vmin * 100u) {
            if (n_v < 255) n_v++;
            if (!lim_cfg.vcut) {                         /* smooth: the budget shrinks while the input is below the floor */
                if (P_v > p_pin + P_MARGIN) P_v = (uint16_t)(p_pin + P_MARGIN);        /* start from what is really drawn */
                s_w = (uint8_t)(P_v / 16 > 255 ? 255 : P_v / 16); if (s_w < 2) s_w = 2;
                P_v = P_v > s_w ? (uint16_t)(P_v - s_w) : 0;
            }
        } else {
            n_v = 0;                                     /* ... and grows back slowly above floor + margin */
            if (p_vin_mv > (uint32_t)lim_cfg.vmin * 100u + VIN_HYST_MV && P_v < LIM_P_BIG) P_v = (uint16_t)(P_v + 4);
        }
        if (!lim_cfg.vcut && P_v < p_P) p_P = P_v;
        if (lim_cfg.vcut && n_v >= TRIP_UPDATES) lim_out.trip = LT_VOLTAGE;         /* cut: below the floor for a second */
    } else { P_v = LIM_P_BIG; n_v = 0; }

    lim_out.budget = p_P;
    if (p_P >= LIM_P_BIG || !p_rail_mv) {                /* no limit active */
        for (p_k = 0; p_k < NPORTS; p_k++) lim_out.cap[p_k] = 0;
        return;
    }
    /* power budget -> port current budget: P x 90 % / rail voltage, in current counts (14.65 mA each) */
    p_t = (uint32_t)p_P * (EFF_PCT * 1000UL) / p_rail_mv;                              /* mA */
    p_t = p_t * 273u / 4000u;
    s_B = p_t > 1000u ? 1000u : (uint16_t)p_t;
    share();
}

uint8_t lim_ilim_next(uint8_t v)
{
    if (v == ILIM_OFF) return ILIM_MIN;
    if (v < 30) return (uint8_t)(v + 1);                 /* 0.1 A steps up to 3 A, 0.5 A steps above */
    return (uint8_t)(v + 5 > ILIM_MAX ? ILIM_MAX : v + 5);
}
uint8_t lim_ilim_prev(uint8_t v)
{
    if (v <= ILIM_MIN) return ILIM_OFF;
    if (v <= 30) return (uint8_t)(v - 1);
    return (uint8_t)(v - 5 < 30 ? 30 : v - 5);
}
uint8_t lim_vmin_next(uint8_t v)                         /* 0.1 V steps up to 6.0 V, then 0.5 V steps */
{
    if (v == VMIN_OFF) return VMIN_MIN;
    if (v < 60) return (uint8_t)(v + 1);
    return (uint8_t)(v + 5 > VMIN_MAX ? VMIN_MAX : v + 5);
}
uint8_t lim_vmin_prev(uint8_t v)
{
    if (v <= VMIN_MIN) return VMIN_OFF;
    if (v <= 60) return (uint8_t)(v - 1);
    return (uint8_t)(v - 5 < 60 ? 60 : v - 5);
}
