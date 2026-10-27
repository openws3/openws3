#include "input.h"

#define QN 4                      /* a power of two: the index wraps with a mask (a % would be a library call in the interrupt) */
static uint8_t  q[QN], qh, qt;
static uint16_t held[2];          /* ticks the button has been down */
static uint16_t next[2];          /* ticks until its next repeat */
static uint8_t  reps[2];          /* repeats sent for this press */
static uint16_t both;             /* ticks both buttons have been down together (chord) */
static uint8_t  chord, back_sent, style;

static void put(uint8_t ev)
{
    uint8_t n = (uint8_t)((qt + 1) & (QN - 1));
    if (n != qh) { q[qt] = ev; qt = n; }         /* drop when full */
}

void input_reset(void)
{
    uint8_t i;
    qh = qt = 0;
    for (i = 0; i < 2; i++) { held[i] = 0; reps[i] = 0; }
    both = 0; chord = back_sent = style = 0;
}

void input_style(uint8_t s) { style = s; }

static void one(uint8_t i, uint8_t now_down, uint8_t ev_tap, uint8_t ev_rep, uint8_t ev_long, uint16_t long_ticks)
{
    if (now_down) {
        if (held[i] < 0xFFFF) held[i]++;
        if (style == IS_MAIN) {
            if (!reps[i] && held[i] >= long_ticks) { put(ev_long); reps[i] = 1; }       /* once; reps only marks the press as used */
        } else if (reps[i] ? --next[i] == 0 : held[i] >= REP_DELAY) {
            put(ev_rep);
            if (reps[i] < 255) reps[i]++;
            next[i] = reps[i] < 5 ? REP_SLOW : reps[i] < 13 ? REP_MID : REP_FAST;
        }
    } else {
        if (held[i] >= DEBOUNCE && !reps[i]) put(ev_tap);
        held[i] = 0; reps[i] = 0;
    }
}

void input_tick(uint8_t b1_down, uint8_t b2_down)
{
    if (chord) {                                     /* both buttons own this press until both are released */
        if (b1_down && b2_down) {
            if (both < 0xFFFF) both++;
            if (!back_sent && both >= CHORD_LONG) { back_sent = 1; put(EV_BACK); }
        } else if (!b1_down && !b2_down) {
            if (!back_sent && both >= DEBOUNCE) put(EV_OK);
            chord = back_sent = 0; both = 0;
            held[0] = held[1] = 0;
        }
        return;
    }
    if (b1_down && b2_down && held[0] <= CHORD_WIN && held[1] <= CHORD_WIN && !reps[0] && !reps[1]) {   /* the second button followed in time */
        chord = 1; back_sent = 0; both = 1;
        return;
    }
    one(0, b1_down, EV_B1, EV_B1_REP, EV_B1_LONG, B1_LONG);
    one(1, b2_down, EV_B2, EV_B2_REP, EV_B2_LONG, B2_LONG);
}

uint8_t input_get(void)
{
    uint8_t e;
    if (qh == qt) return EV_NONE;
    e = q[qh];
    qh = (uint8_t)((qh + 1) & (QN - 1));
    return e;
}
