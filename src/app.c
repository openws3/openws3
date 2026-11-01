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
#include "dev.h"
#include "portset.h"
#include "tele.h"

#define BOOT_RAIL_PASSES 150      /* the rail is brought up under the regulator for this many 2 ms steps before the display starts */
#define TEST_TRIES       3        /* the rail self-test: failed this many times with the rail up, it is fatal */
#define C_TESTING        8        /* the waiting condition between two tries of it (with STOP_VIN .. STOP_RAIL, SF_*) */
#define SELFTEST_TOL1    70       /* phase 1 (servo to the 3.80 V target) must land within this many counts of it ... */
#define SELFTEST_TOL2    80       /* ... phase 2 (servo to the 4.35 V target) within this many */
#define BG_GLITCH        15       /* bandgap counts (~1.4 % of VCC) off the baseline: treat as a supply glitch */
#define LCD_RECONFIG_PASSES 222   /* ~1 s */

static uint8_t  XD rail_hold, run, strap, stop_reason, alarm_ui;   /* alarm_ui: the STOP_* reason the display shows, 0 = none */
static uint32_t XD save_at;                      /* ms: a main-screen change is saved then (0 = nothing to save) */
#define SAVE_DELAY_MS 1500                       /* after the last tap: stepping through values writes the flash once */
static int16_t  XD rail_duty;
static uint16_t XD bg_ref;                     /* slow baseline of the bandgap count: a jump away from it = a supply glitch */
static uint8_t  XD lcd_cnt, bg_quiet;
static uint8_t  XD stack_free, stack_div;     /* the stack's never-used bytes, read about once a second */
static uint8_t  XD screen, text_row;          /* text_row: the row of the menu / diagnostics drawn next */
static uint8_t  XD fatal_code, cond, msg_code, msg_tick;   /* sup.h SF_* latched until the power is cycled; the condition now (SF_* or STOP_*), 0 = none; the one on the screen */
static uint8_t  XD test_ok, test_tries;       /* the rail self-test: passed, failed so far */
static uint32_t XD test_at;                   /* ms: the next try */
static uint16_t XD t1, t2;                    /* its last two readings */
static uint32_t XD t_next;
static chg_in_t  XD cin;
static chg_out_t XD cout;
static ui_state_t XD ui;
static sup_in_t  XD sin;
static sup_out_t XD sout;
static tele_ctx_t XD tctx;                    /* what the telemetry reads (tele.h): pointers into the state above */

/* ------------------------------------------------------------------------------------------------------------- */

static uint8_t ch_iset(uint8_t k) { return settings.ch_cur[k] ? settings.ch_cur[k] : settings.cur; }
static void apply_limits(void) { lim_cfg.ilim = settings.ilim; lim_cfg.vmin = settings.vmin; lim_cfg.vcut = settings.vcut; lim_cfg.mode = settings.lmode; }

static void outputs_off(void)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) { hal_pwm(k, 0); hal_sw(k, 0); }
    hal_global(0);
}

static void rail_step(void)
{
    rail_duty = ctl_rail_step((int16_t)adc_get(ADC_RAIL), (int16_t)sup_setpoint(settings.volt));
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
    tele_event(EV_STOPPED, why);
    chg_abort();
    soft_stop();
    hal_global(0);
}

static char XD v1[8], v2[8];                         /* the numbers of a condition screen, in volts */

static void cond_numbers(uint8_t c)
{
    switch (c) {
    case SF_VCC_HIGH: case STOP_VCC: fmt_v2(v1, dbg_vcc_mv(adc_get(ADC_BANDGAP))); break;
    case STOP_VIN:                   fmt_vin(v1, cal_vin_mv(adc_get(ADC_VIN))); break;
    case SF_RAIL_OVER:               fmt_v2(v1, cal_port_mv(adc_raw(ADC_RAIL))); break;
    case SF_TEST: case C_TESTING:    fmt_v2(v1, cal_port_mv(t1)); fmt_v2(v2, cal_port_mv(t2)); break;
    default:                         fmt_v2(v1, cal_port_mv(adc_get(ADC_RAIL))); break;
    }
}

