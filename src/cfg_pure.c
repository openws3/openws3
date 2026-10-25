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
uint8_t cfg_valid_vmin(uint8_t raw) { return (raw >= VMIN_MIN && raw <= VMIN_MAX) ? raw : VMIN_OFF; }
uint8_t cfg_valid_lmode(uint8_t raw) { return raw < LM_NMODES ? raw : LM_EQUAL; }
uint8_t cfg_valid_theme(uint8_t raw) { return raw < TH_COUNT ? raw : TH_CLASSIC; }
uint8_t cfg_valid_ch_cur(uint8_t raw) { return (raw >= CUR_MIN && raw <= CUR_MAX) ? raw : 0; }
uint8_t cfg_valid_ch_off(uint8_t raw) { return raw < (1u << NPORTS) ? raw : 0; }
