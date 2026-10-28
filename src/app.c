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
#include "version.h"
#include "version_build.h"

#define RAIL_UP          3185     /* vu (3.5 V): wait for the rail before starting the display */
#define RAIL_UP_TIMEOUT  10000    /* ms */
#define SELFTEST_LO1     3388     /* phase 1 (servo to 3458) must land in [3388, 3528) */
#define SELFTEST_HI1     3528
#define SELFTEST_LO2     3878     /* phase 2 (servo to 3958) must land in [3878, 4037) */
#define SELFTEST_HI2     4037
#define VCC_OK_MIN       976      /* bandgap count: VCC <= 5.0 V (it never starts above that) */
#define BG_GLITCH        15       /* bandgap counts (~1.4 % of VCC) off the baseline: treat as a supply glitch */
#define LCD_RECONFIG_PASSES 222   /* ~1 s */

static uint8_t  XD rail_hold, run, strap, stop_reason, alarm_ui;   /* alarm_ui: the STOP_* reason the display shows, 0 = none */
static uint32_t XD save_at;                      /* ms: a main-screen change is saved then (0 = nothing to save) */
#define SAVE_DELAY_MS 3000                       /* after the last tap: stepping through values writes the flash once */
static int16_t  XD rail_duty;
static uint16_t XD bg_ref;                     /* slow baseline of the bandgap count: a jump away from it = a supply glitch */
static uint8_t  XD lcd_cnt;
static uint8_t  XD stack_free, stack_div;     /* the stack's never-used bytes, read about once a second */
#define SCR_PORTS 13                            /* screen: 0 = main, 1..DBG_PAGES = a diagnostics page, SCR_PORTS = the port settings */
#define SCR_DEV   14                            /* mode (portset.h, drawn on the main screen), SCR_DEV = developer mode (dev.h), */
#define SCR_MENU  15                            /* SCR_MENU = the menu (as USB byte 12 reports it) */
static uint8_t  XD screen, text_row;            /* text_row: the row of the menu / diagnostics drawn next */
static uint32_t XD t_next;
static chg_in_t  XD cin;
static chg_out_t XD cout;
static ui_state_t XD ui;
static sup_in_t  XD sin;
static sup_out_t XD sout;

/* ------------------------------------------------------------------------------------------------------------- */

/* ---- USB telemetry (layout in USB.md).  The interrupt copies usb_tx to the host; it is rebuilt here, tail byte last. ---- */
enum { PH_RAIL_WAIT = 1, PH_SPLASH, PH_VCC_GATE, PH_SELFTEST, PH_INPUT, PH_RUN = 7, PH_FATAL = 0x80 };
enum { EV_PORT_FULL = 1, EV_PORT_FAULT, EV_STOPPED };           /* ev_code; ev_port = port (0..5) or the stop reason */
static uint8_t XD tele_seq, ev_seq, ev_code, ev_port, cmd_seq, cmd_res;

static uint8_t ch_iset(uint8_t k) { return settings.ch_cur[k] ? settings.ch_cur[k] : settings.cur; }
static void apply_limits(void) { lim_cfg.ilim = settings.ilim; lim_cfg.vmin = settings.vmin; lim_cfg.vcut = settings.vcut; lim_cfg.mode = settings.lmode; }
static void note_event(uint8_t code, uint8_t port) { ev_code = code; ev_port = port; ev_seq++; }

static void put16(uint8_t XD *b, uint8_t at, uint16_t v) { b[at] = (uint8_t)(v >> 8); b[at + 1] = (uint8_t)v; }

/* clear a page and write its head: the tail is zero until the page is complete (torn-copy guard) */
static void tele_head(uint8_t XD *b, uint8_t page, uint32_t ms)
{
    uint8_t k;
    b[63] = 0;
    for (k = 3; k < 63; k++) b[k] = 0;
    b[0] = 0x57; b[1] = 0x33; b[2] = tele_seq; b[3] = page;
    b[4] = (uint8_t)(ms >> 24); b[5] = (uint8_t)(ms >> 16); b[6] = (uint8_t)(ms >> 8); b[7] = (uint8_t)ms;
}