/* The screen of a condition (sup.h): red for a fatal one, amber for one that waits to clear. */
static void cond_draw(uint8_t c)
{
    cond_numbers(c);
    switch (c) {
    case SF_VCC_HIGH:  ui_message(R_BAD, "VCC HIGH", "OVER 5.0V, POWER CYCLE", v1, 0); break;
    case SF_RAIL_OVER: ui_message(R_BAD, "RAIL OVERVOLTAGE", "ADC FULL, POWER CYCLE", v1, 0); break;
    case SF_NO_RAIL:   ui_message(R_BAD, "NO RAIL", "NOT UP, POWER CYCLE", v1, 0); break;
    case SF_TEST:      ui_message(R_BAD, "RAIL TEST FAIL", "POWER CYCLE", v1, v2); break;
    case STOP_VIN:     ui_message(R_WAIT, "INPUT LOW", settings.usb_off ? "USB POWER OFF, USE XT60" : "CHECK THE SUPPLY", v1, 0); break;
    case STOP_VCC:     ui_message(R_WAIT, "VCC LOW", "TRY ANOTHER SUPPLY", v1, 0); break;
    case STOP_RAIL:    ui_message(R_WAIT, "RAIL OUT OF RANGE", "WAITING TO RECOVER", v1, 0); break;
    case C_TESTING:    ui_message(R_WAIT, "RAIL TEST", "FAILED, TRYING AGAIN", v1, v2); break;
    }
}

/* A fatal condition: everything off, and it stays so until the power is cycled.  The loop goes on (fatal_pass): the host can still read
 * the unit, stop it, or flash it. */
static void go_fatal(uint8_t code)
{
    run = 0; chg_abort();
    hal_all_off();
    screen = 0; fatal_code = code;
    buzz_play(PAT_ERROR);
}

/* ------------------------------------------------------------------------------------------------------------- */
static uint8_t within(uint16_t r, uint16_t t, uint16_t tol) { return r + tol >= t && r < t + tol; }

static uint8_t selftest(uint16_t *r1, uint16_t *r2)
{
    uint8_t ph, n;
    ctl_rail_reset(rail_duty);
    for (ph = 0; ph < 2; ph++) {
        int16_t S = (int16_t)sup_target(ph ? VOLT_4_35 : VOLT_3_80);
        for (n = 0; n < 100; n++) {
            adc_scan();
            rail_duty = ctl_rail_step((int16_t)adc_get(ADC_RAIL), S);
            hal_rail((uint16_t)rail_duty);
            hal_delay_ms(2);
        }
        if (ph == 0) *r1 = adc_get(ADC_RAIL); else *r2 = adc_get(ADC_RAIL);
    }
    return within(*r1, sup_target(VOLT_3_80), SELFTEST_TOL1) && within(*r2, sup_target(VOLT_4_35), SELFTEST_TOL2);
}

void app_boot(void)
{
    uint8_t n;

    cfg_load();                                      /* flash reads are safe before anything is powered */
    th_set(settings.theme); apply_limits();
    input_reset(); buzz_reset(); buzz_enable(settings.sound);

    tctx.out = &cout; tctx.in = &cin; tctx.sup = &sout; tctx.rail_duty = &rail_duty;
    tctx.screen = &screen; tctx.run = &run; tctx.rail_hold = &rail_hold; tctx.strap = &strap;
    tctx.stop = &stop_reason; tctx.alarm = &alarm_ui; tctx.stack = &stack_free;
    tele_init(&tctx);

    hal_power(1);                                    /* main power enable, first on */
    strap = hal_strap();
    if (!strap) hal_profile(1);                      /* P5.3 set when the strap is low */
    rail_duty = 100; hal_rail(100);                  /* rail PWM starts at 100/400 */
    ctl_rail_reset(rail_duty);
    hal_irq_on();

    adc_prime();
    for (n = 0; n < BOOT_RAIL_PASSES; n++) {         /* the rail comes up under the regulator; what is wrong with the supply or the rail is the main loop's */
        adc_scan(); rail_step();
        tele_pass(PH_RAIL_WAIT);
        hal_delay_ms(2);
    }
    lcd_init();                                      /* may be lost, see the logo */

    /* the logo waits until the rail has settled: an init made while the rail is being brought up
       can leave the panel black (cold starts), one made after it always worked */
    tele_pass(PH_SPLASH);
    lcd_init();
    ui_splash();
    buzz_play(PAT_BOOT);
    hal_delay_ms(1000);

    sup_reset();

    ui_main_init();
    adc_prime();
    bg_ref = adc_get(ADC_BANDGAP); lcd_cnt = 0; bg_quiet = 0;
    t_next = hal_ticks();
    run = 0; stop_reason = 0; alarm_ui = 0; screen = 0;
    fatal_code = cond = msg_code = 0; test_ok = test_tries = 0; test_at = 0;
    lim_reset();
    chg_init();
    stack_free = hal_stack_free();                   /* after the boot, which may go deepest; then about once a second */
}

