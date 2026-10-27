#include "dev.h"
#include "hal.h"
#include "adc.h"
#include "ports.h"
#include "ctl.h"
#include "chg.h"
#include "cal.h"
#include "buzz.h"

#define WATCHDOG_MS  500
#define I_MAX        (2 * CUR_MAX + 7)      /* iu: the charger's over-current threshold at the highest setting (2.8 A) */
#define S_MAX        3972                   /* the highest rail setpoint the charger itself uses (4.35 V) */

static uint16_t XD req_duty[NPORTS], req_rail, home;          /* what the host asked for; home: the setpoint at entry */
static uint8_t  XD req_sw, req_flags;
static uint16_t XD out_duty[NPORTS];                           /* what is applied */
static uint8_t  XD out_sw, out_flags, trip, status, raw_now, session;
static int16_t  XD rail_duty;
static uint32_t XD last_cmd;

static void put16(uint8_t XD *b, uint8_t at, uint16_t v) { b[at] = (uint8_t)(v >> 8); b[at + 1] = (uint8_t)v; }

void dev_reset(uint16_t setpoint, int16_t duty, uint8_t flags, uint8_t s)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) req_duty[k] = out_duty[k] = 0;
    req_sw = out_sw = trip = status = raw_now = 0;
    req_flags = out_flags = flags; session = s;
    req_rail = home = setpoint; rail_duty = duty;
    last_cmd = hal_ms();
}

uint8_t dev_taken(uint8_t s) { return s != session && !(status & 1); }

uint8_t dev_set(const uint8_t *c)
{
    uint8_t k;
    uint16_t d;
    if (c[18] != session) return 0;
    if ((c[1] & DF_BEEP) && !(req_flags & DF_BEEP)) buzz_play(PAT_KEY);
    req_flags = c[1]; req_sw = (uint8_t)(c[3] & 0x3F);
    req_rail = ((uint16_t)c[4] << 8) | c[5];
    if (req_rail > (req_flags & DF_RAIL_RAW ? PWM_PERIOD : S_MAX)) req_rail = req_flags & DF_RAIL_RAW ? PWM_PERIOD : S_MAX;
    for (k = 0; k < NPORTS; k++) {
        d = ((uint16_t)c[6 + 2 * k] << 8) | c[7 + 2 * k];
        req_duty[k] = d > PWM_PERIOD ? PWM_PERIOD : d;
        if (!req_duty[k] && !(req_sw & (1u << k))) trip &= (uint8_t)~(1u << k);      /* asked for off: the latch is released */
    }
    last_cmd = hal_ms();
    return 1;
}

uint16_t dev_setpoint(void) { return raw_now || (status & 1) ? home : req_rail; }

int16_t dev_pass(uint8_t supply_stop, uint8_t warm)
{
    uint8_t k, raw, off, was_sw, lost = hal_ms() - last_cmd > WATCHDOG_MS;
    status = (uint8_t)(lost | (supply_stop << 1));
    off = lost || supply_stop;

    /* rail: the host's raw duty, or the PI loop on its setpoint (back on the entry setpoint when the host is gone) */
    raw = !lost && (req_flags & DF_RAIL_RAW);
    if (raw != raw_now) { raw_now = raw; if (!raw) ctl_rail_reset(rail_duty); }
    rail_duty = raw ? (int16_t)req_rail : ctl_rail_step((int16_t)adc_get(ADC_RAIL), (int16_t)(lost ? home : req_rail));
    hal_rail((uint16_t)rail_duty);

    was_sw = out_sw; out_sw = 0;
    for (k = 0; k < NPORTS; k++) {
        if ((out_duty[k] || (was_sw & (1u << k))) && (adc_get(port_v_ch(k)) >= V_FAULT || adc_get(port_i_ch(k)) >= I_MAX)) trip |= (uint8_t)(1u << k);
        out_duty[k] = off || (trip & (1u << k)) ? 0 : req_duty[k];
        if (!off && !(trip & (1u << k)) && (req_sw & (1u << k))) out_sw |= (uint8_t)(1u << k);
        hal_pwm(k, out_duty[k]);
        hal_sw(k, (uint8_t)((out_sw >> k) & 1));
    }
    out_flags = (uint8_t)((req_flags & (DF_PROFILE | DF_BEEP)) | (raw ? DF_RAIL_RAW : 0));
    if (!off && (req_flags & DF_GLOBAL)) out_flags |= DF_GLOBAL;
    if ((req_flags & DF_FAN) || warm) out_flags |= DF_FAN;
    hal_global(out_flags & DF_GLOBAL);
    hal_fan((out_flags & DF_FAN) != 0);
    hal_profile((out_flags & DF_PROFILE) != 0);
    return rail_duty;
}

void dev_tele(uint8_t XD *b)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) {
        put16(b, 8 + 2 * k, adc_raw(port_v_ch(k)));                    /* counts: the host corrects them with the bandgap */
        put16(b, 20 + 2 * k, adc_raw(port_i_ch(k)));
        put16(b, 40 + 2 * k, out_duty[k]);
    }
    put16(b, 32, adc_raw(ADC_IIN)); put16(b, 34, adc_raw(ADC_VIN)); put16(b, 36, adc_raw(ADC_RAIL)); put16(b, 38, adc_raw(ADC_BANDGAP));
    put16(b, 52, (uint16_t)rail_duty);
    b[54] = out_sw; b[55] = out_flags;
    b[56] = (uint8_t)(hal_btn(1) | (hal_btn(2) << 1) | (hal_strap() << 2));
    b[57] = status; b[58] = trip;
    put16(b, 59, dev_setpoint());
}
