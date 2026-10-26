#include "fmt.h"

/* NB: int is 16 bits on the 8051 but 32 on the host - every sum that can exceed 65535 is done in uint32_t. */

static uint8_t put(char *buf, uint8_t n, char c) { buf[n] = c; return n + 1; }

/* int_part '.' frac_digits unit, from a value already scaled to `scale` fractional digits (1 or 2) */
static uint8_t num(char *buf, uint16_t v, uint8_t scale, uint16_t max, char unit)
{
    uint8_t n = 0;
    uint16_t div = scale == 2 ? 100 : 10;
    uint16_t ip, fp;
    if (v > max) v = max;
    ip = v / div; fp = v % div;
    if (ip >= 10) n = put(buf, n, (char)('0' + ip / 10));
    n = put(buf, n, (char)('0' + ip % 10));
    n = put(buf, n, '.');
    if (scale == 2) n = put(buf, n, (char)('0' + fp / 10));
    n = put(buf, n, (char)('0' + fp % 10));
    n = put(buf, n, unit);
    buf[n] = 0;
    return n;
}

uint8_t fmt_v2(char *buf, uint16_t mv) { return num(buf, (uint16_t)(((uint32_t)mv + 5u) / 10u), 2, 999, 'V'); }
uint8_t fmt_v1(char *buf, uint16_t mv) { return num(buf, (uint16_t)(((uint32_t)mv + 50u) / 100u), 1, 999, 'V'); }
uint8_t fmt_a1(char *buf, uint16_t ma) { return num(buf, (uint16_t)(((uint32_t)ma + 50u) / 100u), 1, 999, 'A'); }
uint8_t fmt_a2(char *buf, uint16_t ma) { return num(buf, (uint16_t)(((uint32_t)ma + 5u) / 10u), 2, 999, 'A'); }

uint8_t fmt_u(char *buf, uint16_t v)
{
    char t[6];
    uint8_t n = 0, k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 5);
    while (k) buf[n++] = t[--k];
    buf[n] = 0;
    return n;
}