/* ------------------------------------------------------------------------------------------------------------- */
/* main-screen changes: written once, a while after the last tap or when charging starts */
static void save_main(void) { save_at = 0; cfg_save(); }
static void save_later(void) { save_at = hal_ms() + SAVE_DELAY_MS; if (!save_at) save_at = 1; }

static uint8_t start_run(void)
{
    if (save_at) save_main();
    if (run || sout.stop != STOP_NONE || fatal_code || !test_ok) return 0;
    run = 1; stop_reason = 0; chg_init(); lim_reset();
    return 1;
}

/* ---- screens: the menu, the diagnostics pages it leads to, developer mode ---- */
static void show(uint8_t s)
{
    screen = s; text_row = 0;
    if (s == SCR_DEV) ui_message(R_BAD, "DEV MODE", "HOLD BOTH KEYS TO EXIT", 0, 0);
    else ui_text_enter();
}

/* developer mode (dev.h): every output off first, the rail stays regulated where it is */
static void dev_enter(uint8_t session)
{
    if (screen && screen != SCR_DEV) cfg_save();     /* the menu may have been open: its values are applied, keep them */
    outputs_off();
    dev_reset(sup_setpoint(settings.volt), rail_duty, strap || settings.usb_off ? 0 : DF_PROFILE, session);     /* the profile pin as at boot */
    if (screen != SCR_DEV) show(SCR_DEV);
}

static void dev_leave(void)
{
    outputs_off();
    hal_profile(!strap);
    ctl_rail_reset(rail_duty);
    chg_init(); lim_reset();
    screen = 0;
    ui_main_init();
    buzz_play(PAT_KEY);
}

/* host commands (USB.md): executed here, in the main loop, with the same rules as the buttons */
static void usb_service(void)
{
    uint8_t c[USB_CMD], res = 0;
    if (!usb_cmd_get(c)) return;
    switch (c[0]) {
    case 1: if (run) stop_run(STOP_USER); break;                                       /* stop (always allowed) */
    case 2: res = screen != SCR_DEV && start_run() ? 0 : 1; break;                    /* start (not in developer mode) */
    case 3:                                                                            /* set current (units), not while running */
        if (run || !cal_cur_valid(c[1])) { res = 1; break; }
        settings.cur = c[1]; cfg_save();
        break;
    case 4:                                                                            /* set the target voltage, not while running */
        if (run || c[1] >= NVOLTS) { res = 1; break; }
        settings.volt = c[1]; cfg_save();
        break;
    case 5: break;                                                                     /* ping */
    case 6: if (run) { res = 1; break; } hal_reboot_isp(); break;                      /* reboot into the USB ISP loader (not while charging) */
    case 7:                                                                            /* developer mode, byte 1 = session (not while charging, */
        if (run || fatal_code || (screen == SCR_DEV && dev_taken(c[1]))) { res = 1; break; }   /* nor while another host drives it) */
        dev_enter(c[1]);
        break;
    case 8: if (screen != SCR_DEV || !dev_set(c)) res = 1; break;                     /* developer mode: the outputs (the owner only) */
    case 9: if (screen == SCR_DEV) dev_leave(); break;                                 /* leave developer mode */
    case 10:                                                                           /* one port's value (portset.h): byte 1 = port, */
        if (run || c[1] >= NPORTS || !portset_valid(c[3]) || (screen == SCR_PORTS && portset_editing())) { res = 1; break; }   /* byte 3 */
        portset_put(c[1], c[3]); cfg_save();
        break;
    default: res = 2; break;
    }
    tele_cmd(c[2], res);
}

static void menu_enter(void)
{
    menu_open(run);
    show(SCR_MENU);
}

/* close: the values were applied live, now they are saved (the page is written only when something changed) */
static void menu_leave(void)
{
    cfg_save();
    screen = 0;
    ui_main_init();
}

/* the beep and the close for a menu-like mode; MA_DIAG opens the diagnostics (only the menu can) */
static void mode_result(uint8_t a, void (*open)(void), void (*leave)(void))
{
    if (a == MA_LOCKED) buzz_play(PAT_ERROR);
    else if (a == MA_DIAG && open) { open(); buzz_play(PAT_KEY); }
    else if (a == MA_EXIT) { leave(); buzz_play(PAT_KEY); }
    else if (a) buzz_play(PAT_KEY);
}

static void diag_open(void) { show(1); }

