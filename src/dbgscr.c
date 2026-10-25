#include "dbgscr.h"
#include "cal.h"
#include "rowbuf.h"

static const char *state_name(uint8_t s)
{
    switch (s) {
    case 0: return "IDLE";   case 1: return "PROBE"; case 2: return "CHARGE"; case 3: return "REST";
    case 4: return "PAUSE";  case 5: return "FULL";  case 6: return "BLEED";  default: return "FAULT";
    }
}

uint16_t dbg_vcc_mv(uint16_t bandgap)
{
    return bandgap ? (uint16_t)((1190UL * 4096UL + bandgap / 2) / bandgap) : 0;
}

static const char *mode_name(uint8_t m)
{
    switch (m & 3) { case 0: return "4.20V"; case 1: return "4.35V"; case 2: return "3.80V"; default: return "3.85V"; }
}

dbg_view_t XD dbg_view;
static char XD rowbuf[DBG_COLS + 1];

const char *dbg_text(uint8_t page, uint8_t row)
{
    uint8_t k;
    uint16_t vcc = dbg_vcc_mv(dbg_view.bandgap);
    rb_begin(rowbuf);

    if (row == 0) { rb_num(page, 0); rb_str("/"); rb_num(DBG_PAGES, 0); rb_ch(' '); }

    switch (page) {
    case 1:
        switch (row) {
        case 0: rb_str("POWER  B1 next B2 back"); break;
        case 1: rb_str("BANDGAP "); rb_num(dbg_view.bandgap, 4); rb_str(" counts"); break;
        case 2: rb_str("VCC "); rb_v3(vcc); rb_str("V  (1.19V ref)"); break;
        case 3: rb_str("VIN "); rb_num(dbg_view.vin, 4); rb_str("  "); rb_v3(cal_vin_mv(dbg_view.vin)); rb_str("V"); break;
        case 4: rb_str("RAIL "); rb_num(dbg_view.rail, 4); rb_str("  "); rb_v3(cal_port_mv(dbg_view.rail)); rb_str("V"); break;
        case 5: rb_str("RAIL PWM "); rb_num(dbg_view.rail_duty, 3); rb_str("/400"); break;
        case 6: rb_str("ADC0 (THERM) "); rb_num(dbg_view.adc[0], 4); break;
        case 7: rb_str("FAN P0.7  "); rb_str(dbg_view.fan ? "ON" : "off"); break;
        case 8: rb_str("STRAP P3.4 "); rb_num(dbg_view.strap, 0); rb_str("  DC "); rb_num(dbg_view.dc, 0); break;
        case 9: rb_str("UPTIME "); rb_num(dbg_view.ms / 1000, 0); rb_str(" s"); break;
        }
        break;
    case 2:
        switch (row) {
        case 0: rb_str("PORT V  raw  A     B"); break;
        case 7: rb_str("A = 4.5V/4096 per count"); break;
        case 8: rb_str("B = VCC/4096 per count"); break;
        case 9: rb_str("VCC "); rb_v3(vcc); rb_str("V"); break;
        default:
            k = (uint8_t)(row - 1);
            rb_str("P"); rb_num(k + 1, 0); rb_ch(' '); rb_num(dbg_view.v[k], 4); rb_ch(' ');
            rb_v3(cal_port_mv(dbg_view.v[k])); rb_ch(' '); rb_v3(((uint32_t)dbg_view.v[k] * vcc + 2048) / 4096);
        }
        break;
    case 3:
        switch (row) {
        case 0: rb_str("PORT I cnt  mA  duty"); break;
        case 7: rb_str("1 count = 14.65 mA"); break;
        case 8: rb_str("duty 0..400, sw=bleed path"); break;
        case 9: break;
        default:
            k = (uint8_t)(row - 1);
            rb_str("P"); rb_num(k + 1, 0); rb_ch(' '); rb_num(dbg_view.i[k], 3); rb_ch(' '); rb_num(cal_port_ma(dbg_view.i[k]), 4);
            rb_str(" D"); rb_num(dbg_view.duty[k], 3); rb_ch(' '); rb_str(state_name(dbg_view.state[k]));
            if (dbg_view.sw[k]) rb_str(" sw");
        }
        break;
    case 4:
        switch (row) {
        case 0: rb_str("STATUS"); break;
        case 1: rb_str("RUN "); rb_num(dbg_view.run, 0); rb_str("  MODE "); rb_num(dbg_view.mode, 0); rb_ch(' '); rb_str(mode_name(dbg_view.mode)); break;
        case 2: rb_str("SET "); rb_num(dbg_view.iset, 0); rb_str(" USER "); rb_num(dbg_view.iset_user, 0); rb_str("  "); rb_num(cal_set_to_ma(dbg_view.iset), 0); rb_str("mA"); break;
        case 3: rb_str("TGT "); rb_num(dbg_view.tgt, 0); rb_str("  RAIL SET "); rb_num(dbg_view.setpoint, 0); break;
        case 4: rb_str("STOP "); rb_num(dbg_view.stop, 0); rb_str(" ALARM "); rb_num(dbg_view.alarm, 0); rb_str(" HOLD "); rb_num(dbg_view.rail_hold, 0); break;
        case 5: rb_str("STRAP "); rb_num(dbg_view.strap, 0); rb_str("  DC "); rb_num(dbg_view.dc, 0); rb_str("  FAN "); rb_num(dbg_view.fan, 0); break;
        case 6: rb_str("PORT STATE "); for (k = 0; k < NPORTS; k++) rb_num(dbg_view.state[k], 0); break;
        case 7: rb_str("PORT SW    "); for (k = 0; k < NPORTS; k++) rb_num(dbg_view.sw[k], 0); break;
        case 8: rb_str("USB "); rb_str(dbg_view.usb ? "connected" : "no host"); break;
        case 9:
#if defined(CHARGE_ENABLED) && !CHARGE_ENABLED
            rb_str("BUILD LAB CHARGE=0");
#else
            rb_str("BUILD CHARGE=1");
#endif
            break;
        }
        break;
    case 6: {
        static const CODE char *const SHORT[4] = { "EQUAL", "CH1 FIRST", "LOW V FIRST", "HIGH V FIRST" };
        switch (row) {
        case 0: rb_str("LIMITS"); break;
        case 1: rb_str("EST INPUT "); rb_num(dbg_view.est_ma, 5); rb_str(" mA"); break;
        case 2: rb_str("MAX IN "); if (dbg_view.ilim) { rb_num(dbg_view.ilim / 10, 0); rb_ch('.'); rb_num(dbg_view.ilim % 10, 0); rb_str("A"); } else rb_str("OFF"); break;
        case 3: rb_str("MIN IN "); if (dbg_view.vmin) { rb_num(dbg_view.vmin / 10, 0); rb_ch('.'); rb_num(dbg_view.vmin % 10, 0); rb_str(dbg_view.vcut ? "V STOP" : "V LIMIT"); } else rb_str("OFF"); break;
        case 4: rb_str("REG "); rb_str(SHORT[dbg_view.lmode % 4]); break;
        case 5: rb_str("BUDGET "); if (dbg_view.budget >= 10000) rb_str("NONE"); else { rb_num(dbg_view.budget / 10, 0); rb_ch('.'); rb_num(dbg_view.budget % 10, 0); rb_str(" W"); } break;
        case 6: case 7:
            rb_str(row == 6 ? "CAP P1-3 " : "CAP P4-6 ");
            for (k = 0; k < 3; k++) {
                uint8_t c = dbg_view.cap[(row - 6) * 3 + k];
                if (!c) rb_str("-  "); else if (c == 1) rb_str("OFF "); else { rb_num(c, 0); rb_ch(' '); }
            }
            break;
        case 8: rb_str("TRIP "); rb_num(dbg_view.trip, 0); break;
        case 9: rb_str("CAP - NONE  OFF PARKED"); break;
        }
        break; }
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
