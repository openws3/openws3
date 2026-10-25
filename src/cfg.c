#include "cfg.h"
#include "hal.h"
#include "limits.h"

static uint8_t __xdata cur, mode, buz, ilim, vmin, vcut, lmode;

void cfg_load(void)
{
    cur  = cfg_valid_current(hal_ee_read(CFG_ADDR_CURRENT));
    mode = cfg_valid_mode(hal_ee_read(CFG_ADDR_MODE));
    buz  = cfg_valid_buzzer(hal_ee_read(CFG_ADDR_BUZZER));
    if (hal_ee_read(CFG_ADDR_LIMITS) == CFG_LIMITS_MAGIC) {
        ilim  = cfg_valid_ilim(hal_ee_read(CFG_ADDR_LIMITS + 1));
        lmode = cfg_valid_lmode(hal_ee_read(CFG_ADDR_LIMITS + 2));
        vmin  = cfg_valid_vmin(hal_ee_read(CFG_ADDR_LIMITS + 3));
        vcut  = hal_ee_read(CFG_ADDR_LIMITS + 4) == 1;
    } else { ilim = vmin = vcut = 0; lmode = LM_EQUAL; }
}

uint8_t cfg_current(void) { return cur; }
uint8_t cfg_mode(void)    { return mode; }
uint8_t cfg_buzzer(void)  { return buz; }
uint8_t cfg_ilim(void)    { return ilim; }
uint8_t cfg_vmin(void)    { return vmin; }
uint8_t cfg_vcut(void)    { return vcut; }
uint8_t cfg_lmode(void)   { return lmode; }

static void store(uint16_t addr, uint8_t v)
{
    if (hal_ee_read(addr) == v) return;           /* spare the flash */
    hal_ee_erase(addr);
    hal_delay_ms(6);                              /* wait ~6 ms after an erase */
    hal_ee_write(addr, v);
}

void cfg_set_current(uint8_t u) { cur = cfg_valid_current(u); store(CFG_ADDR_CURRENT, cur); }
void cfg_set_mode(uint8_t m)    { mode = cfg_valid_mode(m);   store(CFG_ADDR_MODE, mode); }
void cfg_set_buzzer(uint8_t on) { buz = on ? 1 : 0;           store(CFG_ADDR_BUZZER, buz); }

void cfg_set_limits(uint8_t i, uint8_t v, uint8_t c, uint8_t m)
{
    uint8_t same = hal_ee_read(CFG_ADDR_LIMITS) == CFG_LIMITS_MAGIC && ilim == i && vmin == v && vcut == (c != 0) && lmode == m;
    ilim = cfg_valid_ilim(i); vmin = cfg_valid_vmin(v); vcut = c != 0; lmode = cfg_valid_lmode(m);
    if (same) return;                              /* spare the flash */
    hal_ee_erase(CFG_ADDR_LIMITS);
    hal_delay_ms(6);
    hal_ee_write(CFG_ADDR_LIMITS, CFG_LIMITS_MAGIC);
    hal_ee_write(CFG_ADDR_LIMITS + 1, ilim); hal_ee_write(CFG_ADDR_LIMITS + 2, lmode);
    hal_ee_write(CFG_ADDR_LIMITS + 3, vmin); hal_ee_write(CFG_ADDR_LIMITS + 4, vcut);
}