static void menu_key(uint8_t ev)  { mode_result(menu_event(ev), diag_open, menu_leave); }

/* the port settings mode on the main screen: drawn over it, saved when it closes */
static void ports_enter(void) { portset_open(run); screen = SCR_PORTS; ui_main_init(); }
static void ports_leave(void) { cfg_save(); screen = 0; ui_main_init(); }

static void ports_key(uint8_t ev) { mode_result(portset_event(ev), 0, ports_leave); }

static void handle_buttons(void)
{
    uint8_t ev;
    input_style(screen ? IS_NAV : IS_MAIN);
    while ((ev = input_get()) != EV_NONE) {
        if (screen == SCR_DEV) { if (ev == EV_BACK) dev_leave(); continue; }     /* the buttons are only watched (page 3) */
        if (screen == SCR_MENU) { menu_key(ev); continue; }
        if (screen == SCR_PORTS) { ports_key(ev); continue; }
        if (screen) {                                    /* diagnostics: right = next page, left = previous, BACK = the menu */
            if (ev == EV_B2) show((uint8_t)(screen % DBG_PAGES + 1));
            else if (ev == EV_B1) show((uint8_t)(screen == 1 ? DBG_PAGES : screen - 1));
            else if (ev == EV_BACK) { show(SCR_MENU); buzz_play(PAT_KEY); }
            continue;
        }
        if (cond) {                                      /* a condition is on the screen: the menu is the way out (DISABLE USB POWER) */
            if (ev == EV_OK) { menu_enter(); buzz_play(PAT_KEY); }
            continue;
        }
        switch (ev) {
        case EV_B1:
            if (!run) {
                settings.cur = cal_ma_step_up(settings.cur);
                buzz_play(PAT_KEY);
                save_later();
            }
            break;
        case EV_B1_LONG:
            if (run) stop_run(STOP_USER); else start_run();
            buzz_play(PAT_KEY);
            break;
        case EV_OK: menu_enter(); buzz_play(PAT_KEY); break;
        case EV_B2_LONG: ports_enter(); buzz_play(PAT_KEY); break;
        case EV_B2:
            if (!run) {                                  /* no voltage change while charging */
                settings.volt = (uint8_t)((settings.volt + 1) % NVOLTS);
                buzz_play(PAT_KEY);
                save_later();
            }
            break;
        }
    }
}

/* The rail self-test (rail authority: it must servo to the 3.80 V and 4.35 V levels), once, as soon as nothing waits and the rail is up.  A
 * low input is not a failure of it: that waits first.  Starting a run needs it passed. */
static void test_step(void)
{
    if (selftest(&t1, &t2)) test_ok = 1;
    else if (++test_tries >= TEST_TRIES) go_fatal(SF_TEST);
    else test_at = hal_ms() + 1000;
    sup_reset();                                     /* the rail went through two levels: its counters start again */
}

/* A fatal condition: nothing is driven, nothing but the host link works. */
static void fatal_pass(void)
{
    hal_all_off();
    while (input_get() != EV_NONE) ;
    usb_service();
    if (msg_code != fatal_code) { cond_draw(fatal_code); msg_code = fatal_code; }
    tele_pass(PH_FATAL);
}

