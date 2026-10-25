#include "app.h"
#include "usb.h"
#include "dbgscr.h"
#include "board.h"
#include "hal.h"
#include "adc.h"
#include "lcd.h"
#include "ui.h"
#include "theme.h"
#include "cfg.h"
#include "cal.h"
#include "ctl.h"
#include "chg.h"
#include "sup.h"
#include "fmt.h"
#include "input.h"
#include "buzz.h"
#include "ports.h"
#include "layout.h"
#include "menu.h"
#include "limits.h"

#ifndef CHARGE_ENABLED
#define CHARGE_ENABLED 1          /* 0 = lab build: full UI and supervision, but charging can never start */
#endif

#define RAIL_UP_COUNTS   3185     /* wait for ADC8 >= 3185 before starting the display */
#define RAIL_UP_TIMEOUT  10000    /* ms */
#define SELFTEST_LO1     3388     /* phase 1 (servo to 3458) must land in [3388, 3528) */
#define SELFTEST_HI1     3528
#define SELFTEST_LO2     3878     /* phase 2 (servo to 3958) must land in [3878, 4037) */
#define SELFTEST_HI2     4037
#define VCC_OK_MIN       976      /* bandgap count: VCC <= 5.0 V (it never starts above that) */
#define BG_GLITCH        15       /* bandgap counts (~1.4 % of VCC) off the baseline: treat as a supply glitch */
#define LCD_RECONFIG_PASSES 222   /* ~1 s */
#define RAIL_BACKOFF     14       /* current setting -14 when the rail trips */

/* debug mailbox (fixed XRAM address): lets a debugger look inside */
struct dbg {
    uint16_t magic;
    uint32_t ms;
    uint8_t  state[NPORTS], flags;
    uint16_t duty[NPORTS], v[NPORTS], i[NPORTS], rail, rail_duty, vin;
    uint8_t  iset, stop_reason, alarm;
};
__xdata __at (DBG_BASE) struct dbg g_dbg;

static uint8_t  XD rail_hold, run, dc, strap, mode, iset_user, stop_reason, alarm_ui, poweroff_req;
static uint32_t XD banner_until;                 /* ms: show a short message in the input-voltage box */
static uint8_t  XD banner_code;
static int16_t  XD rail_duty;
static uint16_t XD bg_ref;                     /* slow baseline of the bandgap count: a jump away from it = a supply glitch */
static uint8_t  XD lcd_cnt;
static uint8_t  XD dbg_page, dbg_next_row;      /* diagnostics screen (opened from the menu): 0 = off, else 1..DBG_PAGES */
static uint8_t  XD menu_on, menu_session, menu_next_row;   /* menu showing / a menu session is open (menu or diagnostics) / row being drawn */
static uint32_t XD t_next;
static chg_in_t  XD cin;
static chg_out_t XD cout;
static ui_state_t XD ui;
static sup_in_t  XD sin;
static sup_out_t XD sout;

/* ------------------------------------------------------------------------------------------------------------- */

/* ---- USB telemetry (layout in USB.md).  The interrupt copies usb_tx to the host; it is rebuilt here, tail byte last. ---- */
enum { PH_RAIL_WAIT = 1, PH_SPLASH, PH_VCC_GATE, PH_SELFTEST, PH_INPUT, PH_CHOOSE, PH_RUN, PH_FATAL = 0x80 };
enum { EV_PORT_FULL = 1, EV_PORT_FAULT, EV_STOPPED };           /* ev_code; ev_port = port (0..5) or the stop reason */
static uint8_t XD phase, tele_seq, ev_seq, ev_code, ev_port, cmd_seq, cmd_res;

static void note_event(uint8_t code, uint8_t port) { ev_code = code; ev_port = port; ev_seq++; }

static void put16(uint8_t at, uint16_t v) { usb_tx[at] = (uint8_t)(v >> 8); usb_tx[at + 1] = (uint8_t)v; }

