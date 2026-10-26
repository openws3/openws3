#include "rowbuf.h"

static char *XD wb;                              /* external RAM: direct RAM is full */
static uint8_t XD wn;

void rb_begin(char XD *buf) { wb = buf; wn = 0; }
void rb_ch(char c) { if (wn < ROW_COLS) wb[wn++] = c; }
void rb_str(const char *s) { while (*s) rb_ch(*s++); }

void rb_num(uint32_t v, uint8_t width)
{
    static char XD t[10];
    static uint8_t XD k;
    k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 10);
    while (width > k) { rb_ch(' '); width--; }
    while (k) rb_ch(t[--k]);
}

void rb_v3(uint32_t mv)
{
    rb_num(mv / 1000, 0); rb_ch('.');
    rb_ch((char)('0' + (mv / 100) % 10)); rb_ch((char)('0' + (mv / 10) % 10)); rb_ch((char)('0' + mv % 10));
}

void rb_pad(uint8_t col) { while (wn < col && wn < ROW_COLS) rb_ch(' '); }

void rb_right(const char *s, uint8_t last)
{
    static uint8_t XD n;
    n = 0;
    while (s[n]) n++;
    while ((uint8_t)(wn + n) < (uint8_t)(last + 1)) rb_ch(' ');
    rb_str(s);
}

const char *rb_end(void)
{
    rb_pad(ROW_COLS);
    wb[ROW_COLS] = 0;
    return wb;
}
