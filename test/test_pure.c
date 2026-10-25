/* Host unit tests for the pure modules (cal, ctl).  make test */
#include <stdio.h>
#include <stdlib.h>
#include "cal.h"
#include "ctl.h"
#include "fmt.h"
#include "cfg.h"
#include "input.h"
#include "dbgscr.h"
#include "buzz.h"
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) CHECK(abs((int)(a) - (int)(b)) <= (tol))

static void t_cal(void)
{
    NEAR(cal_port_mv(3822), 4199, 2);        /* the mode-0 target 3822 is 4.20 V */
    NEAR(cal_port_mv(3958), 4349, 2);        /* mode 1: 4.35 V */
    NEAR(cal_port_mv(3458), 3799, 2);        /* mode 2: 3.80 V */
    NEAR(cal_port_mv(3504), 3850, 2);        /* mode 3: 3.85 V */
    NEAR(cal_port_mv(3532), 3880, 2);        /* raw 3532 -> 3.88 V */
    CHECK(cal_port_mv(0) == 0);
    NEAR(cal_port_mv(4095), 4499, 2);
    NEAR(cal_mv_to_port(4200), 3822, 1);
    NEAR(cal_mv_to_port(1000), 910, 1);      /* the dead-cell cutoff 910 is 1.00 V */
    NEAR(cal_mv_to_port(cal_port_mv(2000)), 2000, 1);
    NEAR(cal_port_ma(21), 308, 1);           /* iset 21 counts = 0.308 A */
    NEAR(cal_port_ma(91), 1333, 2);
    CHECK(cal_port_ma(0) == 0);
    NEAR(cal_vin_mv(2999), 19700, 40);       /* 19.7 V at a filtered input reading of 2999 */
    CHECK(cal_set_to_ma(21) == 300);
    CHECK(cal_set_to_ma(91) == 1300);
    CHECK(cal_ma_step_up(21) == 28);
    CHECK(cal_ma_step_up(84) == 91);
    CHECK(cal_ma_step_up(91) == CUR_MIN);    /* wraps */
    CHECK(cal_ma_step_up(250) == CUR_MIN);   /* never overflows the 8-bit domain */
}

static void t_clamp(void)
{
    CHECK(ctl_clamp_duty(-5) == 0);
    CHECK(ctl_clamp_duty(0) == 0);
    CHECK(ctl_clamp_duty(PWM_PERIOD) == PWM_PERIOD);
    CHECK(ctl_clamp_duty(PWM_PERIOD + 1) == PWM_PERIOD);
    CHECK(ctl_clamp_duty(1000000L) == PWM_PERIOD);
    CHECK(ctl_clamp_duty(-1000000L) == 0);
}

static void t_port(void)
{
    CHECK(ctl_port_step(200, 21, 25) == 198);       /* too much current -> back off by err/2 */
    CHECK(ctl_port_step(200, 21, 17) == 202);
    CHECK(ctl_port_step(200, 21, 22) == 199);       /* err -1: 200 - 0.5 = 199.5 truncates to 199 (floor) */
    CHECK(ctl_port_step(200, 21, 23) == 199);       /* err -2 */
    CHECK(ctl_port_step(200, 21, 24) == 198);       /* err -3: 198.5 -> 198 */
    CHECK(ctl_port_step(200, 22, 21) == 200);       /* err +1: 200.5 -> 200 */
    CHECK(ctl_port_step(399, 91, 0) == PWM_PERIOD); /* clamps high */
    CHECK(ctl_port_step(3, 0, 100) == 0);           /* clamps low */
    CHECK(ctl_port_step(0, 21, 21) == 0);
}

/* A crude rail plant: ADC8 falls with duty (the slope measured on the bench is shallower; the boot
 * self-test implies >= 2.5 counts/duty with P4.7 on).  The loop must settle without windup. */
static int16_t plant(int16_t duty) { int32_t v = 4333 - 5 * (int32_t)duty / 2; return v > 4095 ? 4095 : (int16_t)v; }

