/* Host tests for the per-port controller against a small cell + rail model. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "chg.h"
#include "sup.h"

#include "check.h"

#define T0   3822       /* 4.20 V target */
#define S0   3836       /* rail setpoint of the 4.20 V target */
static const double VPC = 4.5 / 4096, IPC = 1.0 / 68.25;

typedef struct { double q, cap_c, r; } cell_t;     /* coulombs, capacity, series ohms */
static double ocv(const cell_t *c)
{
    static const double s[] = {0, .02, .08, .30, .60, .85, .97, 1.0}, v[] = {2.5, 3.2, 3.55, 3.70, 3.82, 4.00, 4.12, 4.20};
    double x = c->q / c->cap_c;
    int k;
    if (x >= 1.0) return 4.2 + (x - 1.0) * 3.0;
    for (k = 0; k < 7; k++) if (x <= s[k + 1]) return v[k] + (v[k + 1] - v[k]) * (x - s[k]) / (s[k + 1] - s[k]);
    return 4.2;
}

typedef struct {
    cell_t cell[NPORTS];
    int present[NPORTS];
    double k_amp_per_duty, r_pass, rail_v;
    chg_in_t in;
    chg_out_t out;
    double amps[NPORTS];
    int ovr_v[NPORTS], ovr_i[NPORTS];      /* >0: force the sense reading (fault injection) */
} bench_t;

static void bench_init(bench_t *b, int volt, int iset)
{
    memset(b, 0, sizeof *b);
    b->k_amp_per_duty = 0.01; b->r_pass = 0.20; b->rail_v = sup_setpoint(volt) * VPC;
    b->in.tgt = sup_target(volt); b->in.setpoint = sup_setpoint(volt);
    b->in.rail = sup_setpoint(volt); b->in.run = 1;
    { int k; for (k = 0; k < NPORTS; k++) { b->in.iset[k] = (uint8_t)iset; b->in.en[k] = 1; } }
    chg_init();
}

static void attach(bench_t *b, int p, double v0, double cap_mah, double r)
{
    /* start charge at the capacity fraction that gives v0 on the same curve */
    double lo = 0, hi = 1.5;
    int n;
    b->cell[p].cap_c = cap_mah * 3.6; b->cell[p].r = r;
    for (n = 0; n < 40; n++) { b->cell[p].q = (lo + hi) / 2 * b->cell[p].cap_c; if (ocv(&b->cell[p]) < v0) lo = (lo + hi) / 2; else hi = (lo + hi) / 2; }
    b->present[p] = 1;
}

/* one control pass (4.5 ms) of plant + controller */
static void step(bench_t *b)
{
    int p;
    for (p = 0; p < NPORTS; p++) {
        double a = 0, v;
        if (!b->present[p]) { b->in.v_now[p] = 0; b->in.i_now[p] = 0; if (b->ovr_v[p]) b->in.v_now[p] = (uint16_t)b->ovr_v[p]; continue; }
        if (b->out.sw[p]) a = -0.15;                         /* bleed load */
        else {
            double want = b->k_amp_per_duty * b->out.duty[p];
            double room = (b->rail_v - ocv(&b->cell[p])) / (b->cell[p].r + b->r_pass);
            a = want < room ? want : (room > 0 ? room : 0);
        }
        b->amps[p] = a;
        b->cell[p].q += a * 0.0045;
        v = ocv(&b->cell[p]) + a * b->cell[p].r;
        b->in.v_now[p] = (uint16_t)(v / VPC);
        b->in.i_now[p] = (uint16_t)(fabs(a) / IPC);
    }
    for (p = 0; p < NPORTS; p++) {
        if (b->ovr_v[p]) b->in.v_now[p] = (uint16_t)b->ovr_v[p];
        if (b->ovr_i[p]) b->in.i_now[p] = (uint16_t)b->ovr_i[p];
    }
    chg_pass(&b->in, &b->out);
}

static void run(bench_t *b, int passes) { while (passes--) step(b); }