static void tele(uint8_t ph)
{
    uint8_t k;
    uint32_t ms = hal_ms();
    phase = ph;
    usb_tx[63] = 0;                                   /* torn-copy guard: the tail matches the head only when complete */
    for (k = 3; k < 63; k++) usb_tx[k] = 0;
    if (++tele_seq == 0) tele_seq = 1;
    usb_tx[0] = 0x57; usb_tx[1] = 0x33; usb_tx[2] = tele_seq; usb_tx[3] = 1;
    usb_tx[4] = (uint8_t)(ms >> 24); usb_tx[5] = (uint8_t)(ms >> 16); usb_tx[6] = (uint8_t)(ms >> 8); usb_tx[7] = (uint8_t)ms;
    usb_tx[8] = (uint8_t)(run | (mode << 1) | ((run && !rail_hold) << 3) | (CHARGE_ENABLED << 4) | (rail_hold << 5) | (strap << 6) | (dc << 7));
    usb_tx[9] = iset_user; usb_tx[10] = iset_user; usb_tx[11] = stop_reason; usb_tx[12] = (uint8_t)(alarm_ui | ((menu_on ? 15 : dbg_page) << 4));
    usb_tx[13] = ev_seq; usb_tx[14] = ev_code; usb_tx[15] = ev_port; usb_tx[16] = cmd_seq; usb_tx[17] = cmd_res;
    usb_tx[25] = ph;
    if (ph >= PH_RUN && ph < PH_FATAL) {
        for (k = 0; k < NPORTS; k++) {
            usb_tx[18 + k] = cout.state[k];
            usb_tx[24] |= (uint8_t)(cout.sw[k] << k);
            put16(26 + 2 * k, cout.duty[k]);
            put16(38 + 2 * k, cout.v[k]);
            usb_tx[50 + k] = cout.i[k] > 255 ? 255 : (uint8_t)cout.i[k];
        }
        put16(56, cin.rail); put16(58, cin.vin); put16(60, sin.bandgap);
        usb_tx[62] = (uint8_t)(rail_duty >> 2);
    } else {
        put16(56, adc_get(ADC_RAIL)); put16(58, adc_get(ADC_VIN)); put16(60, adc_get(ADC_BANDGAP));
    }
    usb_tx[63] = tele_seq;
}

static void outputs_off(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) { hal_pwm(k, 0); hal_sw(k, 0); }
    hal_global(0);
}

static void rail_step(void)
{
    rail_duty = ctl_rail_step((int16_t)adc_get(ADC_RAIL), (int16_t)sup_setpoint(mode));
    hal_rail((uint16_t)rail_duty);
}

/* Staggered shut-down: ports 1+4, 2+5, 3+6, 75 ms apart, so the supply never sees all six
 * loads drop at once.  Blocks ~0.23 s; the rail regulator is not serviced meanwhile. */
static void soft_stop(void)
{
    uint8_t s;
    for (s = 0; s < 3; s++) {
        hal_pwm(s, 0); hal_sw(s, 0);
        hal_pwm((uint8_t)(s + 3), 0); hal_sw((uint8_t)(s + 3), 0);
        hal_delay_ms(75);
    }
}

static void stop_run(uint8_t why)
{
    run = 0; stop_reason = why;
    note_event(EV_STOPPED, why);
    chg_abort();
    soft_stop();
    outputs_off();
}

static void fatal(const char *title, const char *line2, uint8_t has_n, uint16_t n)
{
    outputs_off();
    hal_rail(0);
    tele(PH_FATAL);
    ui_message(title, line2, has_n, n, 0, 0);
    buzz_play(PAT_ERROR);
    for (;;) { hal_delay_ms(1000); }               /* outputs stay off until the power is cycled */
}