static void t_rail(void)
{
    int16_t duty = 100;
    int i;
    ctl_rail_reset(100);
    for (i = 0; i < 4000; i++) duty = ctl_rail_step(plant(duty), 3958);
    NEAR(plant(duty), 3958, 3);
    CHECK(duty > 0 && duty < PWM_PERIOD);

    /* unreachable setpoint: duty pins at the limit, integral stays bounded, and recovery is quick */
    ctl_rail_reset(100);
    for (i = 0; i < 20000; i++) duty = ctl_rail_step(plant(duty), 3000);
    CHECK(duty == PWM_PERIOD);
    CHECK(labs(ctl_rail_integral()) <= 100000L);
    for (i = 0; i < 4000; i++) duty = ctl_rail_step(plant(duty), 3958);
    NEAR(plant(duty), 3958, 3);

    /* the other end: a rail that can never fall to the setpoint pins duty at 0 */
    ctl_rail_reset(100);
    for (i = 0; i < 20000; i++) duty = ctl_rail_step(4095, 4300);
    CHECK(duty == 0);

    /* at setpoint with zero integral nothing moves */
    ctl_rail_reset(150);
    CHECK(ctl_rail_step(3958, 3958) == 150);

    /* step response: a +30 count disturbance is rejected, with bounded overshoot */
    ctl_rail_reset(150);
    {
        int16_t worst = 0, m;
        for (i = 0; i < 3000; i++) {
            m = plant(duty = ctl_rail_step(plant(duty) + (i > 500 ? 30 : 0), 3958));
            if (i > 600 && abs(m - 3958) > worst) worst = (int16_t)abs(m - 3958);
        }
        NEAR(plant(duty) + 30, 3958, 4);
        CHECK(worst < 40);
    }
}

static void t_fmt(void)
{
    char b[8];
    CHECK(fmt_v2(b, 4197) == 5 && !strcmp(b, "4.20V"));
    CHECK(!strcmp((fmt_v2(b, 0), b), "0.00V"));
    CHECK(!strcmp((fmt_v2(b, 4204), b), "4.20V"));
    CHECK(!strcmp((fmt_v2(b, 4205), b), "4.21V"));   /* round half up */
    CHECK(!strcmp((fmt_v2(b, 65535), b), "9.99V"));   /* clamps, never overruns */
    CHECK(!strcmp((fmt_v1(b, 19680), b), "19.7V"));
    CHECK(!strcmp((fmt_v1(b, 5000), b), "5.0V"));
    CHECK(!strcmp((fmt_v1(b, 65535), b), "65.5V"));   /* near uint16 max: must not wrap (int is 16-bit on target) */
    CHECK(!strcmp((fmt_v2(b, 65530), b), "9.99V"));
    CHECK(!strcmp((fmt_a1(b, 300), b), "0.3A"));
    CHECK(!strcmp((fmt_a1(b, 1300), b), "1.3A"));
    CHECK(fmt_a1(b, 65535) == 4 && !strcmp(b, "9.9A"));
    CHECK(!strcmp((fmt_a2(b, 312), b), "0.31A") && !strcmp((fmt_a2(b, 0), b), "0.00A") && !strcmp((fmt_a2(b, 65535), b), "9.99A"));
    CHECK(!strcmp((fmt_u(b, 0), b), "0") && !strcmp((fmt_u(b, 65535), b), "65535") && !strcmp((fmt_u(b, 3958), b), "3958"));
}

static void press(uint8_t btn, int ms)
{
    int k;
    for (k = 0; k < ms; k++) input_tick(btn == 1, btn == 2);
    input_tick(0, 0);
}