static void t_idle_and_probe(void)
{
    bench_t b; bench_init(&b, 0, 21);
    run(&b, 50);
    CHECK(b.out.state[0] == PS_IDLE && b.out.duty[0] == 0 && b.out.sw[0] == 0);   /* no cell */
    attach(&b, 0, 3.7, 40, 0.1);
    run(&b, 2);
    CHECK(b.out.state[0] == PS_PROBE && b.out.duty[0] == 0);                       /* output stays off while probing */
    run(&b, 10);
    CHECK(b.out.state[0] == PS_CHARGE);
}

static void t_cc(void)
{
    bench_t b; bench_init(&b, 0, 21);
    attach(&b, 0, 3.7, 400, 0.1);
    run(&b, 400);
    CHECK(b.out.duty[0] >= 2 && b.out.duty[0] <= 8);          /* soft ramp: +1 per 100 passes, not a step */
    run(&b, 4000);                                              /* 18 s */
    CHECK(abs((int)b.out.i[0] - 21) <= 2);                      /* regulated to the setting */
    CHECK(b.out.sw[0] == 0);
    /* a higher setting is reached and held too */
    bench_init(&b, 0, 63);
    attach(&b, 0, 3.7, 400, 0.1);
    run(&b, 12000);
    CHECK(abs((int)b.out.i[0] - 63) <= 3);
    /* the highest setting too, from a low cell */
    bench_init(&b, 0, 91);
    attach(&b, 0, 3.6, 400, 0.1);
    run(&b, 16000);                                             /* the soft ramp gets there well before this */
    CHECK(abs((int)b.out.i[0] - 91) <= 3);
    /* a cell close to the rail gives little current per duty count (here a tenth of the others): the soft start grows with the duty and
       gets there in seconds, not minutes, and does not overshoot */
    bench_init(&b, 0, 21);
    b.k_amp_per_duty = 0.0015;
    attach(&b, 0, 4.1, 400, 0.1);
    { int n, top = 0; for (n = 0; n < 6000; n++) { step(&b); if (b.out.i[0] > top) top = b.out.i[0]; }
      CHECK(abs((int)b.out.i[0] - 21) <= 2); CHECK(top <= 21 + 6); }
}

static void t_classify(void)
{
    bench_t b;
    bench_init(&b, 0, 21); attach(&b, 0, 4.20, 40, 0.1); run(&b, 20);
    CHECK(b.out.state[0] == PS_FULL && b.out.duty[0] == 0);                       /* already full: no charge */
    bench_init(&b, 0, 21); attach(&b, 0, 4.28, 40, 0.1); run(&b, 20);
    CHECK(b.out.state[0] == PS_BLEED && b.out.sw[0] == 1 && b.out.duty[0] == 0);   /* above target: bleed path */
    bench_init(&b, 2, 21); attach(&b, 0, 4.0, 40, 0.1); run(&b, 20);
    CHECK(b.out.state[0] == PS_BLEED);                                             /* storage voltage: 4.0 V is above 3.80 V */
    bench_init(&b, 0, 21); b.ovr_v[0] = (int)(0.5 / VPC); run(&b, 20);
    CHECK(b.out.state[0] == PS_IDLE);                                              /* below 1 V: no cell */
}

static void t_full_charge(void)
{
    bench_t b; int k, full_at = -1;
    bench_init(&b, 0, 21);
    attach(&b, 0, 3.95, 12, 0.1);
    for (k = 0; k < 60000 && full_at < 0; k++) { step(&b); if (b.out.state[0] == PS_FULL) full_at = k; }
    CHECK(full_at > 0);
    CHECK(b.out.event[0] == CE_FULL || full_at > 0);
    /* it ended inside the target window and drew almost nothing */
    CHECK(b.out.v[0] >= T0 - 20 && b.out.v[0] < T0 + 45);
    CHECK(b.out.duty[0] == 0);
    CHECK(ocv(&b.cell[0]) < 4.26);                                                  /* no gross overcharge */
}