/* ------------------------------------------------------------------------------------------------------------- */
static uint8_t selftest(uint16_t *r1, uint16_t *r2)
{
    uint8_t ph, n;
    ctl_rail_reset(rail_duty);
    for (ph = 0; ph < 2; ph++) {
        int16_t S = ph ? 3958 : 3458;
        for (n = 0; n < 100; n++) {
            adc_scan();
            rail_duty = ctl_rail_step((int16_t)adc_get(ADC_RAIL), S);
            hal_rail((uint16_t)rail_duty);
            hal_delay_ms(2);
        }
        if (ph == 0) *r1 = adc_get(ADC_RAIL); else *r2 = adc_get(ADC_RAIL);
    }
    return *r1 >= SELFTEST_LO1 && *r1 < SELFTEST_HI1 && *r2 >= SELFTEST_LO2 && *r2 < SELFTEST_HI2;
}

void app_boot(void)
{
    uint16_t r1 = 0, r2 = 0, vin;
    uint32_t t0;
    uint8_t n, ev, sel;

    cfg_load();                                      /* flash reads are safe before anything is powered */
    mode = cfg_mode(); iset_user = cfg_current(); th_set(cfg_theme());
    lim_cfg.ilim = cfg_ilim(); lim_cfg.vmin = cfg_vmin(); lim_cfg.vcut = cfg_vcut(); lim_cfg.mode = cfg_lmode();
    input_reset(); buzz_reset(); buzz_enable(cfg_buzzer());

    hal_power(1);                                    /* main power enable, first on */
    strap = hal_strap();
    if (!strap) hal_profile(1);                      /* P5.3 set when the strap is low */
    rail_duty = 100; hal_rail(100);                  /* rail PWM starts at 100/400 */
    ctl_rail_reset(rail_duty);
    buzz_alarm(1);                                   /* steady P0.7 level during start-up */
    hal_irq_on();

    adc_prime();
    t0 = hal_ms();
    for (;;) {                                       /* wait for the rail */
        adc_scan();
        tele(PH_RAIL_WAIT);
        if (adc_get(ADC_RAIL) >= RAIL_UP_COUNTS) break;
        if (hal_ms() - t0 > RAIL_UP_TIMEOUT) { lcd_init(); fatal("NO RAIL", "RAIL DID NOT COME UP", 1, adc_get(ADC_RAIL)); }
        hal_delay_ms(2);
    }
    lcd_init();
    tele(PH_SPLASH);
    ui_splash();
    buzz_play(PAT_BOOT);
    hal_delay_ms(1000);

    /* VCC gate: do not start unless VCC <= 5.0 V (bandgap >= 976) */
    t0 = hal_ms();
    n = 0;
    for (;;) {
        adc_scan_slow_now(); adc_scan_slow_now();
        tele(PH_VCC_GATE);
        if (adc_get(ADC_BANDGAP) >= VCC_OK_MIN) break;
        if (hal_ms() - t0 > 3000 && !n) { n = 1; ui_message("VCC HIGH", "SUPPLY OVER 5.0V", 1, adc_get(ADC_BANDGAP), 0, 0); }
        hal_delay_ms(100);
    }

    /* rail authority self-test: retry until it passes, showing the numbers meanwhile */
    tele(PH_SELFTEST);
    while (!selftest(&r1, &r2)) {
        tele(PH_SELFTEST);
        ui_message("RAIL TEST FAIL", "RETRYING", 1, r1, 1, r2);
        hal_delay_ms(1000);
    }
    buzz_alarm(0);

    /* input voltage: 50 samples, then the input rules */
    for (n = 0, vin = 0; n < 50; n++) { adc_scan(); adc_scan_slow_now(); hal_delay_ms(3); }
    vin = adc_get(ADC_VIN);
    n = 0;
    tele(PH_INPUT);
    while (vin < VIN_MIN) {
        tele(PH_INPUT);
        if (!n) { n = 1; ui_message("ERR:INPUT LOW", "CONNECT INPUT", 1, vin, 0, 0); }
        hal_delay_ms(500);
        adc_scan_slow_now(); adc_scan_slow_now(); vin = adc_get(ADC_VIN);
    }
    sup_reset();
    dc = 0;
    if (strap) {
        sup_set_input(vin);
        if (!dc && vin < sup_lim_stop(sup_input_cells())) {  /* ambiguous input: ask the user */
            sel = 0;
            ui_choose(sel);
            for (;;) {
                tele(PH_CHOOSE);
                hal_delay_ms(10);
                ev = input_get();
                if (ev == EV_B1_SHORT) { sel ^= 1; buzz_play(PAT_KEY); ui_choose(sel); }
                else if (ev == EV_B1_LONG) { dc = sel; buzz_play(PAT_KEY); break; }
            }
        }
    }

    ui_main_init();
    adc_prime();
    bg_ref = adc_get(ADC_BANDGAP); lcd_cnt = 0;
    t_next = hal_ticks();
    run = 0; stop_reason = 0; alarm_ui = 0; poweroff_req = 0; dbg_page = 0; menu_on = 0; menu_session = 0;
    lim_reset();
    chg_init();
}

