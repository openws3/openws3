#include "cfg.h"
#include "hal.h"

#define BYTES ((uint8_t XD *)&settings)

void cfg_load(void)
{
    uint8_t k, ok = hal_ee_read(CFG_ADDR) == CFG_LAYOUT;
    for (k = 0; k < sizeof settings; k++) BYTES[k] = ok ? hal_ee_read(CFG_ADDR + 1 + k) : 0xFF;
    cfg_validate();
}

void cfg_save(void)
{
    uint8_t k, same = hal_ee_read(CFG_ADDR) == CFG_LAYOUT;
    cfg_validate();
    for (k = 0; k < sizeof settings && same; k++) same = hal_ee_read(CFG_ADDR + 1 + k) == BYTES[k];
    if (same) return;                             /* spare the flash */
    hal_ee_erase(CFG_ADDR);
    hal_delay_ms(6);                              /* wait ~6 ms after an erase */
    for (k = 0; k < sizeof settings; k++) hal_ee_write(CFG_ADDR + 1 + k, BYTES[k]);
    hal_ee_write(CFG_ADDR, CFG_LAYOUT);           /* last: a write cut short leaves no valid page */
}
