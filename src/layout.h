/* Screen layout (160x80 landscape).  Plain numeric literals on purpose: other tools parse this file. */
#ifndef LAYOUT_H
#define LAYOUT_H

/* three header boxes: set current | input voltage | target voltage */
#define UI_BOX_X0       1
#define UI_BOX_PITCH    53
#define UI_BOX_W        52
#define UI_HDR_Y        0
#define UI_HDR_H        20
#define UI_HDR_TX       8        /* text origin inside the box */
#define UI_HDR_TY       2

/* 3 x 2 port cells */
#define UI_CELL_Y0      22
#define UI_CELL_PITCH_Y 29
#define UI_CELL_H       28
#define UI_BADGE_X      2        /* port-number badge inside the cell */
#define UI_BADGE_W      9
#define UI_CELL_VX      14       /* voltage text, stretched 1x2 */
#define UI_CELL_VY      2
#define UI_CELL_IX      14       /* current text, 1x1 */
#define UI_CELL_IY      19
#define UI_FRAME        2        /* port settings mode: the frame round the selected cell */

/* RGB565 */
#define UI_C_BG         0x0000
#define UI_C_BOX        0x2104
#define UI_C_TEXT       0xFFFF
#define UI_C_VIN        0xFFE0
#define UI_C_VOLT       0x87F0
#define UI_C_DIM        0x7BEF
#define UI_C_BADGE      { 0xF800, 0x07E0, 0x041F, 0xFFE0, 0xF81F, 0x07FF }
#define UI_C_RUN        0xC000   /* header boxes while running */
#define UI_C_WARN       0xFD20
#define UI_C_GOOD       0x07E0
#define UI_C_BAD        0xF800
#define UI_C_INFO       0x07FF
#define UI_C_FULL       0x0400   /* the cell of a full port, and the text and dot on it */
#define UI_C_FULLTX     0xFFFF
/* per-port state dot, index = PS_* (idle probe charge rest full bleed fault) */
#define UI_C_DOT        { 0x2104, 0xFFE0, 0xF800, 0xFD20, 0xFFFF, 0x041F, 0xF81F }
#define UI_DOT_X        44
#define UI_DOT_Y        4
#define UI_DOT_S        6

#endif
