#include "cfg.h"
#include "hal.h"
#include "limits.h"
#include "board.h"

static uint8_t __xdata cur, volt, buz, ilim, vmin, vcut, lmode, theme, ch_cur[NPORTS], ch_off;

void cfg_load(void)
{
    uint8_t k;
    cur  = cfg_valid_current(hal_ee_read(CFG_ADDR_CURRENT));
    volt = cfg_valid_volt(hal_ee_read(CFG_ADDR_VOLT));
    buz  = cfg_valid_buzzer(hal_ee_read(CFG_ADDR_BUZZER));
    theme = cfg_valid_theme(hal_ee_read(CFG_ADDR_THEME));
    if (hal_ee_read(CFG_ADDR_LIMITS) == CFG_LIMITS_MAGIC) {
        ilim  = cfg_valid_ilim(hal_ee_read(CFG_ADDR_LIMITS + 1));
        lmode = cfg_valid_lmode(hal_ee_read(CFG_ADDR_LIMITS + 2));
        vmin  = cfg_valid_vmin(hal_ee_read(CFG_ADDR_LIMITS + 3));
        vcut  = hal_ee_read(CFG_ADDR_LIMITS + 4) == 1;
    } else { ilim = vmin = vcut = 0; lmode = LM_EQUAL; }
    ch_off = 0;
    for (k = 0; k < NPORTS; k++) ch_cur[k] = 0;
    if (hal_ee_read(CFG_ADDR_LIMITS) == CFG_LIMITS_MAGIC && hal_ee_read(CFG_ADDR_LIMITS + 5) == CFG_CHAN_MAGIC) {
        for (k = 0; k < NPORTS; k++) ch_cur[k] = cfg_valid_ch_cur(hal_ee_read(CFG_CH_CUR + k));
        ch_off = cfg_valid_ch_off(hal_ee_read(CFG_CH_OFF));
    }
}

uint8_t cfg_current(void) { return cur; }
uint8_t cfg_volt(void)    { return volt; }
uint8_t cfg_buzzer(void)  { return buz; }
uint8_t cfg_ilim(void)    { return ilim; }
uint8_t cfg_vmin(void)    { return vmin; }
uint8_t cfg_vcut(void)    { return vcut; }
uint8_t cfg_lmode(void)   { return lmode; }
uint8_t cfg_theme(void)   { return theme; }
uint8_t cfg_ch_cur(uint8_t port) { return ch_cur[port]; }
uint8_t cfg_ch_off(void)  { return ch_off; }

static void store(uint16_t addr, uint8_t v)
{
    if (hal_ee_read(addr) == v) return;           /* spare the flash */
    hal_ee_erase(addr);
    hal_delay_ms(6);                              /* wait ~6 ms after an erase */
    hal_ee_write(addr, v);
}

void cfg_set_current(uint8_t u) { cur = cfg_valid_current(u); store(CFG_ADDR_CURRENT, cur); }
void cfg_set_volt(uint8_t m)    { volt = cfg_valid_volt(m);   store(CFG_ADDR_VOLT, volt); }
void cfg_set_buzzer(uint8_t on) { buz = on ? 1 : 0;           store(CFG_ADDR_BUZZER, buz); }
void cfg_set_theme(uint8_t t)   { theme = cfg_valid_theme(t); store(CFG_ADDR_THEME, theme); }

static void store_page(void)
{
    uint8_t k;
    hal_ee_erase(CFG_ADDR_LIMITS);
    hal_delay_ms(6);
    hal_ee_write(CFG_ADDR_LIMITS, CFG_LIMITS_MAGIC);
    hal_ee_write(CFG_ADDR_LIMITS + 1, ilim); hal_ee_write(CFG_ADDR_LIMITS + 2, lmode);
    hal_ee_write(CFG_ADDR_LIMITS + 3, vmin); hal_ee_write(CFG_ADDR_LIMITS + 4, vcut);
    hal_ee_write(CFG_ADDR_LIMITS + 5, CFG_CHAN_MAGIC);
    for (k = 0; k < NPORTS; k++) hal_ee_write(CFG_CH_CUR + k, ch_cur[k]);
    hal_ee_write(CFG_CH_OFF, ch_off);
}

/* the page holds both groups of settings: a change in either rewrites all of it, and nothing is written when nothing changed */
static uint8_t page_same(void)
{
    uint8_t k, same = hal_ee_read(CFG_ADDR_LIMITS) == CFG_LIMITS_MAGIC && hal_ee_read(CFG_ADDR_LIMITS + 5) == CFG_CHAN_MAGIC
        && hal_ee_read(CFG_CH_OFF) == ch_off;
    for (k = 0; k < NPORTS; k++) same = same && hal_ee_read(CFG_CH_CUR + k) == ch_cur[k];
    return same && hal_ee_read(CFG_ADDR_LIMITS + 1) == ilim && hal_ee_read(CFG_ADDR_LIMITS + 2) == lmode
        && hal_ee_read(CFG_ADDR_LIMITS + 3) == vmin && hal_ee_read(CFG_ADDR_LIMITS + 4) == vcut;
}

void cfg_set_limits(uint8_t i, uint8_t v, uint8_t c, uint8_t m)
{
    ilim = cfg_valid_ilim(i); vmin = cfg_valid_vmin(v); vcut = c != 0; lmode = cfg_valid_lmode(m);
    if (!page_same()) store_page();
}

void cfg_set_channels(const uint8_t *c, uint8_t off)
{
    uint8_t k;
    for (k = 0; k < NPORTS; k++) ch_cur[k] = cfg_valid_ch_cur(c[k]);
    ch_off = cfg_valid_ch_off(off);
    if (!page_same()) store_page();
}