/* page 2: what the limiter and the supervisor are doing (zero before the run phase) */
static void tele_limits(uint8_t ph, uint32_t ms)
{
    uint8_t k;
    tele_head(usb_tx2, 2, ms);
    if (ph >= PH_RUN && ph < PH_FATAL) {
        usb_tx2[8] = lim_cfg.ilim; usb_tx2[9] = lim_cfg.vmin; usb_tx2[10] = lim_cfg.vcut; usb_tx2[11] = lim_cfg.mode;
        put16(usb_tx2, 12, lim_out.budget); put16(usb_tx2, 14, lim_out.p_cur); put16(usb_tx2, 16, lim_out.p_vol);
        put16(usb_tx2, 18, lim_out.est_ma); put16(usb_tx2, 20, lim_out.bcnt);
        usb_tx2[22] = lim_out.trip; usb_tx2[23] = (uint8_t)(run && !rail_hold);        /* the limiter ran this pass */
        for (k = 0; k < NPORTS; k++) {
            usb_tx2[24 + k] = (uint8_t)(cout.held[k] | (cout.up[k] != 0) << 2 | ((cin.rise >> k) & 1) << 3 | (cin.cut == k) << 4);
            usb_tx2[30 + k] = cout.want[k];
            usb_tx2[36 + k] = portset_get(k);
        }
        put16(usb_tx2, 42, sup_setpoint(settings.volt)); put16(usb_tx2, 44, sup_target(settings.volt));    /* vu */
        usb_tx2[46] = stack_free;
        usb_tx2[47] = FW_PRE;
        usb_tx2[51] = cout.any_active;
        usb_tx2[52] = sout.stop; usb_tx2[54] = sout.rail_cut; usb_tx2[55] = sout.stop == STOP_RAIL; usb_tx2[56] = sout.fan;
    }
    put16(usb_tx2, 57, (uint16_t)(FW_BUILD >> 16)); put16(usb_tx2, 59, (uint16_t)FW_BUILD); usb_tx2[61] = FW_DIRTY;   /* the build id */
    usb_tx2[63] = tele_seq;
}

static void tele(uint8_t ph)
{
    uint8_t k;
    uint32_t ms;
    if (!usb_configured()) return;                   /* nobody reads it */
    ms = hal_ms();
    if (++tele_seq == 0) tele_seq = 1;
    tele_head(usb_tx, 1, ms);
    usb_tx[8] = (uint8_t)(run | (settings.volt << 1) | ((run && !rail_hold) << 3) | (rail_hold << 5) | (strap << 6));
    usb_tx[9] = settings.cur; usb_tx[10] = (uint8_t)(FW_MAJOR << 4 | FW_MINOR); usb_tx[11] = stop_reason; usb_tx[12] = (uint8_t)(alarm_ui | (screen << 4));
    usb_tx[13] = ev_seq; usb_tx[14] = ev_code; usb_tx[15] = ev_port; usb_tx[16] = cmd_seq; usb_tx[17] = cmd_res;
    usb_tx[25] = ph;
    if (ph >= PH_RUN && ph < PH_FATAL && screen != SCR_DEV) {          /* developer mode: the ports are on page 3 */
        for (k = 0; k < NPORTS; k++) {
            usb_tx[18 + k] = cout.state[k];
            usb_tx[24] |= (uint8_t)(cout.sw[k] << k);
            put16(usb_tx, 26 + 2 * k, cout.duty[k]);
            put16(usb_tx, 38 + 2 * k, cout.v[k]);
            usb_tx[50 + k] = cout.i[k] > 255 ? 255 : (uint8_t)cout.i[k];
        }
        usb_tx[62] = (uint8_t)(rail_duty >> 2);
    }
    put16(usb_tx, 56, adc_raw(ADC_RAIL)); put16(usb_tx, 58, adc_raw(ADC_VIN)); put16(usb_tx, 60, adc_raw(ADC_BANDGAP));   /* counts */
    usb_tx[63] = tele_seq;
    if (screen == SCR_DEV) { tele_head(usb_tx2, 3, ms); dev_tele(usb_tx2); usb_tx2[63] = tele_seq; }
    else tele_limits(ph, ms);
}

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
    note_event(EV_STOPPED, why);
    chg_abort();
    soft_stop();
    hal_global(0);
}

