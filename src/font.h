/* 5x7 font, ASCII 0x20..0x5A (digits, upper case, a little punctuation). Cell = 6x8 with spacing. */
#ifndef FONT_H
#define FONT_H
#include "compat.h"
#define FONT_FIRST 0x20
#define FONT_LAST  0x5A
#define FONT_W     6
#define FONT_H_PX  8
#ifdef __SDCC
extern const __code uint8_t font5x7[];
#else
extern const uint8_t font5x7[];
#endif
#endif