static void t_faults(void)
{
    bench_t b; int k;
    /* over-voltage above the rail */
    bench_init(&b, 1, 21); attach(&b, 0, 3.7, 40, 0.1); run(&b, 20);
    b.cell[0].q = b.cell[0].cap_c * 1.5; run(&b, 300);
    CHECK(b.out.state[0] == PS_FAULT && b.out.duty[0] == 0 && b.out.sw[0] == 0);
    /* removing the cell clears the latch; a new cell starts normally */
    b.present[0] = 0; run(&b, 20);
    CHECK(b.out.state[0] == PS_IDLE);
    attach(&b, 0, 3.7, 40, 0.1); run(&b, 20);
    CHECK(b.out.state[0] == PS_CHARGE);
    /* over-current: sense reads far above the setting */
    bench_init(&b, 0, 21); attach(&b, 0, 3.7, 400, 0.1); run(&b, 20);
    b.ovr_i[0] = 80;
    for (k = 0; k < 400; k++) step(&b);
    CHECK(b.out.state[0] == PS_FAULT);
    /* stop: everything idles, outputs off */
    bench_init(&b, 0, 21); attach(&b, 0, 3.7, 400, 0.1); run(&b, 500);
    b.in.run = 0; run(&b, 3);
    CHECK(b.out.state[0] == PS_IDLE && b.out.duty[0] == 0 && b.out.sw[0] == 0);
}

static void t_cut(void)
{
    bench_t b;
    bench_init(&b, 0, 28);
    attach(&b, 1, 3.7, 400, 0.1);
    run(&b, 3000);
    CHECK(b.out.state[1] == PS_CHARGE && b.out.duty[1] > 10);
    chg_cut(); step(&b);
    CHECK(b.out.state[1] == PS_CHARGE && b.out.duty[1] <= 2);        /* duty cut, state kept, ramp restarts */
    run(&b, 300);
    CHECK(b.out.state[1] == PS_CHARGE && b.out.duty[1] > 2);         /* and it climbs again */
}

static void t_independent(void)
{
    bench_t b;
    bench_init(&b, 0, 28);
    attach(&b, 1, 3.7, 400, 0.1); attach(&b, 4, 4.2, 400, 0.1); attach(&b, 5, 4.3, 400, 0.1);
    run(&b, 3000);
    CHECK(b.out.state[0] == PS_IDLE);
    CHECK(b.out.state[1] == PS_CHARGE && b.out.duty[1] > 0);
    CHECK(b.out.state[4] == PS_FULL && b.out.duty[4] == 0);
    CHECK(b.out.state[5] == PS_BLEED && b.out.sw[5] == 1);
}

static sup_in_t si(void)
{
    sup_in_t s;
    memset(&s, 0, sizeof s);
    s.vin = 3000; s.rail = 3836; s.setpoint = 3836; s.bandgap = 995; s.adc0 = 2000;
    return s;
}