static void fatal(const char *title, const char *line2, uint8_t has_n, uint16_t n)
{
    hal_all_off();
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
        int16_t S = (int16_t)sup_target(ph ? VOLT_4_35 : VOLT_3_80);
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
    uint8_t n;

    cfg_load();                                      /* flash reads are safe before anything is powered */
    th_set(settings.theme); apply_limits();
    input_reset(); buzz_reset(); buzz_enable(settings.sound);

    hal_power(1);                                    /* main power enable, first on */
    strap = hal_strap();
    if (!strap) hal_profile(1);                      /* P5.3 set when the strap is low */
    rail_duty = 100; hal_rail(100);                  /* rail PWM starts at 100/400 */
    ctl_rail_reset(rail_duty);
    hal_fan(1);                                      /* the fan runs during start-up */
    hal_irq_on();

    adc_prime();
    t0 = hal_ms();
    for (;;) {                                       /* wait for the rail */
        adc_scan();
        tele(PH_RAIL_WAIT);
        if (adc_get(ADC_RAIL) >= RAIL_UP) break;
        if (hal_ms() - t0 > RAIL_UP_TIMEOUT) { lcd_init(); fatal("NO RAIL", "RAIL DID NOT COME UP", 1, adc_get(ADC_RAIL)); }
        hal_delay_ms(2);
    }
    lcd_init();                                      /* for the start-up messages below; may be lost, see the logo */

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
    hal_fan(0);

    /* input voltage: 50 samples, then the input rules */
    for (n = 0; n < 50; n++) { adc_scan(); adc_scan_slow_now(); hal_delay_ms(3); }
    vin = adc_get(ADC_VIN);
    n = 0;
    tele(PH_INPUT);
    while (vin < VIN_MIN) {
        tele(PH_INPUT);
        if (!n) { n = 1; ui_message("ERR:INPUT LOW", "CONNECT INPUT", 1, vin, 0, 0); }
        hal_delay_ms(500);
        adc_scan_slow_now(); adc_scan_slow_now(); vin = adc_get(ADC_VIN);
    }
    /* the logo waits until the start-up load is over: an init made while the fan line is on and the rail is being brought up
       can leave the panel black (cold starts), one made after the self-test always works */
    tele(PH_SPLASH);
    lcd_init();
    ui_splash();
    buzz_play(PAT_BOOT);
    hal_delay_ms(1000);

    sup_reset();

    ui_main_init();
    adc_prime();
    bg_ref = adc_get(ADC_BANDGAP); lcd_cnt = 0;
    t_next = hal_ticks();
    run = 0; stop_reason = 0; alarm_ui = 0; screen = 0;
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
    if (run || sout.stop != STOP_NONE) return 0;
    run = 1; stop_reason = 0; chg_init(); lim_reset();
    return 1;
}

/* ---- screens: the menu, the diagnostics pages it leads to, developer mode ---- */
static void show(uint8_t s)
{
    screen = s; text_row = 0;
    if (s == SCR_DEV) ui_message("DEV MODE", "HOLD BOTH KEYS TO EXIT", 0, 0, 0, 0);
    else ui_text_enter();
}

