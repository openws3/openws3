/* Host tests for the input-current / input-voltage limits with the port controller on a tiny plant: the port current follows the duty,
 * the supply has a series resistance.  make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "limits.h"
#include "cal.h"
#include "chg.h"

#include "check.h"
#define IN(v) ilim_step(v, 1)
#define IP(v) ilim_step(v, 0)
#define VN(v) vmin_step(v, 1)
#define VP(v) vmin_step(v, 0)

#define RAIL 3836                          /* rail at the 4.20 V target */

static uint16_t vin_raw(uint32_t mv)       /* inverse of cal_vin_mv */
{
    uint16_t r = 0;
    while (r < 4095 && cal_vin_mv(r) < mv) r++;
    return r;
}

#define cfg lim_cfg
#define out lim_out

/* the bench: the real port controller (chg.c) on ports whose current is the duty times a gain, at most a taper value (a cell in its
 * constant-voltage taper); the cell voltages do not move.  Every pass: the limits on the last readings, then the controller. */
static chg_in_t cin;
static chg_out_t cout;
static double gain[NPORTS];                 /* iu per duty count */
static int taper[NPORTS], present[NPORTS], cellv[NPORTS];
static int amps[NPORTS], sum, over;          /* the port currents this pass, their sum, the most it has been above the budget */

static void setup(uint8_t n, uint8_t iset)
{
    uint8_t k;
    memset(&cfg, 0, sizeof cfg); memset(&lim_in, 0, sizeof lim_in); memset(&out, 0, sizeof out);
    memset(&cin, 0, sizeof cin); memset(&cout, 0, sizeof cout);
    lim_reset(); chg_init();
    lim_in.rail = RAIL; lim_in.vin = vin_raw(12000);
    cin.tgt = 3822; cin.setpoint = RAIL; cin.rail = RAIL; cin.volt = 0; cin.run = 1;
    for (k = 0; k < NPORTS; k++) {
        cin.iset[k] = iset; cin.en[k] = 1;
        present[k] = k < n; gain[k] = 2.0; taper[k] = 0; cellv[k] = 3368;     /* 3.70 V */
    }
    over = -1000;
}

static void step(void)
{
    uint8_t k;
    for (sum = 0, k = 0; k < NPORTS; k++) {
        double a = present[k] ? gain[k] * cout.duty[k] : 0;
        if (taper[k] && a > taper[k]) a = taper[k];
        amps[k] = (int)a; sum += amps[k];
        cin.v_now[k] = present[k] ? (uint16_t)cellv[k] : 0; cin.i_now[k] = (uint16_t)amps[k];
    }
    if (out.on && sum - (int)out.bcnt > over) over = sum - (int)out.bcnt;
    for (k = 0; k < NPORTS; k++) {
        lim_in.charging[k] = cout.state[k] == PS_CHARGE; lim_in.want[k] = cout.want[k];
        lim_in.v[k] = cout.v[k]; lim_in.i[k] = cout.i[k]; lim_in.up[k] = cout.up[k];
    }
    lim_pass();
    cin.lim = out.on; cin.rise = out.rise; cin.cut = out.cut; cin.excess = out.excess;
    chg_pass(&cin, &cout);
}

static void run(int passes) { while (passes--) step(); }

static void t_est(void)
{
    setup(3, 91);
    run(10000);                                  /* 3 x 1.33 A at 4.2 V from 12 V at 90 %: 1.56 A */
    CHECK(abs((int)out.est_ma - 1561) < 40);
    CHECK(!out.on && out.rise == 0xFF && out.cut == NPORTS && out.trip == LT_NONE);   /* no limits set: the gate is open */
    CHECK(out.budget == LIM_P_BIG);
}

