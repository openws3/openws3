/*
 * Debug screens (opened from the menu's DIAGNOSTICS).  Pure text generation: the app fills a snapshot, this module
 * returns the text of one 26-character row of one page.  The display code (ui.c) only draws rows.
 *
 * Pages: 1 power (bandgap, VCC, input, rail, thermistor)   2 port voltages in three scales   3 port currents and drive
 *        4 status and settings                              5 every ADC channel, raw      6 user limits
 */
#ifndef DBGSCR_H
#define DBGSCR_H
#include "compat.h"
#include "board.h"
#include "rowbuf.h"

#define DBG_PAGES  6
#define DBG_ROWS   10
#define DBG_COLS   ROW_COLS

typedef struct {
    uint32_t ms;
    uint16_t adc[NADC];                    /* adc_raw(0..15): counts */
    uint16_t v[NPORTS], i[NPORTS];         /* filtered port voltage (vu) / current (iu) readings */
    uint16_t duty[NPORTS];
    uint8_t  state[NPORTS], sw[NPORTS];
    uint16_t rail, rail_duty, vin, bandgap;  /* rail, vin: filtered readings; bandgap: counts */
    uint16_t tgt, setpoint;
    uint8_t  volt, iset, run, stop, alarm, strap, rail_hold, fan, usb;
    uint8_t  ilim, vmin, vcut, lmode, cap[NPORTS];            /* user limits (limits.h) */
    uint16_t est_ma, budget;
} dbg_view_t;

/* VCC in mV from the bandgap count: the internal reference is nominally 1.19 V and the ADC is referenced to VCC */
uint16_t dbg_vcc_mv(uint16_t bandgap);

/* the snapshot the app fills before asking for rows (a global on purpose: direct RAM is full on this chip, pointers and
 * extra parameters cost bytes there) */
extern dbg_view_t XD dbg_view;

/* text of `row` (0..DBG_ROWS-1) of `page` (1..DBG_PAGES), built from dbg_view, padded with spaces to DBG_COLS and
 * NUL terminated.  Points into a static buffer that the next call overwrites. */
const char *dbg_text(uint8_t page, uint8_t row);

#endif