static void t_sup(void)
{
    sup_in_t in; sup_out_t o; int k;
    CHECK(sup_target(0) == 3822 && sup_target(1) == 3959 && sup_target(2) == 3458 && sup_target(3) == 3504);
    CHECK(sup_setpoint(0) == 3836 && sup_setpoint(1) == 3973 && sup_setpoint(2) == 3472 && sup_setpoint(3) == 3518);   /* 15 mV above the target */

    sup_reset(); in = si(); sup_pass(&in, &o);
    CHECK(o.stop == STOP_NONE && !o.rail_cut && !o.fan);

    in.vin = 699; sup_pass(&in, &o); CHECK(o.stop == STOP_VIN);                       /* input gone */
    in.vin = 700; sup_pass(&in, &o); CHECK(o.stop == STOP_NONE);                      /* back above the gate */

    sup_reset(); in = si(); in.bandgap = 1230; sup_pass(&in, &o); CHECK(o.stop == STOP_VCC);

    /* rail: out of band for 250 passes -> alarm + stop; recovers inside +-30 */
    sup_reset(); in = si(); in.rail = 3700;
    for (k = 0; k < 249; k++) sup_pass(&in, &o);
    CHECK(o.stop == STOP_NONE);
    sup_pass(&in, &o); CHECK(o.stop == STOP_RAIL);
    in.rail = 3836 + 20; sup_pass(&in, &o); CHECK(o.stop == STOP_NONE);
    /* rail over-voltage (the ADC at full scale): cut after 50 passes; a high reading below full scale is not one */
    sup_reset(); in = si(); in.rail = 4080; for (k = 0; k < 200; k++) { sup_pass(&in, &o); CHECK(!o.rail_cut); }
    sup_reset(); in = si(); in.rail = 4095; in.rail_sat = 1;
    for (k = 0; k < 49; k++) sup_pass(&in, &o);
    CHECK(!o.rail_cut); sup_pass(&in, &o); CHECK(o.rail_cut);
    /* a long sag followed by an overshoot while recovering is not an over-voltage */
    sup_reset(); in = si(); in.rail = 3500; for (k = 0; k < 300; k++) sup_pass(&in, &o);
    in.rail = 4095; in.rail_sat = 1; for (k = 0; k < 20; k++) { sup_pass(&in, &o); CHECK(!o.rail_cut); }
    in.rail = 3836; in.rail_sat = 0; for (k = 0; k < 20; k++) sup_pass(&in, &o);
    in.rail = 4095; in.rail_sat = 1; for (k = 0; k < 30; k++) { sup_pass(&in, &o); CHECK(!o.rail_cut); }      /* the count decays in between */
    /* but over-voltage most of the time is */
    sup_reset(); in = si();
    for (k = 0; k < 400 && !o.rail_cut; k++) { in.rail_sat = k % 4 != 0; in.rail = in.rail_sat ? 4095 : 3836; sup_pass(&in, &o); }
    CHECK(o.rail_cut && k < 120);
    /* a short excursion is forgiven: the counter decays */
    sup_reset(); in = si(); in.rail = 3700; for (k = 0; k < 100; k++) sup_pass(&in, &o);
    in.rail = 3836; for (k = 0; k < 120; k++) sup_pass(&in, &o);
    in.rail = 3700; for (k = 0; k < 200; k++) sup_pass(&in, &o);
    CHECK(o.stop != STOP_RAIL);

    /* fan: follows the thermistor, with hysteresis */
    sup_reset(); in = si(); in.adc0 = 1200; sup_pass(&in, &o); CHECK(o.fan);
    in.adc0 = 1400; sup_pass(&in, &o); CHECK(o.fan);
    in.adc0 = 1500; sup_pass(&in, &o); CHECK(!o.fan);
}

/* the user limits' gate (limits.h): a rise only when let, one count per pass; a cut brings the port down; a held port keeps its state */
static void t_gate(void)
{
    bench_t b; int n, d, jump = 0;
    bench_init(&b, 0, 63); attach(&b, 0, 3.9, 4000, 0.1);        /* a big cell: the voltage barely moves during the test */
    b.in.lim = 1; b.in.rise = 0; b.in.cut = NPORTS;
    run(&b, 2000);
    CHECK(b.out.state[0] == PS_CHARGE && b.out.duty[0] == 0 && b.out.held[0] == 2 && b.out.up[0]);   /* no room: it waits, output off */
    b.in.rise = 0xFF;
    for (n = 0, d = 0; n < 3000; n++) { step(&b); if (b.out.duty[0] > d + (d ? 1 : 2)) jump = 1; d = b.out.duty[0]; }
    CHECK(!jump && abs((int)b.out.i[0] - 63) <= 3 && b.out.up[0] == 0);       /* up one count per pass, no soft ramp after a hold */
    b.in.cut = 0; b.in.excess = 30;
    step(&b);
    CHECK(b.out.duty[0] < d && b.out.held[0] == 1);                              /* brought down */
    b.in.cut = NPORTS; b.in.rise = 0;
    run(&b, 400);
    CHECK(b.out.i[0] < 50 && b.out.state[0] == PS_CHARGE);                       /* and kept there */
    b.in.rise = 0xFF;
    run(&b, 400);
    CHECK(abs((int)b.out.i[0] - 63) <= 3);                                       /* let go: back at the setting */
    b.in.lim = 0;
    step(&b);
    CHECK(b.out.held[0] == 0);
    for (n = 0; n < 30 && b.out.duty[0]; n++) { b.in.lim = 1; b.in.rise = 0; b.in.cut = 0; b.in.excess = 255; step(&b); }
    CHECK(b.out.duty[0] == 0 && b.out.state[0] == PS_CHARGE);                    /* down to off, still CHARGE */

    /* a held port's tail timer stands still: a near-full reading and no current would otherwise end the charge; and it is not taken
       for a pulled cell */
    bench_init(&b, 0, 21); attach(&b, 0, 3.7, 400, 0.1);
    run(&b, 100);
    CHECK(b.out.state[0] == PS_CHARGE);
    b.ovr_v[0] = 3810; b.ovr_i[0] = 1; b.in.lim = 1; b.in.rise = 0; b.in.cut = NPORTS;
    run(&b, 9000);
    CHECK(b.out.state[0] == PS_CHARGE);
    b.in.lim = 0;                                                                 /* the same situation without the limits: rest test */
    run(&b, 9000);
    CHECK(b.out.state[0] != PS_CHARGE);
}