static void t_equal(void)
{
    uint8_t k;
    setup(4, 91);
    cfg.ilim = 10; cfg.mode = LM_EQUAL;        /* 1.0 A of input, 12 V: a 12 W budget */
    run(30000);
    CHECK(out.budget == 120);
    CHECK(out.est_ma <= 1030 && out.est_ma >= 850);                         /* holds at the limit, not far below it */
    for (k = 1; k < 4; k++) CHECK(abs(amps[k] - amps[0]) <= 4 && amps[k] > 10);   /* even shares */
    CHECK(over <= LIM_ROOM);                                                /* never above the budget (by more than a count) */
    CHECK(cout.held[0] && amps[4] == 0 && amps[5] == 0);
    cfg.ilim = 0;                                                            /* limit removed: the gate opens, the ports go up */
    run(400);
    CHECK(!out.on && amps[0] >= 89 && amps[3] >= 89);

    /* a port that wants less than the share leaves the rest to the others */
    setup(3, 91);
    cin.iset[2] = 20;
    cfg.ilim = 10; cfg.mode = LM_EQUAL;
    run(30000);
    CHECK(abs(amps[2] - 20) <= 2 && amps[0] > 60 && abs(amps[0] - amps[1]) <= 4);
}

/* a budget too small for five ports at a usable current: they all get an even bit, the sum stays under the budget */
static void t_equal_few(void)
{
    uint8_t k;
    setup(5, 35);
    lim_in.vin = vin_raw(5040);
    cfg.ilim = 4; cfg.mode = LM_EQUAL;         /* 0.4 A at 5 V: 29 counts, 5.8 each for five ports */
    run(20000);
    CHECK(out.bcnt >= 28 && out.bcnt <= 30);
    for (k = 0; k < 5; k++) CHECK(amps[k] >= 3 && amps[k] <= 9 && cout.state[k] == PS_CHARGE);
    CHECK(over <= 5 && sum >= 20);                                           /* the filtered readings round down by up to 1 count each */
    cfg.ilim = 10;                             /* 72 counts: 14 each */
    run(20000);
    for (k = 0; k < 5; k++) CHECK(amps[k] >= 11 && amps[k] <= 17);
}

static void t_follows_input(void)
{
    setup(4, 91);
    cfg.ilim = 10; cfg.mode = LM_EQUAL;
    run(300);
    CHECK(out.budget == 120);
    lim_in.vin = vin_raw(20000);                                            /* a PD source moves to 20 V: the same input current is more power */
    run(300);
    CHECK(out.budget == 200);
}

static void t_by_channel(void)
{
    setup(4, 91);
    cfg.ilim = 15; cfg.mode = LM_BY_CH;        /* 1.5 A of input: room for about 2.9 ports at 12 V */
    run(40000);
    CHECK(out.est_ma <= 1550 && out.est_ma >= 1250);
    CHECK(amps[0] >= 89 && amps[1] >= 89);                                  /* ports 1 and 2 get their full current */
    CHECK(amps[2] >= amps[3] && amps[3] < 30);                              /* the last port is cut first */
    CHECK(over <= LIM_ROOM);
}

/* a port that wants more but cannot yet have the margin a rise needs waits, and no port behind it goes ahead meanwhile */
static void t_wait_in_line(void)
{
    uint8_t k;
    setup(3, 91);
    cfg.ilim = 15; cfg.mode = LM_BY_CH;
    lim_in.charging[0] = lim_in.charging[1] = lim_in.charging[2] = 1;
    lim_in.up[1] = 4; lim_in.up[2] = 1;                                     /* ports 2 and 3 want more: a count adds about 4 and 1 iu */
    for (k = 0; k < 3; k++) lim_in.i[k] = 20;
    lim_pass();
    for (k = 0; k < 3; k++) lim_in.i[k] = (uint16_t)((out.bcnt - 10) / 3);  /* 10 iu of room: port 3 would fit (3 + 3), port 2 needs 3 + 12 */
    lim_pass();
    CHECK(!(out.rise & 2) && !(out.rise & 4));
    lim_in.up[1] = 0;                                                       /* port 2 no longer waits: port 3 may go */
    lim_pass();
    CHECK(out.rise & 4);
}