void app_pass(void)
{
    uint8_t k;

    while ((int32_t)(hal_ticks() - t_next) < 0) ;
    t_next += PASS_TICKS;
    if ((int32_t)(hal_ticks() - t_next) > 4 * PASS_TICKS) t_next = hal_ticks();       /* fell behind: do not burst */

    adc_scan();
    if (!++stack_div) stack_free = hal_stack_free();
    if (fatal_code) { fatal_pass(); return; }
    handle_buttons();
    usb_service();
    if (save_at && (int32_t)(hal_ms() - save_at) >= 0) save_main();
    if (screen == SCR_PORTS) {
        portset_context(run);
        if (portset_pass() == MA_EXIT) ports_leave();                       /* 10 s without a key */
    } else if (screen && screen != SCR_DEV) {        /* menu values apply while they are edited; they are saved when the menu closes */
        buzz_enable(settings.sound);
        if (settings.usb_off) hal_profile(0);        /* DISABLE USB POWER takes effect at once; turning it off again waits for the next start */
        if (settings.theme != th_get()) { th_set(settings.theme); show(screen); }   /* repaint the menu in the new colours */
        apply_limits();
        menu_context(run);
    }
    if (screen != SCR_DEV) rail_step();              /* developer mode runs the rail itself */

    /* ---- supervision ---- */
    sin.vin = adc_get(ADC_VIN); sin.rail = adc_get(ADC_RAIL); sin.rail_sat = adc_saturated(ADC_RAIL); sin.bandgap = adc_get(ADC_BANDGAP); sin.adc0 = adc_get(0);
    sin.setpoint = screen == SCR_DEV ? dev_setpoint() : sup_setpoint(settings.volt);
    sup_pass(&sin, &sout);
    if (sout.fatal) { go_fatal(sout.fatal); return; }
    cond = sout.stop;                                /* STOP_VIN, STOP_VCC, STOP_RAIL: waiting, on the screen */
    if (!test_ok && screen != SCR_DEV && !run && !cond && sout.rail_up && (int32_t)(hal_ms() - test_at) >= 0) test_step();
    if (!cond && !test_ok && test_tries) cond = C_TESTING;

    /* display self-healing: re-send the panel settings about once a second; after a sudden VCC dip (bandgap count jumps off its
       baseline) also repaint everything, since a glitch can scramble the panel's rotation, colours and picture */
    {
        int16_t d = (int16_t)sin.bandgap - (int16_t)bg_ref;
        bg_ref = (uint16_t)((int16_t)bg_ref + (d >> 5));
        if (bg_quiet) bg_quiet--;
        if ((d > BG_GLITCH || d < -BG_GLITCH) && !bg_quiet) {         /* at most once a second: a supply falling over many steps would repaint all the way */
            bg_ref = sin.bandgap; bg_quiet = 222;
            lcd_wake(); hal_delay_ms(5); lcd_reconfig();
            if (screen) show(screen); else if (!cond) ui_main_init();
            msg_code = 0;                            /* a condition on the screen is drawn again */
        } else if (++lcd_cnt == LCD_RECONFIG_PASSES - 2) {
            lcd_wake();                              /* the settings follow two passes (9 ms) later: no busy wait */
        } else if (lcd_cnt >= LCD_RECONFIG_PASSES) {
        if (settings.usb_off) hal_profile(0);        /* DISABLE USB POWER takes effect at once; turning it off again waits for the next start */
            lcd_cnt = 0;
            lcd_reconfig();
        }
    }

    alarm_ui = sout.stop ? sout.stop : !run && stop_reason == STOP_VLIM ? STOP_VLIM : STOP_NONE;
    if (run && sout.stop != STOP_NONE && sout.stop != STOP_RAIL) stop_run(sout.stop);
    /* the rail out of band for long is a pause, not a stop: nothing is driven, charging resumes by itself with a fresh soft ramp */
    rail_hold = (uint8_t)(run && sout.stop == STOP_RAIL);
    if (screen == SCR_DEV) {                         /* developer mode: the host drives the outputs, the guards in dev.c */
        rail_duty = dev_pass(sout.stop == STOP_VIN || sout.stop == STOP_VCC, sout.fan);
        tele_pass(PH_RUN);
        return;
    }
    hal_fan(sout.fan);
    if (sout.stop == STOP_RAIL && !buzz_busy()) buzz_play(PAT_RAIL_ALARM);

    /* ---- user limits (limits.h): the gate on the port duties, or a stop when a STOP-mode limit is exceeded ---- */
    if (run && !rail_hold) {
        for (k = 0; k < NPORTS; k++) {
            lim_in.charging[k] = cout.state[k] == PS_CHARGE; lim_in.want[k] = cout.want[k];
            lim_in.v[k] = cout.v[k]; lim_in.i[k] = cout.i[k]; lim_in.up[k] = cout.up[k];
        }
        lim_in.rail = adc_get(ADC_RAIL); lim_in.vin = adc_get(ADC_VIN);
        lim_pass();
        if (lim_out.trip) { stop_run(STOP_VLIM); buzz_play(PAT_ERROR); }
    } else {
        lim_reset();
    }
    cin.lim = lim_out.on; cin.rise = lim_out.rise; cin.cut = lim_out.cut; cin.excess = lim_out.excess;

    /* ---- charge control ---- */
    for (k = 0; k < NPORTS; k++) { cin.v_now[k] = adc_now(port_v_ch(k)); cin.i_now[k] = adc_now(port_i_ch(k)); }
    cin.rail = adc_get(ADC_RAIL);
    cin.tgt = sup_target(settings.volt); cin.setpoint = sup_setpoint(settings.volt); cin.run = run;
    for (k = 0; k < NPORTS; k++) { cin.iset[k] = ch_iset(k); cin.en[k] = !(settings.ch_off & (1u << k)); }
    if (rail_hold) { outputs_off(); chg_cut(); }
    else chg_pass(&cin, &cout);

    if (run && !rail_hold) {
        hal_global(1);
        for (k = 0; k < NPORTS; k++) {
            hal_pwm(k, cout.duty[k]);
            hal_sw(k, cout.sw[k]);
            if (cout.event[k] == CE_FULL) { buzz_play(PAT_DONE); tele_event(EV_PORT_FULL, k); }
            if (cout.event[k] == CE_FAULT) { buzz_play(PAT_ERROR); tele_event(EV_PORT_FAULT, k); }
        }
    } else if (!rail_hold) {
        outputs_off();
    }

    /* ---- display ---- */
    ui.run = run; ui.volt = settings.volt; ui.iset = settings.cur; ui.vin = sin.vin; ui.tgt = cin.tgt; ui.alarm = alarm_ui == STOP_VLIM ? STOP_VLIM : STOP_NONE;      /* the others are screens of their own */
    ui.blink = (uint8_t)((hal_ms() >> 9) & 1);
    ui.tphase = (uint8_t)(((hal_ms() >> 10) & 3) == 3);
    ui.usb_off = settings.usb_off;
    ui.ps = screen != SCR_PORTS ? 0 : portset_editing() ? 2 : 1; ui.sel = portset_port(); ui.locked = portset_locked(); ui.mixed = 0;
    for (k = 0; k < NPORTS; k++) {
        ui.state[k] = cout.state[k]; ui.v[k] = cout.v[k]; ui.i[k] = cout.i[k]; ui.parked[k] = (uint8_t)(cout.held[k] == 2);
        ui.set[k] = portset_get(k); ui.want[k] = cout.state[k] == PS_CHARGE ? cout.want[k] : ch_iset(k);
        if (ui.set[k] != PV_AUTO) ui.mixed = 1;
    }
    if (screen && screen != SCR_MENU && screen != SCR_PORTS) {
        uint8_t n;
        dbg_view.ms = hal_ms();
        for (n = 0; n < NADC; n++) dbg_view.adc[n] = adc_raw(n);
        for (n = 0; n < NPORTS; n++) {
            dbg_view.v[n] = cout.v[n]; dbg_view.i[n] = cout.i[n]; dbg_view.duty[n] = cout.duty[n];
            dbg_view.state[n] = cout.state[n]; dbg_view.sw[n] = cout.sw[n];
        }
        dbg_view.rail = cin.rail; dbg_view.rail_duty = (uint16_t)rail_duty; dbg_view.vin = sin.vin; dbg_view.bandgap = sin.bandgap;
        dbg_view.run = run; dbg_view.stop = stop_reason; dbg_view.alarm = alarm_ui;
        dbg_view.strap = strap; dbg_view.stack = stack_free; dbg_view.rail_hold = rail_hold; dbg_view.fan = sout.fan; dbg_view.usb = usb_configured();
        for (n = 0; n < NPORTS; n++) dbg_view.held[n] = cout.held[n];
        ui_text_row(text_row, dbg_text(screen, text_row), text_row == 0 ? th(R_INFO) : th(R_TEXT));       /* one row per pass */
        if (++text_row == DBG_ROWS) text_row = 0;
        msg_code = 0;
    } else if (screen == SCR_MENU) {
        static const char * XD t;
        t = menu_text(text_row);
        ui_text_row(text_row, t, t[0] == '>' ? th(R_GOOD) : t[0] == '*' ? th(R_WARN) : text_row == 0 ? th(R_INFO) : text_row == MENU_ROWS - 1 ? th(R_DIM) : th(R_TEXT));
        if (++text_row == MENU_ROWS) text_row = 0;
        msg_code = 0;
    } else if (cond && screen == 0) {
        if (msg_code != cond) { cond_draw(cond); msg_code = cond; }     /* once: the screen of the condition replaces the main screen */
        else if (cond != C_TESTING && (uint8_t)(hal_ms() >> 8) != msg_tick) { msg_tick = (uint8_t)(hal_ms() >> 8); cond_numbers(cond); ui_message_big(v1); }   /* its number, ~4 times a second */
    } else {
        if (msg_code) { msg_code = 0; ui_main_init(); }              /* the condition is over: the main screen again */
        ui_main_step(&ui);                               /* one element per pass: at the bus rate a big one takes ~5 ms */
    ui.usb_off = settings.usb_off;
    }

    tele_pass(PH_RUN);
}