/* ------------------------------------------------------------------------------------------------------------- */
static uint8_t start_run(void)
{
#if CHARGE_ENABLED
    if (!run && sout.stop == STOP_NONE) { run = 1; stop_reason = 0; chg_init(); lim_reset(); return 1; }
#endif
    return 0;
}

/* host commands (USB.md): executed here, in the main loop, with the same rules as the buttons */
static void usb_service(void)
{
    uint8_t c[8], res = 0;
    if (!usb_cmd_get(c)) return;
    switch (c[0]) {
    case 1: if (run) stop_run(STOP_USER); break;                                       /* stop (always allowed) */
    case 2: res = start_run() ? 0 : 1; break;                                          /* start */
    case 3:                                                                            /* set current (units), not while running */
        if (run || c[1] < CUR_MIN || c[1] > CUR_MAX || (c[1] - CUR_MIN) % CUR_UNITS_PER_100MA) { res = 1; break; }
        iset_user = c[1];
        cfg_set_current(iset_user);
        break;
    case 4:                                                                            /* set mode, not while running */
        if (run || c[1] > 3) { res = 1; break; }
        mode = c[1]; cfg_set_mode(mode);
        break;
    case 5: break;                                                                     /* ping */
    case 6: if (run) { res = 1; break; } hal_reboot_isp(); break;                      /* reboot into the USB ISP loader (not while charging) */
    default: res = 2; break;
    }
    cmd_seq = c[2]; cmd_res = res;
}

/* ---- menu session: the menu and the diagnostics screens it leads to ---- */
static void menu_enter(void)
{
    menu_v.mode = mode; menu_v.cur = iset_user; menu_v.sound = cfg_buzzer(); menu_v.theme = th_get();
    menu_v.ilim = lim_cfg.ilim; menu_v.vmin = lim_cfg.vmin; menu_v.vcut = lim_cfg.vcut; menu_v.lmode = lim_cfg.mode;
    menu_open(run, CUR_MAX);
    menu_session = 1; menu_on = 1; dbg_page = 0; menu_next_row = 0;
    ui_text_enter();
}

/* close: the values were applied live, now they are saved (only what changed is written) */
static void menu_leave(void)
{
    cfg_set_mode(mode); cfg_set_current(iset_user); cfg_set_buzzer(menu_v.sound); cfg_set_theme(th_get());
    cfg_set_limits(lim_cfg.ilim, lim_cfg.vmin, lim_cfg.vcut, lim_cfg.mode);
    menu_session = 0; menu_on = 0; dbg_page = 0;
    ui_main_init();
}

static void menu_key(uint8_t ev)
{
    static uint8_t XD a;
    a = menu_event(ev);
    if (a == MA_LOCKED) buzz_play(PAT_ERROR);
    else if (a == MA_DIAG) { menu_on = 0; dbg_page = 1; dbg_next_row = 0; ui_text_enter(); buzz_play(PAT_KEY); }
    else if (a == MA_EXIT) { menu_leave(); buzz_play(PAT_KEY); }
    else if (a) buzz_play(PAT_KEY);
}