static void t_by_voltage(void)
{
    setup(4, 91);
    cellv[0] = 3200; cellv[1] = 3400; cellv[2] = 3000; cellv[3] = 3600;
    cfg.ilim = 12; cfg.mode = LM_BY_VHIGH;     /* highest voltage keeps it: the 3000 cell goes first */
    run(40000);
    CHECK(amps[3] >= 89 && amps[3] >= amps[1] && amps[1] >= amps[0] && amps[0] >= amps[2]);
    CHECK(amps[2] < 30 && over <= LIM_ROOM);
}

/* the failure seen on the unit: with CH1 FIRST and the budget used up by ports 2..4, a low cell is put on port 1.  It has the priority
 * and far more current per duty count than the cells near full; it must take its current from port 4 without the sum ever going over
 * the budget.  The same in the other two orders. */
static void t_hotplug(void)
{
    uint8_t m;
    for (m = 0; m < LM_NMODES; m++) {
        setup(4, 91);
        present[0] = 0; gain[1] = gain[2] = gain[3] = 0.7;                 /* 10 mA per count */
        cfg.ilim = 12; cfg.mode = m;           /* 12 W: about 2.3 ports' worth */
        run(60000);
        CHECK(cout.state[0] == PS_IDLE && over <= LIM_ROOM);
        present[0] = 1; gain[0] = 2.0; cellv[0] = 3000;                    /* 3.3 V, much lower than the others: 3 x the current per count */
        over = -1000;
        run(40000);
        CHECK(over <= LIM_ROOM);                                            /* never above the budget on the way */
        if (m == LM_BY_CH) CHECK(amps[0] >= 89 && amps[3] < amps[1]);      /* port 1 got its full current, port 4 gave it up */
        if (m == LM_EQUAL) CHECK(abs(amps[0] - amps[1]) <= 4 && abs(amps[0] - amps[3]) <= 4);
        if (m == LM_BY_VHIGH) CHECK(amps[0] < amps[1]);                     /* the low cell comes last */
    }
}

static void t_cut(void)
{
    int n;
    setup(3, 91);                                                           /* the voltage floor in cut mode stops the run */
    cfg.vmin = 140; cfg.vcut = 1; cfg.mode = LM_EQUAL;
    lim_in.vin = vin_raw(6500);
    for (n = 0; n < 190; n++) { step(); CHECK(out.trip == LT_NONE && !out.on); }   /* not at once, and nothing is held */
    for (; n < 260; n++) { step(); if (out.trip) break; }
    CHECK(out.trip == LT_VOLTAGE && n < 260);                               /* after about a second */

    setup(3, 91);                                                           /* a brief dip does not stop the run */
    cfg.vmin = 140; cfg.vcut = 1; cfg.mode = LM_EQUAL;
    lim_in.vin = vin_raw(6500); run(150);
    lim_in.vin = vin_raw(12000);
    for (n = 0; n < 600; n++) { step(); CHECK(out.trip == LT_NONE); }

    setup(3, 91);                                                           /* the current limit never stops the run, it limits */
    cfg.ilim = 5; cfg.mode = LM_EQUAL;
    for (n = 0; n < 20000; n++) { step(); CHECK(out.trip == LT_NONE); }
    CHECK(out.est_ma < 520);
}

/* with CH1 FIRST, port 1 in its taper must free room for port 3, which waits until there is some */
static void t_taper(void)
{
    int n, started = 0;
    setup(3, 91);
    cfg.ilim = 10; cfg.mode = LM_BY_CH;                                     /* 1.0 A of input: about 1.9 ports */
    run(30000);
    CHECK(amps[0] >= 89 && cout.held[2] && amps[2] <= 2);                  /* port 3 waits */
    for (n = 0; n < 8000; n++) {
        taper[0] = 91 - n * 70 / 8000;                                      /* port 1's current decays from 91 to 21 */
        step();
        if (amps[2] > 10) started = 1;
    }
    run(3000);
    CHECK(started && amps[2] > 30);                                         /* port 3 started once there was room */
    CHECK(amps[1] >= 89);                                                   /* port 2 kept its current */
    CHECK(over <= LIM_ROOM);                                                /* the limit held all along */
}

