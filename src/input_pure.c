#include "input.h"

#define QN 4
static uint8_t  q[QN], qh, qt;
static uint16_t held[2];          /* ticks the button has been down */
static uint8_t  fired[2];         /* long event already sent for this press */
static uint8_t  down[2];
static uint8_t  chord, chord_sent;

static void put(uint8_t ev)
{
    uint8_t n = (uint8_t)((qt + 1) % QN);
    if (n != qh) { q[qt] = ev; qt = n; }         /* drop when full */
}

void input_reset(void)
{
    uint8_t i;
    qh = qt = 0;
    for (i = 0; i < 2; i++) { held[i] = 0; fired[i] = 0; down[i] = 0; }
    chord = chord_sent = 0;
}

static void one(uint8_t i, uint8_t now_down, uint16_t short_min, uint16_t long_ticks, uint8_t ev_short, uint8_t ev_long)
{
    if (now_down) {
        if (held[i] < 0xFFFF) held[i]++;
        down[i] = 1;
        if (!fired[i] && held[i] >= long_ticks) { fired[i] = 1; put(ev_long); }
    } else {
        if (down[i] && !fired[i] && held[i] >= short_min) put(ev_short);
        held[i] = 0; fired[i] = 0; down[i] = 0;
    }
}

void input_tick(uint8_t b1_down, uint8_t b2_down)
{
    if (chord) {                                     /* both buttons own this press until both are released */
        if (b1_down || b2_down) {
            if (held[0] < 0xFFFF && b1_down) held[0]++;
            if (held[1] < 0xFFFF && b2_down) held[1]++;
            if (!chord_sent && b1_down && b2_down && held[0] >= CHORD_TICKS && held[1] >= CHORD_TICKS) { chord_sent = 1; put(EV_CHORD); }
            return;
        }
        chord = chord_sent = 0;
        held[0] = held[1] = 0; fired[0] = fired[1] = 0; down[0] = down[1] = 0;
        return;
    }
    if (b1_down && b2_down && !fired[0] && !fired[1]) {   /* the second button arrived before any long press: a chord */
        chord = 1; chord_sent = 0;
        if (held[0] < 0xFFFF) held[0]++;
        if (held[1] < 0xFFFF) held[1]++;
        return;
    }
    one(0, b1_down, B1_SHORT_MIN, B1_LONG, EV_B1_SHORT, EV_B1_LONG);
    one(1, b2_down, B2_SHORT_MIN, B2_LONG, EV_B2_SHORT, EV_B2_LONG);
}

uint8_t input_get(void)
{
    uint8_t e;
    if (qh == qt) return EV_NONE;
    e = q[qh];
    qh = (uint8_t)((qh + 1) % QN);
    return e;
}

uint8_t input_held(uint8_t btn) { return btn >= 1 && btn <= 2 && down[btn - 1]; }