static void handle_buttons(void)
{
    uint8_t ev;
    while ((ev = input_get()) != EV_NONE) {
        if (ev == EV_CHORD) {                            /* B1+B2 held together: menu on / off (off also saves) */
            if (menu_session) menu_leave(); else menu_enter();
            buzz_play(PAT_KEY);
            continue;
        }
        if (menu_on) { menu_key(ev); continue; }
        if (dbg_page) {                                  /* diagnostics: right = next page, left = previous, left long = back to the menu */
            if (ev == EV_B2_SHORT) { dbg_page = (uint8_t)(dbg_page % DBG_PAGES + 1); dbg_next_row = 0; ui_text_enter(); }
            else if (ev == EV_B1_SHORT) { dbg_page = (uint8_t)(dbg_page == 1 ? DBG_PAGES : dbg_page - 1); dbg_next_row = 0; ui_text_enter(); }
            else if (ev == EV_B1_LONG) { dbg_page = 0; menu_on = 1; menu_next_row = 0; ui_text_enter(); buzz_play(PAT_KEY); }
            continue;
        }
        switch (ev) {
        case EV_B1_SHORT:
            if (!run) {
                iset_user = (uint8_t)(iset_user + CUR_UNITS_PER_100MA);
                if (iset_user > CUR_MAX) iset_user = CUR_MIN;
                buzz_play(PAT_KEY);
                cfg_set_current(iset_user);
            }
            break;
        case EV_B1_LONG:
            if (run) stop_run(STOP_USER); else start_run();
            buzz_play(PAT_KEY);
            break;
        case EV_B2_SHORT:
            if (!run) {                                  /* no mode change while charging */
                mode = (uint8_t)((mode + 1) & 3);
                buzz_play(PAT_KEY);
                cfg_set_mode(mode);
            }
            break;
        case EV_B2_LONG:
            cfg_set_buzzer(!cfg_buzzer());
            buzz_enable(cfg_buzzer());
            buzz_play(PAT_KEY);
            banner_code = cfg_buzzer() ? UA_SND_ON : UA_SND_OFF;
            banner_until = hal_ms() + 1500;
            break;
        }
    }
}

