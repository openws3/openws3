/* Host tests for the input-current / input-voltage limits against a tiny plant: port currents follow their caps, the supply
 * has a series resistance.  make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "limits.h"
#include "cal.h"
#include "chg.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define RAIL 3836                          /* 4.2 V mode-0 rail */

static uint16_t vin_raw(uint32_t mv)       /* inverse of cal_vin_mv */
{
    uint16_t r = 0;
    while (r < 4095 && cal_vin_mv(r) < mv) r++;
    return r;
}

#define cfg lim_cfg
#define in lim_in
#define out lim_out
static uint8_t taper[NPORTS];               /* 0 = none; else the most the cell takes (a port in its constant-voltage taper) */

static void setup(uint8_t n, uint8_t want)
{
    uint8_t k;
    memset(&cfg, 0, sizeof cfg); memset(&in, 0, sizeof in); memset(&out, 0, sizeof out); memset(taper, 0, sizeof taper);
    lim_reset();
    in.rail = RAIL; in.vin = vin_raw(12000);
    for (k = 0; k < NPORTS; k++) { in.charging[k] = k < n; in.want[k] = k < n ? want : 0; in.v[k] = 3500; }
}

/* the port current is its wanted current limited by its cap (and by its taper) */
static void plant(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) {
        uint8_t c = out.cap[k];
        in.i[k] = !in.charging[k] ? 0 : c == LIM_PARK ? 0 : (c && c < in.want[k]) ? c : in.want[k];
        if (taper[k] && in.i[k] > taper[k]) in.i[k] = taper[k];
    }
}

static void run(int passes) { while (passes--) { plant(); lim_pass(); } }

/* how permissive a cap is: none > counts > parked */
static int rank(uint8_t c) { return c == 0 ? 1000 : c == LIM_PARK ? -1 : c; }

static void t_est(void)
{
    CHECK(LIM_PARK == CAP_PARK);                 /* the two modules agree on the parked marker */
    setup(3, 91);
    in.i[0] = in.i[1] = in.i[2] = 91;
    lim_pass();                                  /* 3 x 1.33 A at 4.2 V from 12 V at 90 %: 1.56 A */
    CHECK(abs((int)out.est_ma - 1561) < 30);
    CHECK(out.cap[0] == 0 && out.cap[1] == 0 && out.cap[2] == 0 && out.trip == LT_NONE);   /* no limits set: nothing is capped */
    run(100);
    CHECK(out.budget == LIM_P_BIG);
}

static void t_equal(void)
{
    uint8_t k;
    setup(4, 91);
    cfg.ilim = 10; cfg.mode = LM_EQUAL;        /* 1.0 A of input, 12 V: a 12 W budget */
    run(600);
    CHECK(out.budget == 120);
    CHECK(out.est_ma <= 1030 && out.est_ma >= 850);                         /* holds at the limit, not far below it */
    for (k = 1; k < 4; k++) CHECK(abs((int)out.cap[k] - (int)out.cap[0]) <= 1 && out.cap[k] > LIM_PARK);   /* equal shares */
    CHECK(out.cap[4] == 0 && out.cap[5] == 0);                              /* idle ports are untouched */
    cfg.ilim = 0;                                                           /* limit removed: caps vanish at the next update */
    run(60);
    CHECK(out.cap[0] == 0 && out.cap[3] == 0);

    /* a port that wants less than the share frees the rest for the others */
    setup(3, 91);
    in.want[2] = 20;
    cfg.ilim = 10; cfg.mode = LM_EQUAL;
    run(300);
    CHECK(out.cap[2] == 0 && out.cap[0] > 60 && out.cap[0] == out.cap[1]);
}

/* a budget too small for an equal share per port: the highest-numbered ports wait, the others still charge */
static void t_equal_few(void)
{
    uint8_t k;
    int sum;
    setup(5, 35);
    in.vin = vin_raw(5040);
    cfg.ilim = 4; cfg.mode = LM_EQUAL;         /* 0.4 A at 5 V: 29 counts, 5.8 each for five ports: below the minimum of 7 */
    run(300);
    CHECK(out.bcnt >= 28 && out.bcnt <= 30);
    for (k = 0; k < 4; k++) CHECK(out.cap[k] >= LIM_CAP_MIN && out.cap[k] != LIM_PARK);
    CHECK(out.cap[4] == LIM_PARK);
    for (sum = 0, k = 0; k < NPORTS; k++) sum += in.i[k];
    CHECK(sum > 0 && sum <= out.bcnt);
    cfg.ilim = 5;                              /* 36 counts: 7.2 each would do for a running port, but not to restart the parked one */
    run(300);
    CHECK(out.cap[4] == LIM_PARK && out.cap[3] != LIM_PARK);
    cfg.ilim = 10;                             /* 72 counts: 14.4 each: everybody runs, the parked port restarts */
    run(300);
    for (k = 0; k < 5; k++) CHECK(out.cap[k] != LIM_PARK);
    setup(3, 35);                              /* wants below the share do not count against it: 20 counts for port 1 only */
    in.vin = vin_raw(5040); in.want[0] = 6; in.want[1] = 6; in.want[2] = 35;
    cfg.ilim = 4; cfg.mode = LM_EQUAL;
    run(300);
    CHECK(out.cap[0] == 0 && out.cap[1] == 0 && out.cap[2] != LIM_PARK);    /* the two small ports fit, the big one gets the rest */
}

