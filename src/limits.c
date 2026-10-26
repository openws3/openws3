#include "limits.h"
#include "cal.h"

#define LIM_PERIOD     22       /* the caps are recomputed every 22 passes (~100 ms) */
#define TRIP_UPDATES   10       /* cut mode: below the floor for this many updates (~1 s) before the run is stopped */
#define VIN_HYST_MV    100      /* the voltage budget grows again above the floor plus this: small enough for a 5 V supply that is
                                   only a little above a 4.9 V floor when unloaded */
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
    lim_out.p_cur = lim_out.p_vol = lim_out.budget = LIM_P_BIG; lim_out.bcnt = 0; lim_out.est_ma = 0; lim_out.trip = LT_NONE;
}

/* Share a budget B (counts: the most the charging ports may draw together) over the charging ports.  The caps are written to
 * lim_out.cap; their previous values give the park hysteresis.
 *   ordered modes  in priority order: a port that fits keeps its current, the first that does not gets what is left, the rest
 *                  are parked.  Higher-priority ports reserve what they really draw, not what they want.
 *   equal          every port gets the same share; a port that wants less than the share frees the rest for the others.  When the
 *                  share is too small to drive, the highest-numbered ports are parked, as few as it takes for the rest to get a usable one. */
static uint16_t XD s_B, s_rem, s_share, s_W;
static uint8_t  XD s_order[NPORTS], s_was[NPORTS], s_open[NPORTS];
static uint8_t  XD s_n, s_k, s_a, s_b, s_t, s_w, s_r, s_changed, s_nopen, s_m, s_fit;

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
        for (s_m = s_n; s_m; s_m--) {                    /* serve the first s_m ports (the order is by port number) */
            s_rem = s_B; s_nopen = s_m;
            for (s_a = 0; s_a < s_m; s_a++) s_open[s_order[s_a]] = 1;
            do {                                         /* settle the ports that want less than the share */
                s_changed = 0; s_share = s_rem / s_nopen;
                for (s_a = 0; s_a < s_m; s_a++) {
                    s_k = s_order[s_a];
                    if (s_open[s_k] && lim_in.want[s_k] <= s_share) {
                        s_open[s_k] = 0; lim_out.cap[s_k] = 0; s_rem -= lim_in.want[s_k]; s_nopen--; s_changed = 1;
                    }
                }
            } while (s_changed && s_nopen);
            s_share = s_nopen ? s_rem / s_nopen : 0;
            s_fit = 1;                                   /* every capped port must get a share it can be driven with */
            for (s_a = 0; s_a < s_m; s_a++) {
                s_k = s_order[s_a];
                if (s_open[s_k] && give(s_share, s_was[s_k]) == LIM_PARK) s_fit = 0;
            }
            if (s_fit) break;
        }
        for (s_a = 0; s_a < s_n; s_a++) {
            s_k = s_order[s_a];
            if (s_a >= s_m) lim_out.cap[s_k] = LIM_PARK;
            else if (s_open[s_k]) lim_out.cap[s_k] = give(s_share, s_was[s_k]);
        }
        return;
    }

    if (lim_cfg.mode == LM_BY_VHIGH) {                   /* highest voltage first (stable insertion sort, at most six).  Stable by
                                                            itself: a charging cell reads higher and keeps its place */
        for (s_a = 1; s_a < s_n; s_a++) {
            s_t = s_order[s_a];
            for (s_b = s_a; s_b > 0 && lim_in.v[s_t] > lim_in.v[s_order[s_b - 1]]; s_b--) s_order[s_b] = s_order[s_b - 1];
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
static uint16_t XD p_P;
static uint32_t XD p_sum_ma, p_est, p_vin_mv, p_rail_mv, p_pin, p_t;

void lim_pass(void)
{
    lim_out.trip = LT_NONE;
    if (++tick < LIM_PERIOD) return;
    tick = 0;

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

    if (lim_cfg.vmin) {
        p_pin = p_est * p_vin_mv / 100000u;                                            /* what is drawn now, 0.1 W */
        if (p_vin_mv < (uint32_t)lim_cfg.vmin * 50u) {
            if (n_v < 255) n_v++;
            if (!lim_cfg.vcut) {                         /* smooth: the budget shrinks while the input is below the floor */
                if (P_v > p_pin + P_MARGIN) P_v = (uint16_t)(p_pin + P_MARGIN);        /* start from what is really drawn */
                s_w = (uint8_t)(P_v / 16 > 255 ? 255 : P_v / 16); if (s_w < 2) s_w = 2;
                P_v = P_v > s_w ? (uint16_t)(P_v - s_w) : 0;
            }
        } else {
            n_v = 0;                                     /* ... and grows back slowly above floor + margin */
            if (p_vin_mv > (uint32_t)lim_cfg.vmin * 50u + VIN_HYST_MV && P_v < LIM_P_BIG) P_v = (uint16_t)(P_v + 4);
        }
        if (!lim_cfg.vcut) lim_out.p_vol = P_v;
        if (!lim_cfg.vcut && P_v < p_P) p_P = P_v;
        if (lim_cfg.vcut && n_v >= TRIP_UPDATES) lim_out.trip = LT_VOLTAGE;         /* cut: below the floor for a second */
    } else { P_v = LIM_P_BIG; n_v = 0; }

    lim_out.budget = p_P;
    if (p_P >= LIM_P_BIG || !p_rail_mv) {                /* no limit active */
        lim_out.bcnt = 0;
        for (p_k = 0; p_k < NPORTS; p_k++) lim_out.cap[p_k] = 0;
        return;
    }
    /* power budget -> port current budget: P x 90 % / rail voltage, in current counts (14.65 mA each) */
    p_t = (uint32_t)p_P * (EFF_PCT * 1000UL) / p_rail_mv;                              /* mA */
    p_t = p_t * 273u / 4000u;
    s_B = p_t > 1000u ? 1000u : (uint16_t)p_t;
    lim_out.bcnt = s_B;
    share();
}

/* 0 (off), then min .. knee in steps of 1, then steps of 5 up to max (clamped, no wrap) */
uint8_t lim_step(uint8_t v, uint8_t up, uint8_t min, uint8_t knee, uint8_t max)
{
    if (up) {
        if (v == 0) return min;
        if (v < knee) return (uint8_t)(v + 1);
        return (uint8_t)(v + 5 > max ? max : v + 5);
    }
    if (v <= min) return 0;
    if (v <= knee) return (uint8_t)(v - 1);
    return (uint8_t)(v - 5 < knee ? knee : v - 5);
}

uint8_t vmin_step(uint8_t v, uint8_t up)
{
    if (up) {
        if (v == 0) return VMIN_MIN;
        if (v < VMIN_FINE) return (uint8_t)(v + 1);
        if (v < VMIN_COARSE) return (uint8_t)(v + 2);
        return (uint8_t)(v + 10 > VMIN_MAX ? VMIN_MAX : v + 10);
    }
    if (v <= VMIN_MIN) return 0;
    if (v <= VMIN_FINE) return (uint8_t)(v - 1);
    if (v <= VMIN_COARSE) return (uint8_t)(v - 2);
    return (uint8_t)(v - 10 < VMIN_COARSE ? VMIN_COARSE : v - 10);
}

const char *lim_mode_name(uint8_t m)
{
    static const CODE char *const NAME[LM_NMODES] = { "EQUAL", "CH1 FIRST", "HIGH V FIRST" };
    return NAME[m < LM_NMODES ? m : LM_EQUAL];
}
