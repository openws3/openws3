/* User settings: charge current, target voltage, buzzer.  Stored in the same EEPROM cells the original firmware uses (so swapping firmwares keeps them). */
#ifndef CFG_H
#define CFG_H
#include "compat.h"
#include "sup.h"
#include "board.h"

#define CFG_ADDR_CURRENT 0x0200       /* low byte of the 16-bit setting; page erased, then written */
#define CFG_ADDR_VOLT    0x0400
#define CFG_ADDR_BUZZER  0x0600
#define CFG_ADDR_THEME   0x0A00       /* colour theme, theme.h TH_* */
/* our own settings page (the original firmware does not use it): magic, input current limit, regulation, minimum input voltage, cut/limit,
 * then the per-channel settings: a current for each port (0 = the global one) and a mask of the ports that sit the run out.
 * A block without the magic (erased flash) means: limits off, equal sharing, limit smoothly, every channel on the global settings. */
#define CFG_ADDR_LIMITS  0x0800
#define CFG_CHAN_MAGIC   0xC1        /* the per-channel part of the page: magic, a current per port, the mask */
#define CFG_CH_CUR       (CFG_ADDR_LIMITS + 6)
#define CFG_CH_OFF       (CFG_ADDR_LIMITS + 6 + NPORTS)
#define CFG_LIMITS_MAGIC 0xA6        /* A5 was the first layout (two sharing modes); old blocks are ignored */

/* pure: turn a raw stored byte into a valid setting (erased flash = 0xFF -> default) */
uint8_t cfg_valid_current(uint8_t raw);       /* 21..91, else 21 */
uint8_t cfg_valid_volt(uint8_t raw);          /* 0..3 (4.20 / 4.35 / 3.80 / 3.85 V), else 0 */
uint8_t cfg_valid_buzzer(uint8_t raw);        /* anything but 0 = on (erased 0xFF = on) */
uint8_t cfg_valid_ilim(uint8_t raw);          /* 0 (off) or 5..100 (0.1 A), else 0 */
uint8_t cfg_valid_vmin(uint8_t raw);          /* 0 (off) or 48..250 (0.1 V), else 0 */
uint8_t cfg_valid_lmode(uint8_t raw);         /* a limits.h LM_* regulation mode, else LM_EQUAL */
uint8_t cfg_valid_theme(uint8_t raw);         /* a theme.h TH_* theme, else TH_CLASSIC */
uint8_t cfg_valid_ch_cur(uint8_t raw);        /* 21..91, else 0 (same as the global setting) */
uint8_t cfg_valid_ch_off(uint8_t raw);        /* one bit per port, else (erased flash) none */

/* target only */
void    cfg_load(void);
uint8_t cfg_current(void);
uint8_t cfg_volt(void);
uint8_t cfg_buzzer(void);
void    cfg_set_current(uint8_t units);       /* persist only when it changed */
void    cfg_set_volt(uint8_t volt);               /* volt: index of the target voltage */
void    cfg_set_buzzer(uint8_t on);
uint8_t cfg_ilim(void);
uint8_t cfg_vmin(void);
uint8_t cfg_vcut(void);
uint8_t cfg_lmode(void);
uint8_t cfg_theme(void);
void    cfg_set_theme(uint8_t theme);
void    cfg_set_limits(uint8_t ilim, uint8_t vmin, uint8_t vcut, uint8_t lmode);   /* one page write, only when something changed */
uint8_t cfg_ch_cur(uint8_t port);             /* 0 = the global current */
uint8_t cfg_ch_off(void);                     /* bit k set: port k sits the run out */
void    cfg_set_channels(const uint8_t *cur, uint8_t off);                          /* same page, same rule */

#endif
