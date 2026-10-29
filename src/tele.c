/*
 * USB telemetry pages 1-3 (layout in USB.md).  The interrupt copies usb_tx / usb_tx2 to the host; they are rebuilt here, tail byte
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
        tele_put16(usb_tx2, 12, lim_out.budget); tele_put16(usb_tx2, 14, lim_out.p_cur); tele_put16(usb_tx2, 16, lim_out.p_vol);
        tele_put16(usb_tx2, 18, lim_out.est_ma); tele_put16(usb_tx2, 20, lim_out.bcnt);
        usb_tx2[22] = lim_out.trip; usb_tx2[23] = (uint8_t)(*ctx.run && !*ctx.rail_hold);        /* the limiter ran this pass */
        for (k = 0; k < NPORTS; k++) {
            usb_tx2[24 + k] = (uint8_t)(ctx.out->held[k] | (ctx.out->up[k] != 0) << 2 | ((ctx.in->rise >> k) & 1) << 3 | (ctx.in->cut == k) << 4);
            usb_tx2[30 + k] = ctx.out->want[k];
            usb_tx2[36 + k] = portset_get(k);
        }
        tele_put16(usb_tx2, 42, sup_setpoint(settings.volt)); tele_put16(usb_tx2, 44, sup_target(settings.volt));    /* vu */
        usb_tx2[46] = *ctx.stack;
        usb_tx2[47] = FW_PRE;
        usb_tx2[51] = ctx.out->any_active;
        usb_tx2[52] = ctx.sup->stop; usb_tx2[54] = ctx.sup->rail_cut; usb_tx2[55] = ctx.sup->stop == STOP_RAIL; usb_tx2[56] = ctx.sup->fan;
    }
    tele_put16(usb_tx2, 57, (uint16_t)(FW_BUILD >> 16)); tele_put16(usb_tx2, 59, (uint16_t)FW_BUILD); usb_tx2[61] = FW_DIRTY;   /* the build id */
    usb_tx2[63] = tele_seq;
}

void tele_pass(uint8_t ph)
{
    uint8_t k;
    uint32_t ms;
    if (!usb_configured()) return;                   /* nobody reads it */
    ms = hal_ms();
    if (++tele_seq == 0) tele_seq = 1;
    tele_head(usb_tx, 1, ms);
    usb_tx[8] = (uint8_t)(*ctx.run | (settings.volt << 1) | ((*ctx.run && !*ctx.rail_hold) << 3) | (*ctx.rail_hold << 5) | (*ctx.strap << 6));
    usb_tx[9] = settings.cur; usb_tx[10] = (uint8_t)(FW_MAJOR << 4 | FW_MINOR); usb_tx[11] = *ctx.stop; usb_tx[12] = (uint8_t)(*ctx.alarm | (*ctx.screen << 4));
    usb_tx[13] = ev_seq; usb_tx[14] = ev_code; usb_tx[15] = ev_port; usb_tx[16] = cmd_seq; usb_tx[17] = cmd_res;
    usb_tx[25] = ph;
    if (ph >= PH_RUN && ph < PH_FATAL && *ctx.screen != SCR_DEV) {          /* developer mode: the ports are on page 3 */
        for (k = 0; k < NPORTS; k++) {
            usb_tx[18 + k] = ctx.out->state[k];
            usb_tx[24] |= (uint8_t)(ctx.out->sw[k] << k);
            tele_put16(usb_tx, 26 + 2 * k, ctx.out->duty[k]);
            tele_put16(usb_tx, 38 + 2 * k, ctx.out->v[k]);
            usb_tx[50 + k] = ctx.out->i[k] > 255 ? 255 : (uint8_t)ctx.out->i[k];
        }
        usb_tx[62] = (uint8_t)(*ctx.rail_duty >> 2);
    }
    tele_put16(usb_tx, 56, adc_raw(ADC_RAIL)); tele_put16(usb_tx, 58, adc_raw(ADC_VIN)); tele_put16(usb_tx, 60, adc_raw(ADC_BANDGAP));   /* counts */
    usb_tx[63] = tele_seq;
    if (*ctx.screen == SCR_DEV) { tele_head(usb_tx2, 3, ms); dev_tele(usb_tx2); usb_tx2[63] = tele_seq; }
    else tele_limits(ph, ms);
}

void tele_init(const tele_ctx_t XD *c)
{
    ctx = *c;
    tele_seq = 0; ev_seq = 0; ev_code = 0; ev_port = 0; cmd_seq = 0; cmd_res = 0;
}

void tele_event(uint8_t code, uint8_t port) { ev_code = code; ev_port = port; ev_seq++; }
void tele_cmd(uint8_t seq, uint8_t res) { cmd_seq = seq; cmd_res = res; }