static void t_input(void)
{
    int k;
    input_reset();
    press(1, 4);   CHECK(input_get() == EV_NONE);              /* bounce (< 5 ticks) */
    press(1, 100); CHECK(input_get() == EV_B1_SHORT); CHECK(input_get() == EV_NONE);
    press(1, 699); CHECK(input_get() == EV_B1_SHORT);          /* just under the long threshold */
    press(1, 700); CHECK(input_get() == EV_B1_LONG); CHECK(input_get() == EV_NONE);   /* long fires once, no short on release */
    press(1, 5000);CHECK(input_get() == EV_B1_LONG); CHECK(input_get() == EV_NONE);   /* holding longer does not repeat */
    press(2, 100); CHECK(input_get() == EV_B2_SHORT);
    press(2, 999); CHECK(input_get() == EV_B2_SHORT);
    press(2, 1000);CHECK(input_get() == EV_B2_LONG);
    /* two buttons one after the other are independent */
    press(1, 50); press(2, 50);
    CHECK(input_get() == EV_B1_SHORT); CHECK(input_get() == EV_B2_SHORT);
    /* the queue drops events instead of overflowing */
    for (k = 0; k < 10; k++) press(1, 50);
    k = 0; while (input_get() != EV_NONE) k++;
    CHECK(k == 3);

    /* chord: B1 first, B2 50 ms later, both held 1.6 s: one EV_CHORD, no short/long events from either button */
    input_reset();
    for (k = 0; k < 100; k++) input_tick(1, 0);
    for (k = 0; k < 3100; k++) input_tick(1, 1);
    CHECK(input_get() == EV_CHORD); CHECK(input_get() == EV_NONE);
    for (k = 0; k < 4000; k++) input_tick(1, 1);                    /* holding on does not repeat it */
    CHECK(input_get() == EV_NONE);
    input_tick(1, 0);                                                /* one button released: still the chord's */
    for (k = 0; k < 2000; k++) input_tick(1, 0);
    input_tick(0, 0);
    CHECK(input_get() == EV_NONE);
    press(1, 100); CHECK(input_get() == EV_B1_SHORT);                /* normal operation resumes */
    /* B2 first, B1 within 200 ms */
    input_reset();
    for (k = 0; k < 300; k++) input_tick(0, 1);
    for (k = 0; k < 3000; k++) input_tick(1, 1);
    CHECK(input_get() == EV_CHORD); CHECK(input_get() == EV_NONE);
    input_tick(0, 0);
    /* a quick press of both is nothing (no short events either) */
    input_reset();
    for (k = 0; k < 100; k++) input_tick(1, 1);
    input_tick(0, 0);
    CHECK(input_get() == EV_NONE);
    /* B1 long already fired before B2 arrives: not a chord, B1 long stands, B2 acts alone */
    input_reset();
    for (k = 0; k < 720; k++) input_tick(1, 0);
    CHECK(input_get() == EV_B1_LONG);
    for (k = 0; k < 3500; k++) input_tick(1, 1);
    CHECK(input_get() == EV_B2_LONG); CHECK(input_get() == EV_NONE);   /* B2 long on its own; no chord event */
    input_tick(0, 0);
}

static void t_cfg(void)
{
    CHECK(cfg_valid_current(0xFF) == 21);          /* erased flash */
    CHECK(cfg_valid_ilim(0xFF) == 0 && cfg_valid_ilim(4) == 0 && cfg_valid_ilim(5) == 5 && cfg_valid_ilim(100) == 100 && cfg_valid_ilim(101) == 0);
    CHECK(cfg_valid_vmin(0xFF) == 0 && cfg_valid_vmin(47) == 0 && cfg_valid_vmin(48) == 48 && cfg_valid_vmin(250) == 250 && cfg_valid_vmin(251) == 0);
    CHECK(cfg_valid_lmode(0xFF) == 0 && cfg_valid_lmode(3) == 3 && cfg_valid_lmode(4) == 0);   /* unknown -> equal sharing */
    CHECK(cfg_valid_current(20) == 21);
    CHECK(cfg_valid_current(21) == 21);
    CHECK(cfg_valid_current(91) == 91);
    CHECK(cfg_valid_current(92) == 21);
    CHECK(cfg_valid_mode(0xFF) == 0); CHECK(cfg_valid_mode(3) == 3); CHECK(cfg_valid_mode(4) == 0);
    CHECK(cfg_valid_buzzer(0xFF) == 1);            /* erased = buzzer on */
    CHECK(cfg_valid_buzzer(0) == 0); CHECK(cfg_valid_buzzer(1) == 1);
}

/* run the engine for `n` ticks (0.5 ms); returns the number of ticks with the tone pin high, and edges */
static void buzz_run(int ms, int *high_ms, int *edges, int *on_ms)
{
    int k, last = 0;
    *high_ms = *edges = *on_ms = 0;
    for (k = 0; k < ms; k++) {
        int lvl = buzz_tick();
        if (lvl) (*high_ms)++;
        if (lvl != last) (*edges)++;
        last = lvl;
        if (buzz_busy()) (*on_ms)++;
    }
}