/* a source with a series resistance: the input voltage sags with the load */
static void sag(uint32_t v0_mv, uint32_t rs_mohm)
{
    uint32_t drop = (uint32_t)out.est_ma * rs_mohm / 1000;
    lim_in.vin = vin_raw(v0_mv > drop ? v0_mv - drop : 1000);
}

static void t_vmin(void)
{
    int n;
    setup(3, 91);
    cfg.vmin = 140; cfg.mode = LM_EQUAL;        /* floor 7.0 V from a 9 V source with 2 ohm: at most 1.0 A (smooth mode) */
    for (n = 0; n < 30000; n++) { sag(9000, 2000); step(); }
    CHECK(cal_vin_mv(lim_in.vin) >= 6800);                                  /* the input is held near the floor, not far below it */
    CHECK(out.est_ma > 400);                                                /* and the ports still charge (no collapse) */
    CHECK(sum < 3 * 91);

    for (n = 0; n < 60000; n++) { sag(9000, 500); step(); }                /* a better source (0.5 ohm): the budget is given back */
    CHECK(amps[0] >= 89 && amps[1] >= 89 && amps[2] >= 89);
}

/* a 5 V supply that sags below a 4.9 V floor under load: the budget must come back when the input is back, even though the input at no
 * load (5.04 V) is only a little above the floor */
static void t_vmin_recover(void)
{
    int n;
    setup(3, 91);
    cfg.vmin = 98; cfg.mode = LM_EQUAL;
    for (n = 0; n < 30000; n++) { sag(5040, 1000); step(); }
    CHECK(out.budget < LIM_P_BIG);                                          /* it shrank under load */
    for (n = 0; n < 40000; n++) { lim_in.vin = vin_raw(5040); step(); }    /* the input stays up: the budget grows back */
    CHECK(out.budget > 200 && amps[0] >= 89 && amps[1] >= 89 && amps[2] >= 89);
}

static void t_both_and_steps(void)
{
    setup(2, 91);
    cfg.ilim = 8; cfg.vmin = 200; cfg.mode = LM_BY_CH;                      /* both limits at once: the smaller budget rules */
    lim_in.vin = vin_raw(9000);
    run(20000);
    CHECK(out.budget <= 72);                                                /* 0.8 A x 9 V = 7.2 W from the current limit (or less from the floor) */
    CHECK(amps[1] <= amps[0] && over <= LIM_ROOM);

    CHECK(IN(0) == 5 && IN(29) == 30 && IN(30) == 35 && IN(98) == 100 && IN(100) == 100);
    CHECK(IP(5) == 0 && IP(0) == 0 && IP(30) == 29 && IP(35) == 30 && IP(100) == 95);
    CHECK(VN(0) == 96 && VN(99) == 100 && VN(100) == 102 && VN(118) == 120 && VN(120) == 130 && VN(190) == 200 && VN(200) == 201 && VN(229) == 230 && VN(230) == 230);
    CHECK(VP(96) == 0 && VP(0) == 0 && VP(100) == 99 && VP(102) == 100 && VP(120) == 118 && VP(130) == 120 && VP(200) == 190 && VP(201) == 200 && VP(230) == 229);
    CHECK(VMIN_MV(96) == 4800 && VMIN_MV(200) == 10000 && VMIN_MV(201) == 10500 && VMIN_MV(230) == 25000);   /* 0.5 V per step everywhere above 6 V */
}

int main(void)
{
    t_est(); t_equal(); t_equal_few(); t_follows_input(); t_by_channel(); t_wait_in_line(); t_by_voltage(); t_hotplug(); t_cut(); t_taper(); t_vmin(); t_vmin_recover(); t_both_and_steps();
    return check_report("limits");
}
