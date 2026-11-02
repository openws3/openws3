/*
 * USB telemetry pages (layout in USB.md).  The interrupt copies the usb_page buffers to the host; they are rebuilt here, tail byte
 * last.  The source data is the app's live state, reached through a context of pointers set once at boot (tele_init), so nothing
 * is copied: the packets always show what the app is doing.
 */
#include "tele.h"
#include "usb.h"
#include "hal.h"
#include "adc.h"
#include "cfg.h"
#include "limits.h"
#include "portset.h"
#include "dev.h"
#include "app.h"
#include "version.h"
#include "version_build.h"

static tele_ctx_t XD ctx;
static uint8_t XD tele_seq, ev_seq, ev_code, ev_port, cmd_seq, cmd_res;

void tele_put16(uint8_t XD *b, uint8_t at, uint16_t v) { b[at] = (uint8_t)(v >> 8); b[at + 1] = (uint8_t)v; }
static void tele_put32(uint8_t XD *b, uint8_t at, uint32_t v) REENT { tele_put16(b, at, (uint16_t)(v >> 16)); tele_put16(b, at + 2, (uint16_t)v); }

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
    uint8_t XD *b = usb_page[UP_LIMITS];
    tele_head(b, 2, ms);
    if (ph >= PH_RUN && ph < PH_FATAL) {
        b[8] = lim_cfg.ilim; b[9] = lim_cfg.vmin; b[10] = lim_cfg.vcut; b[11] = lim_cfg.mode;
        tele_put16(b, 12, lim_out.budget); tele_put16(b, 14, lim_out.p_cur); tele_put16(b, 16, lim_out.p_vol);
        tele_put16(b, 18, lim_out.est_ma); tele_put16(b, 20, lim_out.bcnt);
        b[22] = lim_out.trip; b[23] = (uint8_t)(*ctx.run && !*ctx.rail_hold);        /* the limiter ran this pass */
        for (k = 0; k < NPORTS; k++) {
            b[24 + k] = (uint8_t)(ctx.out->held[k] | (ctx.out->up[k] != 0) << 2 | ((ctx.in->rise >> k) & 1) << 3 | (ctx.in->cut == k) << 4);
            b[30 + k] = ctx.out->want[k];
            b[36 + k] = portset_get(k);
        }
        tele_put16(b, 42, sup_setpoint(settings.volt)); tele_put16(b, 44, sup_target(settings.volt));    /* vu */
        b[46] = *ctx.stack;
        b[47] = FW_PRE;
        b[51] = ctx.out->any_active;
        b[52] = ctx.sup->stop; b[54] = ctx.sup->fatal; b[55] = ctx.sup->stop == STOP_RAIL; b[56] = ctx.sup->fan;
    }
    tele_put16(b, 57, (uint16_t)(FW_BUILD >> 16)); tele_put16(b, 59, (uint16_t)FW_BUILD); b[61] = FW_DIRTY;   /* the build id */
    b[63] = tele_seq;
}

/* page 4: what each cell has had (chg_out_t): the charge, 32 bit; the voltage it started from; the bleed's estimated share, the top
   16 bits (zero before the run phase) */
static void tele_charge(uint8_t ph, uint32_t ms) REENT
{
    uint8_t k;
    uint8_t XD *b = usb_page[UP_CHARGE];
    tele_head(b, 4, ms);
    if (ph >= PH_RUN && ph < PH_FATAL) {
        for (k = 0; k < NPORTS; k++) {
            tele_put32(b, 8 + 4 * k, ctx.out->charge[k]);
            tele_put16(b, 32 + 2 * k, ctx.out->v_start[k]);
            tele_put16(b, 44 + 2 * k, (uint16_t)(ctx.out->bled[k] >> 16));
        }
    }
    b[63] = tele_seq;
}

void tele_pass(uint8_t ph)
{
    uint8_t k;
    uint32_t ms;
    uint8_t XD *b = usb_page[UP_MAIN];
    if (!usb_configured()) return;                   /* nobody reads it */
    ms = hal_ms();
    if (++tele_seq == 0) tele_seq = 1;
    tele_head(b, 1, ms);
    b[8] = (uint8_t)(*ctx.run | (settings.volt << 1) | ((*ctx.run && !*ctx.rail_hold) << 3) | (*ctx.rail_hold << 5) | (*ctx.strap << 6));
    b[9] = settings.cur; b[10] = (uint8_t)(FW_MAJOR << 4 | FW_MINOR); b[11] = *ctx.stop; b[12] = (uint8_t)(*ctx.alarm | (*ctx.screen << 4));
    b[13] = ev_seq; b[14] = ev_code; b[15] = ev_port; b[16] = cmd_seq; b[17] = cmd_res;
    b[25] = ph;
    if (ph >= PH_RUN && ph < PH_FATAL && *ctx.screen != SCR_DEV) {          /* developer mode: the ports are on page 3 */
        for (k = 0; k < NPORTS; k++) {
            b[18 + k] = ctx.out->state[k];
            b[24] |= (uint8_t)(ctx.out->sw[k] << k);
            tele_put16(b, 26 + 2 * k, ctx.out->duty[k]);
            tele_put16(b, 38 + 2 * k, ctx.out->v[k]);
            b[50 + k] = ctx.out->i[k] > 255 ? 255 : (uint8_t)ctx.out->i[k];
        }
        b[62] = (uint8_t)(*ctx.rail_duty >> 2);
    }
    tele_put16(b, 56, adc_raw(ADC_RAIL)); tele_put16(b, 58, adc_raw(ADC_VIN)); tele_put16(b, 60, adc_raw(ADC_BANDGAP));   /* counts */
    b[63] = tele_seq;
    if (*ctx.screen == SCR_DEV) { tele_head(usb_page[UP_LIMITS], 3, ms); dev_tele(usb_page[UP_LIMITS]); usb_page[UP_LIMITS][63] = tele_seq; }
    else tele_limits(ph, ms);
    tele_charge(ph, ms);
}

void tele_init(const tele_ctx_t XD *c)
{
    ctx = *c;
    tele_seq = 0; ev_seq = 0; ev_code = 0; ev_port = 0; cmd_seq = 0; cmd_res = 0;
}

void tele_event(uint8_t code, uint8_t port) { ev_code = code; ev_port = port; ev_seq++; }
void tele_cmd(uint8_t seq, uint8_t res) { cmd_seq = seq; cmd_res = res; }