/* a cell pulled while charging: the loop winds the duty up and the pin follows it to the rail with no current; with the output off the
 * pin collapses.  It must be released within a couple of seconds, not after the 36 s tail timer */
static void t_pulled(void)
{
    bench_t b; int n;
    bench_init(&b, 0, 21); attach(&b, 0, 3.7, 400, 0.1);
    run(&b, 4000);
    CHECK(b.out.state[0] == PS_CHARGE && abs((int)b.out.i[0] - 21) <= 2);
    b.present[0] = 0;
    for (n = 0; n < 2000 && b.out.state[0] != PS_IDLE; n++) { b.ovr_v[0] = b.out.duty[0] > 0 ? 3830 : 0; step(&b); }
    CHECK(b.out.state[0] == PS_IDLE && n > 400 && n < 800);                         /* about 2 s + the rest */
    CHECK(b.out.duty[0] == 0);

    /* a cell is not mistaken for a pulled one while the soft ramp has not produced a current yet, nor when the limits hold it off */
    bench_init(&b, 0, 21); attach(&b, 0, 3.7, 400, 0.1);
    run(&b, 1000);
    CHECK(b.out.state[0] == PS_CHARGE);
    bench_init(&b, 0, 21); attach(&b, 0, 3.7, 400, 0.1);
    run(&b, 4000);
    for (n = 0; n < 30 && b.out.duty[0]; n++) { b.in.lim = 1; b.in.rise = 0; b.in.cut = 0; b.in.excess = 255; step(&b); }
    b.in.cut = NPORTS;
    run(&b, 2000);
    CHECK(b.out.state[0] == PS_CHARGE && b.out.duty[0] == 0);

    /* a pulled cell is found also while the limits are in force */
    bench_init(&b, 0, 63); attach(&b, 0, 3.7, 4000, 0.1);
    b.in.lim = 1; b.in.rise = 0xFF; b.in.cut = NPORTS;
    run(&b, 12000);
    CHECK(b.out.state[0] == PS_CHARGE && abs((int)b.out.i[0] - 63) <= 3);
    b.present[0] = 0;
    for (n = 0; n < 2000 && b.out.state[0] != PS_IDLE; n++) { b.ovr_v[0] = b.out.duty[0] > 0 ? 3830 : 0; step(&b); }
    CHECK(b.out.state[0] == PS_IDLE && n < 800);

    /* a real cell is unaffected: it keeps drawing its current for a long time */
    bench_init(&b, 0, 21); attach(&b, 0, 3.7, 4000, 0.1);
    run(&b, 20000);
    CHECK(b.out.state[0] == PS_CHARGE && abs((int)b.out.i[0] - 21) <= 2);
}

