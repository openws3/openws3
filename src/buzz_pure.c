#include "buzz.h"

/* Patterns as {on, off} pairs in ticks (0.5 ms), 0 terminates. */
typedef struct { uint16_t on, off; } seg_t;

static const CODE seg_t p_boot[]  = { {167, 167}, {167, 0}, {0, 0} };                         /* two 84 ms bursts, 167 ms apart */
static const CODE seg_t p_key[]   = { {33, 0}, {0, 0} };                                          /* 17 ms beep */
static const CODE seg_t p_done[]  = { {133, 166}, {133, 0}, {0, 0} };                         /* double beep when a port finishes: 2 x 67 ms */
static const CODE seg_t p_error[] = { {277, 111}, {277, 0}, {0, 0} };                         /* 2 x 139 ms */
static const CODE seg_t p_rail[]  = { {125, 125}, {125, 125}, {125, 360}, {0, 0} };           /* triple blink (62 ms), then repeat */

static const CODE seg_t *pat;
static uint8_t  repeat, seg, in_off, enabled, tone_lvl;
static uint16_t left;

void buzz_reset(void) { pat = 0; repeat = 0; seg = 0; in_off = 0; left = 0; tone_lvl = 0; enabled = 1; }
void buzz_enable(uint8_t on) { enabled = on ? 1 : 0; }
uint8_t buzz_busy(void) { return pat != 0; }

void buzz_play(uint8_t id)
{
    switch (id) {
    case PAT_BOOT:  pat = p_boot;  repeat = 0; break;
    case PAT_KEY:   pat = p_key;   repeat = 0; break;
    case PAT_DONE:  pat = p_done;  repeat = 0; break;
    case PAT_ERROR: pat = p_error; repeat = 0; break;
    case PAT_RAIL_ALARM: pat = p_rail; repeat = 1; break;
    default: pat = 0; return;
    }
    seg = 0; in_off = 0; left = pat[0].on;
}

uint8_t buzz_tick(void)
{
    if (!pat) { tone_lvl = 0; return 0; }
    if (!in_off) {
        if (left) left--;
        if (!left) {
            if (pat[seg].off) { in_off = 1; left = pat[seg].off; }
            else { seg++; goto next; }
        }
    } else {
        if (left) left--;
        if (!left) {
            seg++;
        next:
            if (!pat[seg].on) {
                if (repeat) seg = 0; else { pat = 0; tone_lvl = 0; return 0; }
            }
            in_off = 0; left = pat[seg].on;
        }
    }
    if (!in_off && enabled) {
        tone_lvl ^= 1;                                          /* toggle every tick: 1 kHz */
    } else {
        tone_lvl = 0;
    }
    return tone_lvl;
}
