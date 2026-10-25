/* User settings: charge current, mode, buzzer.  Stored in the same EEPROM cells the original firmware uses (so swapping firmwares keeps them). */
#ifndef CFG_H
#define CFG_H
#include "compat.h"

#define CFG_ADDR_CURRENT 0x0200       /* low byte of the 16-bit setting; page erased, then written */
#define CFG_ADDR_MODE    0x0400
#define CFG_ADDR_BUZZER  0x0600
#define NMODES           4
/* our own settings page (the original firmware does not use it): magic, input current limit, regulation, minimum input voltage, cut/limit.
 * A block without the magic (erased flash) means: limits off, equal sharing, limit smoothly. */
#define CFG_ADDR_LIMITS  0x0800
#define CFG_LIMITS_MAGIC 0xA6        /* A5 was the first layout (two modes); old blocks are ignored */

/* pure: turn a raw stored byte into a valid setting (erased flash = 0xFF -> default) */
uint8_t cfg_valid_current(uint8_t raw);       /* 21..91, else 21 */
uint8_t cfg_valid_mode(uint8_t raw);          /* 0..3, else 0 */
uint8_t cfg_valid_buzzer(uint8_t raw);        /* anything but 0 = on (erased 0xFF = on) */
uint8_t cfg_valid_ilim(uint8_t raw);          /* 0 (off) or 5..100 (0.1 A), else 0 */
uint8_t cfg_valid_vmin(uint8_t raw);          /* 0 (off) or 48..250 (0.1 V), else 0 */
uint8_t cfg_valid_lmode(uint8_t raw);         /* a limits.h LM_* mode, else LM_EQUAL */

/* target only */
void    cfg_load(void);
uint8_t cfg_current(void);
uint8_t cfg_mode(void);
uint8_t cfg_buzzer(void);
void    cfg_set_current(uint8_t units);       /* persist only when it changed */
void    cfg_set_mode(uint8_t mode);
void    cfg_set_buzzer(uint8_t on);
uint8_t cfg_ilim(void);
uint8_t cfg_vmin(void);
uint8_t cfg_vcut(void);
uint8_t cfg_lmode(void);
void    cfg_set_limits(uint8_t ilim, uint8_t vmin, uint8_t vcut, uint8_t lmode);   /* one page write, only when something changed */

#endif