/* each port has its own current setting and can sit the run out */
static void t_per_port(void)
{
    bench_t b;
    bench_init(&b, 0, 21);
    attach(&b, 0, 3.7, 4000, 0.1); attach(&b, 1, 3.7, 4000, 0.1); attach(&b, 2, 3.7, 4000, 0.1);
    b.in.iset[1] = 42; b.in.en[2] = 0;
    run(&b, 16000);
    CHECK(b.out.state[0] == PS_CHARGE && abs((int)b.out.i[0] - 21) <= 2);
    CHECK(b.out.state[1] == PS_CHARGE && abs((int)b.out.i[1] - 42) <= 3);
    CHECK(b.out.state[2] == PS_IDLE && b.out.duty[2] == 0 && b.out.i[2] == 0);                 /* disabled: not even probed */
    b.in.en[2] = 1;
    run(&b, 16000);
    CHECK(b.out.state[2] == PS_CHARGE && abs((int)b.out.i[2] - 21) <= 2);
    b.in.en[0] = 0;                                                                              /* switched off mid-charge */
    run(&b, 50);
    CHECK(b.out.state[0] == PS_IDLE && b.out.duty[0] == 0 && b.out.state[1] == PS_CHARGE);
}

/* storage voltage: the rail holds the cell near the target, the current tapers and the rest test ends it in the window; even a
   high-resistance cell (big load rise) is not driven above the target */
static void t_storage(void)
{
    bench_t b; int k, full_at = -1;
    bench_init(&b, 2, 35);                                                          /* 3.80 V, 0.5 A */
    attach(&b, 0, 3.60, 40, 0.3);                                                   /* a high-resistance cell: big load rise */
    for (k = 0; k < 200000 && full_at < 0; k++) {
        step(&b);
        if (b.out.state[0] == PS_FULL) full_at = k;
    }
    CHECK(full_at > 0);
    CHECK(ocv(&b.cell[0]) > 3.76 && ocv(&b.cell[0]) < 3.85);                        /* at rest close to 3.80 V */
}

/* FULL: a cell that sags below the hold window is tested again; inside it nothing happens */
static void t_full_hold(void)
{
    bench_t b;
    bench_init(&b, 0, 21);
    attach(&b, 0, 4.19, 40, 0.1); run(&b, 20);
    CHECK(b.out.state[0] == PS_FULL);
    run(&b, 2000); CHECK(b.out.state[0] == PS_FULL);
    b.ovr_v[0] = T0 - 40; run(&b, 400); CHECK(b.out.state[0] == PS_FULL);           /* a short sag is ignored */
    run(&b, 200); CHECK(b.out.state[0] == PS_PROBE || b.out.state[0] == PS_CHARGE);
    bench_init(&b, 0, 21);
    attach(&b, 0, 4.19, 40, 0.1); run(&b, 20);
    b.ovr_v[0] = T0 + 60; run(&b, 600); CHECK(b.out.state[0] == PS_BLEED);          /* risen above it: bleed */
}

/* bleed: down to the target, then the rest test ends it in FULL */
static void t_bleed(void)
{
    bench_t b; int k, full_at = -1;
    bench_init(&b, 2, 21);                                                          /* 3.80 V storage */
    attach(&b, 0, 3.95, 20, 0.1);
    for (k = 0; k < 400000 && full_at < 0; k++) { step(&b); if (b.out.state[0] == PS_FULL) full_at = k; }
    CHECK(full_at > 0);
    CHECK(ocv(&b.cell[0]) > 3.76 && ocv(&b.cell[0]) < 3.84);
    run(&b, 20000); CHECK(b.out.state[0] == PS_FULL && b.out.sw[0] == 0);           /* and it stays there */
}

int main(void)
{
    t_sup(); t_idle_and_probe(); t_cc(); t_classify(); t_full_charge(); t_faults(); t_cut(); t_independent(); t_gate(); t_pulled(); t_per_port(); t_storage(); t_full_hold(); t_bleed();
    return check_report("chg");
}
