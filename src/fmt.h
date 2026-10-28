/* Number formatting for the display. Pure; writes a NUL-terminated string, returns its length. */
#ifndef FMT_H
#define FMT_H
#include "compat.h"
uint8_t fmt_v2(char *buf, uint16_t mv);   /* 4197 -> "4.20V"   (clamps at 9.99V)  */
uint8_t fmt_vin(char *buf, uint16_t mv);  /* 19700 -> "19.70V" (clamps at 99.99V) */
uint8_t fmt_v1(char *buf, uint16_t mv);   /* 19700 -> "19.7V"  (clamps at 99.9V)  */
uint8_t fmt_a1(char *buf, uint16_t ma);   /* 300 -> "0.3A"     (clamps at 99.9A)  */
uint8_t fmt_a2(char *buf, uint16_t ma);   /* 312 -> "0.31A"    (clamps at 9.99A)  */
uint8_t fmt_u(char *buf, uint16_t v);     /* plain unsigned decimal, up to 5 digits */
#endif
