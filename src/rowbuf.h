/* A tiny writer for fixed-width text rows (debug screens, menu).  One row is built at a time into a caller-supplied buffer. */
#ifndef ROWBUF_H
#define ROWBUF_H
#include "compat.h"

#define ROW_COLS 26                              /* characters per row: 26 x 6 px fills the 160 px panel */

void    rb_begin(char XD *buf);                  /* start a row at buf (ROW_COLS + 1 bytes) */
void    rb_ch(char c);
void    rb_str(const char *s);
void    rb_num(uint32_t v, uint8_t width);       /* unsigned decimal, right-aligned in `width` columns (0: as is) */
void    rb_v3(uint32_t mv);                      /* millivolts as X.XXX */
void    rb_pad(uint8_t col);                     /* spaces up to column col */
void    rb_right(const char *s, uint8_t last);   /* s right-aligned so that it ends at column `last` (spaces before it) */
const char *rb_end(void);                        /* pad to ROW_COLS, NUL terminate, return the row */

#endif