static void t_follows_input(void)
{
    setup(4, 91);
    cfg.ilim = 10; cfg.mode = LM_EQUAL;
    run(300);
    CHECK(out.budget == 120);
    in.vin = vin_raw(20000);                                                /* a PD source moves to 20 V: the same input current is more power */
    run(300);
    CHECK(out.budget == 200);
}

static void t_by_channel(void)
{
    setup(4, 91);
    cfg.ilim = 15; cfg.mode = LM_BY_CH;        /* 1.5 A of input: room for about 2.9 ports at 12 V */
    run(800);
    CHECK(out.est_ma <= 1550 && out.est_ma >= 1250);
    CHECK(rank(out.cap[0]) >= rank(out.cap[1]) && rank(out.cap[1]) >= rank(out.cap[2]) && rank(out.cap[2]) >= rank(out.cap[3]));
    CHECK(out.cap[0] == 0);                                                 /* port 1 keeps its full current */
    CHECK(out.cap[3] != 0);                                                 /* the last port is cut first */
}

static void t_by_voltage(void)
{
    setup(4, 91);
    in.v[0] = 3500; in.v[1] = 3700; in.v[2] = 3300; in.v[3] = 3900;
    cfg.ilim = 12; cfg.mode = LM_BY_VLOW;      /* lowest voltage keeps its current longest: the 3900 cell goes first */
    run(800);
    CHECK(rank(out.cap[2]) >= rank(out.cap[0]) && rank(out.cap[0]) >= rank(out.cap[1]) && rank(out.cap[1]) >= rank(out.cap[3]));
    CHECK(out.cap[2] == 0 && out.cap[3] != 0);

    setup(4, 91);
    in.v[0] = 3500; in.v[1] = 3700; in.v[2] = 3300; in.v[3] = 3900;
    cfg.ilim = 12; cfg.mode = LM_BY_VHIGH;     /* highest voltage keeps it: the 3300 cell goes first */
    run(800);
    CHECK(rank(out.cap[3]) >= rank(out.cap[1]) && rank(out.cap[1]) >= rank(out.cap[0]) && rank(out.cap[0]) >= rank(out.cap[2]));
    CHECK(out.cap[3] == 0 && out.cap[2] != 0);
}

static void t_reorder_dwell(void)
{
    uint8_t c0, c2;
    setup(4, 91);
    in.v[0] = 3500; in.v[1] = 3700; in.v[2] = 3300; in.v[3] = 3900;
    cfg.ilim = 12; cfg.mode = LM_BY_VLOW;
    run(800);
    c0 = out.cap[0]; c2 = out.cap[2];
    in.v[0] = 3300; in.v[2] = 3500;                      /* the two swap places in voltage */
    run(5000);                                           /* 110 periods: too early */
    CHECK(out.cap[0] == c0 && out.cap[2] == c2);
    run(3000);                                           /* past ~30 s: the new order is taken up */
    CHECK(rank(out.cap[0]) >= rank(out.cap[2]) && out.cap[0] == c2 && out.cap[2] == c0);
}

static void t_cut(void)
{
    int n;
    setup(3, 91);                                                           /* the voltage floor in cut mode stops the run */
    cfg.vmin = 70; cfg.vcut = 1; cfg.mode = LM_EQUAL;
    in.vin = vin_raw(6500);
    for (n = 0; n < 200; n++) { plant(); lim_pass(); CHECK(out.trip == LT_NONE && out.cap[0] == 0); }   /* not at once, and nothing is throttled */
    for (; n < 260; n++) { plant(); lim_pass(); if (out.trip) break; }
    CHECK(out.trip == LT_VOLTAGE && n < 260);                               /* after about a second */

    setup(3, 91);                                                           /* a brief dip does not stop the run */
    cfg.vmin = 70; cfg.vcut = 1; cfg.mode = LM_EQUAL;
    in.vin = vin_raw(6500); run(150);
    in.vin = vin_raw(12000);
    for (n = 0; n < 600; n++) { plant(); lim_pass(); CHECK(out.trip == LT_NONE); }

    setup(3, 91);                                                           /* the current limit never stops the run, it limits */
    cfg.ilim = 5; cfg.mode = LM_EQUAL;
    for (n = 0; n < 1000; n++) { plant(); lim_pass(); CHECK(out.trip == LT_NONE); }
    CHECK(out.est_ma < 650);
}

/* the failure seen on the unit: with CH1 FIRST, port 1 in its taper must free room for port 2, and port 3 must start when there is
 * room for it, and a port that has started must not be parked again while port 1 keeps tapering */
