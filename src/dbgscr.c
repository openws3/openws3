#include "dbgscr.h"
#include "cal.h"
#include "rowbuf.h"
#include "limits.h"
#include "sup.h"
#include "ports.h"

static const char *state_name(uint8_t s)
{
    switch (s) {
    case 0: return "IDLE";   case 1: return "PROBE"; case 2: return "CHARGE"; case 3: return "REST";
    case 4: return "FULL";   case 5: return "BLEED"; default: return "FAULT";
    }
}

uint16_t dbg_vcc_mv(uint16_t bandgap)
{
    return bandgap ? (uint16_t)((1190UL * 4096UL + bandgap / 2) / bandgap) : 0;
}

dbg_view_t XD dbg_view;
static char XD rowbuf[DBG_COLS + 1];

const char *dbg_text(uint8_t page, uint8_t row)
{
    uint8_t k;
    rb_begin(rowbuf);

    if (row == 0) { rb_num(page, 0); rb_str("/"); rb_num(DBG_PAGES, 0); rb_ch(' '); }

    switch (page) {
    case 1:
        switch (row) {
        case 0: rb_str("POWER  B2 next B1 back"); break;
        case 1: rb_str("BANDGAP "); rb_num(dbg_view.bandgap, 4); rb_str(" counts"); break;
        case 2: rb_str("VCC "); rb_v3(dbg_vcc_mv(dbg_view.bandgap)); rb_str("V  (1.19V ref)"); break;
        case 3: rb_str("VIN "); rb_num(dbg_view.adc[ADC_VIN], 4); rb_str("  "); rb_v3(cal_vin_mv(dbg_view.vin)); rb_str("V"); break;
        case 4: rb_str("RAIL "); rb_num(dbg_view.adc[ADC_RAIL], 4); rb_str("  "); rb_v3(cal_port_mv(dbg_view.rail)); rb_str("V"); break;
        case 5: rb_str("RAIL PWM "); rb_num(dbg_view.rail_duty, 3); rb_str("/400"); break;
        case 6: rb_str("ADC0 (THERM) "); rb_num(dbg_view.adc[0], 4); break;
        case 7: rb_str("FAN P0.7  "); rb_str(dbg_view.fan ? "ON" : "off"); break;
        case 8: rb_str("STRAP "); rb_num(dbg_view.strap, 0); rb_str("  STACK FREE "); rb_num(dbg_view.stack, 0); break;
        case 9: rb_str("UPTIME "); rb_num(dbg_view.ms / 1000, 0); rb_str(" s"); break;
        }
        break;
    case 2:
        switch (row) {
        case 0: rb_str("PORT V  raw  volts"); break;
        case 7: rb_str("raw: ADC counts"); break;
        case 8: rb_str("volts: corrected for VCC"); break;
        case 9: break;
        default:
            k = (uint8_t)(row - 1);
            rb_str("P"); rb_num(k + 1, 0); rb_ch(' '); rb_num(dbg_view.adc[port_v_ch(k)], 4); rb_ch(' '); rb_v3(cal_port_mv(dbg_view.v[k]));
        }
        break;
    case 3:
        switch (row) {
        case 0: rb_str("PORT I raw  mA  duty"); break;
        case 7: rb_str("raw: counts, mA corrected"); break;
        case 8: rb_str("duty 0..400, sw=bleed path"); break;
        case 9: break;
        default:
            k = (uint8_t)(row - 1);
            rb_str("P"); rb_num(k + 1, 0); rb_ch(' '); rb_num(dbg_view.adc[port_i_ch(k)], 3); rb_ch(' '); rb_num(cal_port_ma(dbg_view.i[k]), 4);
            rb_str(" D"); rb_num(dbg_view.duty[k], 3); rb_ch(' '); rb_str(state_name(dbg_view.state[k]));
            if (dbg_view.sw[k]) rb_str(" sw");
        }
        break;
    case 4:
        switch (row) {
        case 0: rb_str("STATUS"); break;
        case 1: rb_str("RUN "); rb_num(dbg_view.run, 0); rb_str("  VOLT "); rb_num(dbg_view.volt, 0); rb_ch(' '); rb_str(sup_volt_name(dbg_view.volt)); break;
        case 2: rb_str("SET "); rb_num(dbg_view.iset, 0); rb_str("  "); rb_num(cal_set_to_ma(dbg_view.iset), 0); rb_str("mA"); break;
        case 3: rb_str("TGT "); rb_num(dbg_view.tgt, 0); rb_str("  RAIL SET "); rb_num(dbg_view.setpoint, 0); break;
        case 4: rb_str("STOP "); rb_num(dbg_view.stop, 0); rb_str(" ALARM "); rb_num(dbg_view.alarm, 0); rb_str(" HOLD "); rb_num(dbg_view.rail_hold, 0); break;
        case 5: rb_str("STRAP "); rb_num(dbg_view.strap, 0); rb_str("  FAN "); rb_num(dbg_view.fan, 0); break;
        case 6: rb_str("PORT STATE "); for (k = 0; k < NPORTS; k++) rb_num(dbg_view.state[k], 0); break;
        case 7: rb_str("PORT SW    "); for (k = 0; k < NPORTS; k++) rb_num(dbg_view.sw[k], 0); break;
        case 8: rb_str("USB "); rb_str(dbg_view.usb ? "connected" : "no host"); break;
        }
        break;
    case 6:
        switch (row) {
        case 0: rb_str("LIMITS"); break;
        case 1: rb_str("EST INPUT "); rb_num(dbg_view.est_ma, 5); rb_str(" mA"); break;
        case 2: rb_str("MAX IN "); if (dbg_view.ilim) { rb_num(dbg_view.ilim / 10, 0); rb_ch('.'); rb_num(dbg_view.ilim % 10, 0); rb_str("A"); } else rb_str("OFF"); break;
        case 3: rb_str("MIN IN "); if (dbg_view.vmin) { rb_v3(VMIN_MV(dbg_view.vmin)); rb_str(dbg_view.vcut ? "V STOP" : "V LIMIT"); } else rb_str("OFF"); break;
        case 4: rb_str("REG "); rb_str(lim_mode_name(dbg_view.lmode)); break;
        case 5: rb_str("BUDGET "); if (dbg_view.budget >= LIM_P_BIG) rb_str("NONE"); else { rb_num(dbg_view.budget / 10, 0); rb_ch('.'); rb_num(dbg_view.budget % 10, 0); rb_str(" W"); } break;
        case 6: rb_str("PORTS MAX "); if (dbg_view.bcnt) { rb_num(cal_port_ma(dbg_view.bcnt), 0); rb_str(" mA"); } else rb_str("NONE"); break;
        case 7:
            rb_str("HELD ");
            for (k = 0; k < NPORTS; k++) { rb_ch(dbg_view.held[k] == 2 ? 'W' : dbg_view.held[k] ? 'H' : '-'); rb_ch(' '); }
            break;
        case 8: rb_str("TRIP "); rb_num(dbg_view.stop == STOP_VLIM, 0); break;
        case 9: rb_str("H HELD  W WAITS"); break;
        }
        break;
    default:                                                      /* page 5 */
        if (row == 0) { rb_str("ADC RAW 0..15"); break; }
        if (row <= 6) {
            uint8_t c, first = (uint8_t)((row - 1) * 3);
            for (c = 0; c < 3; c++) {
                k = (uint8_t)(first + c);
                if (k >= NADC) break;
                rb_str("A"); rb_num(k, 0); rb_ch(':'); rb_num(dbg_view.adc[k], 4); rb_ch(' ');
            }
        } else if (row == 7) { rb_str("A15 = bandgap, A8 = rail"); }
        else if (row == 8) { rb_str("A7 = VIN, A0 = thermistor"); }
        break;
    }
    return rb_end();
}