void app_pass(void)
{
    uint8_t k, hi = 0, mid = 0;

    while ((int32_t)(hal_ticks() - t_next) < 0) ;
    t_next += PASS_TICKS;
    if ((int32_t)(hal_ticks() - t_next) > 4 * PASS_TICKS) t_next = hal_ticks();       /* fell behind: do not burst */

    adc_scan();
    handle_buttons();
    usb_service();
    if (menu_session) {                              /* menu values apply while they are edited; they are saved when the menu closes */
        if (!run) { mode = menu_v.mode; iset_user = menu_v.cur; }
        buzz_enable(menu_v.sound);
        if (menu_v.theme != th_get()) { th_set(menu_v.theme); ui_text_enter(); menu_next_row = 0; }   /* repaint the menu in the new colours */
        lim_cfg.ilim = menu_v.ilim; lim_cfg.vmin = menu_v.vmin; lim_cfg.vcut = menu_v.vcut; lim_cfg.mode = menu_v.lmode;
        menu_context(run, CUR_MAX);
    }
    rail_step();

    /* ---- supervision ---- */
    for (k = 0; k < NPORTS; k++) { if (cout.i[k] >= 63) hi++; if (cout.i[k] >= 56) mid++; }
    sin.vin = adc_get(ADC_VIN); sin.rail = adc_get(ADC_RAIL); sin.bandgap = adc_get(ADC_BANDGAP); sin.adc0 = adc_get(0);
    sin.setpoint = sup_setpoint(mode); sin.strap = strap; sin.dc = dc; sin.run = run; sin.mode = mode;
    sin.i_port_hi = hi; sin.i_port_mid = mid;
    sup_pass(&sin, &sout);
    if (sout.rail_cut && sin.bandgap >= 1192) sout.stop = STOP_VCC;     /* also checks VCC (>= 1192) */

    /* display self-healing: re-send the panel settings about once a second; after a sudden VCC dip (bandgap count jumps off its
       baseline) also repaint everything, since a glitch can scramble the panel's rotation, colours and picture */
    {
        int16_t d = (int16_t)sin.bandgap - (int16_t)bg_ref;
        bg_ref = (uint16_t)((int16_t)bg_ref + d / 32);
        if (d > BG_GLITCH || d < -BG_GLITCH) {
            bg_ref = sin.bandgap;
            lcd_reconfig();
            if (menu_session) ui_text_enter(); else ui_main_init();
        } else if (++lcd_cnt >= LCD_RECONFIG_PASSES) {
            lcd_cnt = 0;
            lcd_reconfig();
        }
    }

    alarm_ui = UA_NONE;
    if (sout.stop == STOP_RAIL) alarm_ui = UA_RAIL;
    else if (sout.stop == STOP_VIN) alarm_ui = UA_VIN;
    else if (sout.stop == STOP_VCC) alarm_ui = UA_VCC;
    else if (!run && stop_reason == STOP_VLIM) alarm_ui = UA_VLIM;
    if (banner_until && (int32_t)(hal_ms() - banner_until) < 0 && alarm_ui == UA_NONE) alarm_ui = banner_code;
    if (run && sout.stop != STOP_NONE && sout.stop != STOP_RAIL) stop_run(sout.stop);
    /* rail trouble is a pause, not a stop: while the rail is out of band for long, or over-voltage, nothing is driven;
       charging resumes by itself with a fresh soft ramp */
    rail_hold = (uint8_t)(run && (sout.stop == STOP_RAIL || sout.rail_cut));
    if (sout.rail_cut && run) {                      /* rail over-voltage: cut every port at once and back
                                                        the current off by 14 */
        if (iset_user >= 35) iset_user = (uint8_t)(iset_user - RAIL_BACKOFF);
    }
    if (sout.poweroff) { ui_message("LOW VOLTAGE", "POWERING OFF", 0, 0, 0, 0); hal_delay_ms(1000); hal_power_down(); }
    buzz_alarm(sout.alarm_steady);
    if (sout.rail_alarm && !buzz_busy()) buzz_play(PAT_RAIL_ALARM);

    /* ---- user limits (limits.h): per-port current caps, or a stop when a STOP-mode limit is exceeded ---- */
    if (run && !rail_hold) {
        for (k = 0; k < NPORTS; k++) {
            lim_in.charging[k] = cout.state[k] == PS_CHARGE; lim_in.want[k] = cout.want[k];
            lim_in.v[k] = cout.v[k]; lim_in.i[k] = cout.i[k];
        }
        lim_in.rail = adc_get(ADC_RAIL); lim_in.vin = adc_get(ADC_VIN);
        lim_pass();
        if (lim_out.trip) { stop_run(STOP_VLIM); buzz_play(PAT_ERROR); }
    } else {
        lim_reset();
        for (k = 0; k < NPORTS; k++) lim_out.cap[k] = 0;
        lim_out.est_ma = 0; lim_out.budget = LIM_P_BIG; lim_out.trip = LT_NONE;
    }
    for (k = 0; k < NPORTS; k++) cin.cap[k] = lim_out.cap[k];

    /* ---- charge control ---- */
    for (k = 0; k < NPORTS; k++) { cin.adc_v[k] = adc_raw(port_v_ch(k)); cin.adc_i[k] = adc_raw(port_i_ch(k)); }
    cin.rail = adc_get(ADC_RAIL); cin.vin = adc_get(ADC_VIN);
    cin.tgt = sup_target(mode); cin.setpoint = sup_setpoint(mode); cin.mode = mode; cin.iset = iset_user; cin.run = run;
    if (rail_hold) { outputs_off(); chg_cut(); }
    else chg_pass(&cin, &cout);

    if (run && !rail_hold) {
        hal_global(1);
        for (k = 0; k < NPORTS; k++) {
            hal_pwm(k, cout.duty[k]);
            hal_sw(k, cout.sw[k]);
            if (cout.event[k] == CE_FULL) { buzz_play(PAT_DONE); note_event(EV_PORT_FULL, k); }
            if (cout.event[k] == CE_FAULT) { buzz_play(PAT_ERROR); note_event(EV_PORT_FAULT, k); }
        }
    } else if (!rail_hold) {
        outputs_off();
    }

    /* ---- display ---- */
    ui.run = run; ui.mode = mode; ui.iset = iset_user; ui.vin = cin.vin; ui.tgt = cin.tgt; ui.alarm = alarm_ui;
    ui.blink = (uint8_t)((hal_ms() >> 9) & 1);
    for (k = 0; k < NPORTS; k++) { ui.state[k] = cout.state[k]; ui.v[k] = cout.v[k]; ui.i[k] = cout.i[k]; ui.parked[k] = (uint8_t)(cout.thr[k] && cin.cap[k] == CAP_PARK); }
    if (dbg_page) {
        uint8_t n;
        dbg_view.ms = hal_ms();
        for (n = 0; n < NADC; n++) dbg_view.adc[n] = adc_raw(n);
        for (n = 0; n < NPORTS; n++) {
            dbg_view.v[n] = cout.v[n]; dbg_view.i[n] = cout.i[n]; dbg_view.duty[n] = cout.duty[n];
            dbg_view.state[n] = cout.state[n]; dbg_view.sw[n] = cout.sw[n];
        }
        dbg_view.rail = cin.rail; dbg_view.rail_duty = (uint16_t)rail_duty; dbg_view.vin = cin.vin; dbg_view.bandgap = sin.bandgap;
        dbg_view.tgt = cin.tgt; dbg_view.setpoint = cin.setpoint; dbg_view.mode = mode; dbg_view.iset = iset_user;
        dbg_view.iset_user = iset_user; dbg_view.run = run; dbg_view.stop = stop_reason; dbg_view.alarm = alarm_ui;
        dbg_view.strap = strap; dbg_view.dc = dc; dbg_view.rail_hold = rail_hold; dbg_view.fan = buzz_alarm_level(); dbg_view.usb = usb_configured();
        dbg_view.ilim = lim_cfg.ilim; dbg_view.vmin = lim_cfg.vmin; dbg_view.vcut = lim_cfg.vcut; dbg_view.lmode = lim_cfg.mode;
        dbg_view.est_ma = lim_out.est_ma; dbg_view.budget = lim_out.budget;
        dbg_view.trip = (uint8_t)(stop_reason == STOP_VLIM);
        for (n = 0; n < NPORTS; n++) dbg_view.cap[n] = cin.cap[n];
        ui_text_row(dbg_next_row, dbg_text(dbg_page, dbg_next_row), dbg_next_row == 0 ? th(R_INFO) : th(R_TEXT));       /* one row per pass */
        if (++dbg_next_row == DBG_ROWS) dbg_next_row = 0;
    } else if (menu_on) {
        static const char * XD t;
        t = menu_text(menu_next_row);
        ui_text_row(menu_next_row, t, t[0] == '>' ? th(R_GOOD) : t[0] == '*' ? th(R_WARN) : menu_next_row == 0 ? th(R_INFO) : menu_next_row == MENU_ROWS - 1 ? th(R_DIM) : th(R_TEXT));
        if (++menu_next_row == MENU_ROWS) menu_next_row = 0;
    } else {
        ui_main_step(&ui);                               /* one element per pass: at the bus rate a big one takes ~5 ms */
    }

    g_dbg.magic = 0xD00D; g_dbg.ms = hal_ms(); g_dbg.flags = (uint8_t)(run | (mode << 1) | (stop_reason << 4));
    for (k = 0; k < NPORTS; k++) { g_dbg.state[k] = cout.state[k]; g_dbg.duty[k] = cout.duty[k]; g_dbg.v[k] = cout.v[k]; g_dbg.i[k] = cout.i[k]; }
    g_dbg.rail = cin.rail; g_dbg.rail_duty = (uint16_t)rail_duty; g_dbg.vin = cin.vin;
    g_dbg.iset = iset_user; g_dbg.stop_reason = stop_reason; g_dbg.alarm = alarm_ui;
    tele(PH_RUN);
}