/* developer mode (dev.h): every output off first, the rail stays regulated where it is */
static void dev_enter(uint8_t session)
{
    if (screen && screen != SCR_DEV) cfg_save();     /* the menu may have been open: its values are applied, keep them */
    outputs_off();
    dev_reset(sup_setpoint(settings.volt), rail_duty, strap ? 0 : DF_PROFILE, session);     /* the profile pin as at boot */
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
        if (run || c[1] < CUR_MIN || c[1] > CUR_MAX || (c[1] - CUR_MIN) % CUR_UNITS_PER_100MA) { res = 1; break; }
        settings.cur = c[1]; cfg_save();
        break;
    case 4:                                                                            /* set the target voltage, not while running */
        if (run || c[1] >= NVOLTS) { res = 1; break; }
        settings.volt = c[1]; cfg_save();
        break;
    case 5: break;                                                                     /* ping */
    case 6: if (run) { res = 1; break; } hal_reboot_isp(); break;                      /* reboot into the USB ISP loader (not while charging) */
    case 7:                                                                            /* developer mode, byte 1 = session (not while charging, */
        if (run || (screen == SCR_DEV && dev_taken(c[1]))) { res = 1; break; }       /* nor while another host drives it) */
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
    cmd_seq = c[2]; cmd_res = res;
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

static void menu_key(uint8_t ev)
{
    static uint8_t XD a;
    a = menu_event(ev);
    if (a == MA_LOCKED) buzz_play(PAT_ERROR);
    else if (a == MA_DIAG) { show(1); buzz_play(PAT_KEY); }
    else if (a == MA_EXIT) { menu_leave(); buzz_play(PAT_KEY); }
    else if (a) buzz_play(PAT_KEY);
}

/* the port settings mode on the main screen: drawn over it, saved when it closes */
static void ports_enter(void) { portset_open(run); screen = SCR_PORTS; ui_main_init(); }
static void ports_leave(void) { cfg_save(); screen = 0; ui_main_init(); }

static void ports_key(uint8_t ev)
{
    static uint8_t XD a;
    a = portset_event(ev);
    if (a == MA_LOCKED) buzz_play(PAT_ERROR);
    else if (a == MA_EXIT) { ports_leave(); buzz_play(PAT_KEY); }
    else if (a) buzz_play(PAT_KEY);
}

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

void app_pass(void)
{
    uint8_t k;

    while ((int32_t)(hal_ticks() - t_next) < 0) ;
    t_next += PASS_TICKS;
    if ((int32_t)(hal_ticks() - t_next) > 4 * PASS_TICKS) t_next = hal_ticks();       /* fell behind: do not burst */

    adc_scan();
    if (!++stack_div) stack_free = hal_stack_free();
    handle_buttons();
    usb_service();
    if (save_at && (int32_t)(hal_ms() - save_at) >= 0) save_main();
    if (screen == SCR_PORTS) {
        portset_context(run);
        if (portset_pass() == MA_EXIT) ports_leave();                       /* 10 s without a key */
    } else if (screen && screen != SCR_DEV) {        /* menu values apply while they are edited; they are saved when the menu closes */
        buzz_enable(settings.sound);
        if (settings.theme != th_get()) { th_set(settings.theme); show(screen); }   /* repaint the menu in the new colours */
        apply_limits();
        menu_context(run);
    }
    if (screen != SCR_DEV) rail_step();              /* developer mode runs the rail itself */

    /* ---- supervision ---- */
    sin.vin = adc_get(ADC_VIN); sin.rail = adc_get(ADC_RAIL); sin.rail_sat = adc_saturated(ADC_RAIL); sin.bandgap = adc_get(ADC_BANDGAP); sin.adc0 = adc_get(0);
    sin.setpoint = screen == SCR_DEV ? dev_setpoint() : sup_setpoint(settings.volt);
    sup_pass(&sin, &sout);

    /* display self-healing: re-send the panel settings about once a second; after a sudden VCC dip (bandgap count jumps off its
       baseline) also repaint everything, since a glitch can scramble the panel's rotation, colours and picture */
    {
        int16_t d = (int16_t)sin.bandgap - (int16_t)bg_ref;
        bg_ref = (uint16_t)((int16_t)bg_ref + (d >> 5));
        if (d > BG_GLITCH || d < -BG_GLITCH) {
            bg_ref = sin.bandgap;
            lcd_wake(); hal_delay_ms(5); lcd_reconfig();
            if (screen) show(screen); else ui_main_init();
        } else if (++lcd_cnt == LCD_RECONFIG_PASSES - 2) {
            lcd_wake();                              /* the settings follow two passes (9 ms) later: no busy wait */
        } else if (lcd_cnt >= LCD_RECONFIG_PASSES) {
            lcd_cnt = 0;
            lcd_reconfig();
        }
    }

    alarm_ui = sout.stop ? sout.stop : !run && stop_reason == STOP_VLIM ? STOP_VLIM : STOP_NONE;
    if (run && sout.stop != STOP_NONE && sout.stop != STOP_RAIL) stop_run(sout.stop);
    /* a rail held over-voltage means the regulation failed: nothing may be driven again until the power is cycled */
    if (sout.rail_cut) fatal("RAIL OVERVOLTAGE", "ADC FULL, POWER CYCLE", 1, adc_raw(ADC_RAIL));
    /* the rail out of band for long is a pause, not a stop: nothing is driven, charging resumes by itself with a fresh soft ramp */
    rail_hold = (uint8_t)(run && sout.stop == STOP_RAIL);
    if (screen == SCR_DEV) {                         /* developer mode: the host drives the outputs, the guards in dev.c */
        rail_duty = dev_pass(sout.stop == STOP_VIN || sout.stop == STOP_VCC, sout.fan);
        tele(PH_RUN);
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
            if (cout.event[k] == CE_FULL) { buzz_play(PAT_DONE); note_event(EV_PORT_FULL, k); }
            if (cout.event[k] == CE_FAULT) { buzz_play(PAT_ERROR); note_event(EV_PORT_FAULT, k); }
        }
    } else if (!rail_hold) {
        outputs_off();
    }

    /* ---- display ---- */
    ui.run = run; ui.volt = settings.volt; ui.iset = settings.cur; ui.vin = sin.vin; ui.tgt = cin.tgt; ui.alarm = alarm_ui;
    ui.blink = (uint8_t)((hal_ms() >> 9) & 1);
    ui.tphase = (uint8_t)(((hal_ms() >> 10) & 3) == 3);
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
        dbg_view.tgt = cin.tgt; dbg_view.setpoint = cin.setpoint; dbg_view.volt = settings.volt; dbg_view.iset = settings.cur;
        dbg_view.run = run; dbg_view.stop = stop_reason; dbg_view.alarm = alarm_ui;
        dbg_view.strap = strap; dbg_view.stack = stack_free; dbg_view.rail_hold = rail_hold; dbg_view.fan = sout.fan; dbg_view.usb = usb_configured();
        dbg_view.ilim = lim_cfg.ilim; dbg_view.vmin = lim_cfg.vmin; dbg_view.vcut = lim_cfg.vcut; dbg_view.lmode = lim_cfg.mode;
        dbg_view.est_ma = lim_out.est_ma; dbg_view.budget = lim_out.budget;
        dbg_view.bcnt = lim_out.bcnt;
        for (n = 0; n < NPORTS; n++) dbg_view.held[n] = cout.held[n];
        ui_text_row(text_row, dbg_text(screen, text_row), text_row == 0 ? th(R_INFO) : th(R_TEXT));       /* one row per pass */
        if (++text_row == DBG_ROWS) text_row = 0;
    } else if (screen == SCR_MENU) {
        static const char * XD t;
        t = menu_text(text_row);
        ui_text_row(text_row, t, t[0] == '>' ? th(R_GOOD) : t[0] == '*' ? th(R_WARN) : text_row == 0 ? th(R_INFO) : text_row == MENU_ROWS - 1 ? th(R_DIM) : th(R_TEXT));
        if (++text_row == MENU_ROWS) text_row = 0;
    } else {
        ui_main_step(&ui);                               /* one element per pass: at the bus rate a big one takes ~5 ms */
    }

    tele(PH_RUN);
}