static void t_buzz(void)
{
    int hi, ed, on;
    buzz_reset();
    buzz_play(PAT_BOOT);
    buzz_run(700, &hi, &ed, &on);
    NEAR(on, 501, 4);                          /* 167 + 167 + 167 ticks = 84 + 84 + 84 ms */
    NEAR(ed, 334, 6);                          /* a level change every tick (1 kHz) for 2 x 167 ticks */
    CHECK(!buzz_busy());

    buzz_reset(); buzz_enable(0);              /* muted by the user: pattern runs, pin stays low */
    buzz_play(PAT_DONE);
    buzz_run(2000, &hi, &ed, &on);
    CHECK(hi == 0 && ed == 0);
    buzz_alarm(1); CHECK(buzz_alarm_level() == 1);   /* the steady alarm level is independent of the mute */

    buzz_reset();
    buzz_play(PAT_RAIL_ALARM);                 /* repeats until stopped */
    buzz_run(10000, &hi, &ed, &on);
    CHECK(buzz_busy());
    buzz_stop();
    CHECK(!buzz_busy() && buzz_tick() == 0);

    buzz_reset();
    buzz_play(PAT_DONE);
    buzz_play(PAT_KEY);                        /* a new pattern replaces the running one */
    buzz_run(200, &hi, &ed, &on);
    NEAR(on, 33, 3);
}

static void t_dbg(void)
{
    uint8_t k;
    memset(&dbg_view, 0, sizeof dbg_view);
    dbg_view.bandgap = 1073; dbg_view.vin = 768; dbg_view.rail = 3971; dbg_view.rail_duty = 187; dbg_view.adc[0] = 2345;
    dbg_view.v[3] = 3966; dbg_view.i[3] = 21; dbg_view.duty[3] = 345; dbg_view.state[3] = 2;
    dbg_view.mode = 1; dbg_view.iset = 21; dbg_view.iset_user = 28; dbg_view.tgt = 3958; dbg_view.setpoint = 3972;
    dbg_view.ms = 123456; dbg_view.fan = 1;
    CHECK(dbg_vcc_mv(1073) == 4543); CHECK(dbg_vcc_mv(976) == 4994); CHECK(dbg_vcc_mv(1219) == 3999); CHECK(dbg_vcc_mv(0) == 0);
    CHECK(!strncmp(dbg_text(1, 1), "BANDGAP 1073 counts", 19));
    CHECK(!strncmp(dbg_text(1, 2), "VCC 4.543V", 10));
    CHECK(!strncmp(dbg_text(1, 3), "VIN  768  5.042V", 16));
    CHECK(!strncmp(dbg_text(1, 5), "RAIL PWM 187/400", 16));
    CHECK(!strncmp(dbg_text(1, 6), "ADC0 (THERM) 2345", 17));
    CHECK(!strncmp(dbg_text(1, 7), "FAN P0.7  ON", 12));
    CHECK(!strncmp(dbg_text(1, 9), "UPTIME 123 s", 12));
    CHECK(!strncmp(dbg_text(2, 4), "P4 3966 4.357 4.399", 19));          /* A = 4.5V/4096, B = VCC/4096 */
    CHECK(!strncmp(dbg_text(3, 4), "P4  21  308 D345 CHARGE", 23));
    CHECK(!strncmp(dbg_text(4, 1), "RUN 0  MODE 1 4.35V", 19));
    CHECK(!strncmp(dbg_text(4, 2), "SET 21 USER 28  300mA", 21));
    CHECK(!strncmp(dbg_text(4, 6), "PORT STATE 000200", 17));
    dbg_view.adc[10] = 1234; dbg_view.adc[15] = 1073;
    CHECK(!strncmp(dbg_text(5, 4), "A9:   0 A10:1234 A11:   0", 25));
    CHECK(!strncmp(dbg_text(5, 6), "A15:1073", 8));
    /* every row of every page: exactly DBG_COLS printable characters, NUL terminated */
    for (k = 0; k < DBG_PAGES * DBG_ROWS; k++) {
        const char *t = dbg_text((uint8_t)(1 + k / DBG_ROWS), (uint8_t)(k % DBG_ROWS));
        int n = 0, bad = 0;
        while (t[n]) { if (t[n] < 32 || t[n] > 126) bad = 1; n++; }
        CHECK(n == DBG_COLS && !bad);
    }
    CHECK(!strncmp(dbg_text(1, 0), "1/6 ", 4) && !strncmp(dbg_text(6, 0), "6/6 ", 4));
}

int main(void)
{
    t_cal(); t_clamp(); t_port(); t_rail(); t_fmt(); t_input(); t_cfg(); t_buzz(); t_dbg();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
