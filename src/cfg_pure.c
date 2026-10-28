#include "cfg.h"
#include "cal.h"
#include "limits.h"
#include "theme.h"
#include "board.h"

uint8_t cfg_valid_current(uint8_t raw)
{
    return (raw >= CUR_MIN && raw <= CUR_MAX) ? raw : CUR_MIN;
}

uint8_t cfg_valid_volt(uint8_t raw) { return raw < NVOLTS ? raw : 0; }

uint8_t cfg_valid_buzzer(uint8_t raw) { return raw != 0; }
uint8_t cfg_valid_ilim(uint8_t raw) { return (raw >= ILIM_MIN && raw <= ILIM_MAX) ? raw : ILIM_OFF; }
uint8_t cfg_valid_vmin(uint8_t raw) { return (raw == VMIN_OFF || (raw >= VMIN_MIN && raw <= VMIN_MAX)) ? raw : VMIN_DEFAULT; }
uint8_t cfg_valid_lmode(uint8_t raw) { return raw < LM_NMODES ? raw : LM_BY_CH; }
uint8_t cfg_valid_theme(uint8_t raw) { return raw < TH_COUNT ? raw : TH_CLASSIC; }
uint8_t cfg_valid_ch_cur(uint8_t raw) { return (raw >= CUR_MIN && raw <= CUR_MAX) ? raw : 0; }
uint8_t cfg_valid_ch_off(uint8_t raw) { return raw < (1u << NPORTS) ? raw : 0; }

cfg_t XD settings;

void cfg_validate(void)
{
    uint8_t k;
    settings.cur = cfg_valid_current(settings.cur);
    settings.volt = cfg_valid_volt(settings.volt);
    settings.sound = cfg_valid_buzzer(settings.sound);
    settings.theme = cfg_valid_theme(settings.theme);
    settings.ilim = cfg_valid_ilim(settings.ilim);
    settings.vmin = cfg_valid_vmin(settings.vmin);
    settings.vcut = settings.vcut == 1;
    settings.lmode = cfg_valid_lmode(settings.lmode);
    for (k = 0; k < NPORTS; k++) settings.ch_cur[k] = cfg_valid_ch_cur(settings.ch_cur[k]);
    settings.ch_off = cfg_valid_ch_off(settings.ch_off);
}