static void t_taper(void)
{
    int n, flips = 0, started = 0;
    uint8_t was;
    setup(3, 91);
    cfg.ilim = 10; cfg.mode = LM_BY_CH;                                     /* 1.0 A of input: about 1.9 ports */
    run(300);
    CHECK(out.cap[0] == 0 && out.cap[2] == LIM_PARK);                       /* port 3 waits */
    was = out.cap[2];
    for (n = 0; n < 4000; n++) {
        taper[0] = (uint8_t)(91 - n * 70 / 4000);                           /* port 1's current decays from 91 to 21 */
        plant(); lim_pass();
        if (out.cap[2] != LIM_PARK) started = 1;
        if (started && out.cap[2] == LIM_PARK && was != LIM_PARK) flips++;
        was = out.cap[2];
    }
    CHECK(started);                                                         /* port 3 started once there was room */
    CHECK(flips == 0);                                                      /* and was not parked again */
    CHECK(out.cap[1] == 0 || out.cap[1] > LIM_PARK);                        /* port 2 is not parked */
    CHECK(out.est_ma <= 1650);                                              /* the limit still holds */
}

/* a source with a series resistance: the input voltage sags with the load */
static void sag(uint32_t v0_mv, uint32_t rs_mohm)
{
    uint32_t drop = (uint32_t)out.est_ma * rs_mohm / 1000;
    in.vin = vin_raw(v0_mv > drop ? v0_mv - drop : 1000);
}

static void t_vmin(void)
{
    int n; uint32_t sum;
    uint8_t k;
    setup(3, 91);
    cfg.vmin = 70; cfg.mode = LM_EQUAL;        /* floor 7.0 V from a 9 V source with 2 ohm: at most 1.0 A (smooth mode) */
    for (n = 0; n < 1500; n++) { sag(9000, 2000); plant(); lim_pass(); }
    CHECK(cal_vin_mv(in.vin) >= 6800);                                      /* the input is held near the floor, not far below it */
    CHECK(out.est_ma > 400);                                                /* and the ports still charge (no collapse) */
    for (sum = 0, k = 0; k < 3; k++) sum += in.i[k];
    CHECK(sum < 3u * 91u);

    in.charging[0] = in.charging[1] = in.charging[2] = 0;                   /* all done: load gone, then three new cells */
    run(50);
    in.charging[0] = in.charging[1] = in.charging[2] = 1; in.want[0] = in.want[1] = in.want[2] = 91;
    for (n = 0; n < 3000; n++) { sag(9000, 500); plant(); lim_pass(); }    /* a better source (0.5 ohm): budget is given back */
    CHECK(out.cap[0] == 0 && out.cap[1] == 0 && out.cap[2] == 0);
}

/* the failure seen on the unit: a 5 V supply sagged below a 4.9 V floor under load, the voltage budget shrank, and it never came back
 * because the input at no load (5.04 V) was below floor + the regrow margin */
static void t_vmin_recover(void)
{
    int n;
    setup(3, 91);
    cfg.vmin = 49; cfg.mode = LM_EQUAL;
    for (n = 0; n < 1500; n++) { sag(5040, 1000); plant(); lim_pass(); }
    CHECK(out.budget < LIM_P_BIG);                                          /* it shrank under load */
    in.vin = vin_raw(5040);                                                 /* the ports are parked: no load, the input is back at 5.04 V */
    run(2000);
    CHECK(out.budget > 200 && out.cap[0] == 0 && out.cap[1] == 0 && out.cap[2] == 0);     /* the budget grew back until nothing is capped */
}

static void t_both_and_steps(void)
{
    setup(2, 91);
    cfg.ilim = 8; cfg.vmin = 100; cfg.mode = LM_BY_CH;                      /* both limits at once: the smaller budget rules */
    in.vin = vin_raw(9000);
    run(400);
    CHECK(out.budget <= 72);                                                /* 0.8 A x 9 V = 7.2 W from the current limit (or less from the floor) */
    CHECK(out.cap[0] == 0 || out.cap[0] > LIM_PARK);
    CHECK(rank(out.cap[1]) <= rank(out.cap[0]));

    CHECK(lim_ilim_next(0) == 5 && lim_ilim_next(29) == 30 && lim_ilim_next(30) == 35 && lim_ilim_next(98) == 100 && lim_ilim_next(100) == 100);
    CHECK(lim_ilim_prev(5) == 0 && lim_ilim_prev(0) == 0 && lim_ilim_prev(30) == 29 && lim_ilim_prev(35) == 30 && lim_ilim_prev(100) == 95);
    CHECK(lim_vmin_next(0) == 48 && lim_vmin_next(59) == 60 && lim_vmin_next(60) == 65 && lim_vmin_next(250) == 250 && lim_vmin_next(248) == 250);
    CHECK(lim_vmin_prev(48) == 0 && lim_vmin_prev(0) == 0 && lim_vmin_prev(60) == 59 && lim_vmin_prev(65) == 60 && lim_vmin_prev(250) == 245);
}

int main(void)
{
    t_est(); t_equal(); t_equal_few(); t_follows_input(); t_by_channel(); t_by_voltage(); t_reorder_dwell(); t_cut(); t_taper(); t_vmin(); t_vmin_recover(); t_both_and_steps();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
