/* User settings, kept in one EEPROM page: a layout byte, then the fields of cfg_t in order.  A page without the layout byte
 * (erased flash, or an older layout) gives the defaults.  The app works on `settings` directly and calls cfg_save(). */
#ifndef CFG_H
#define CFG_H
#include "compat.h"
#include "sup.h"
#include "board.h"

#define CFG_ADDR     0x0800
#define CFG_LAYOUT   0xB2             /* change it whenever cfg_t changes: an old page then loads the defaults */

typedef struct {
    uint8_t cur;                      /* charge current, counts: 21..91 (0.3..1.3 A) */
    uint8_t volt;                     /* target voltage, sup.h VOLT_* */
    uint8_t sound;                    /* 1 = beeps on */
    uint8_t theme;                    /* theme.h TH_* */
    uint8_t ilim, vmin, vcut, lmode;  /* input limits, limits.h lim_cfg_t */
    uint8_t ch_cur[NPORTS];           /* a current per port, 0 = the global one */
    uint8_t ch_off;                   /* bit k set: port k sits the run out */
} cfg_t;

extern cfg_t XD settings;

/* pure: turn a raw stored byte into a valid setting (erased flash = 0xFF -> default) */
uint8_t cfg_valid_current(uint8_t raw);       /* 21..91, else 21 */
uint8_t cfg_valid_volt(uint8_t raw);          /* 0..3 (4.20 / 4.35 / 3.80 / 3.85 V), else 0 */
uint8_t cfg_valid_buzzer(uint8_t raw);        /* anything but 0 = on (erased 0xFF = on) */
uint8_t cfg_valid_ilim(uint8_t raw);          /* 0 (off) or 5..100 (0.1 A), else 0 */
uint8_t cfg_valid_vmin(uint8_t raw);          /* 0 (off) or 96..250 (0.05 V), else 0 */
uint8_t cfg_valid_lmode(uint8_t raw);         /* a limits.h LM_* regulation mode, else LM_EQUAL */
uint8_t cfg_valid_theme(uint8_t raw);         /* a theme.h TH_* theme, else TH_CLASSIC */
uint8_t cfg_valid_ch_cur(uint8_t raw);        /* 21..91, else 0 (same as the global setting) */
uint8_t cfg_valid_ch_off(uint8_t raw);        /* one bit per port, else (erased flash) none */
void    cfg_validate(void);                   /* every field of `settings` through its rule */

/* target only */
void    cfg_load(void);
void    cfg_save(void);                       /* writes the page only when something changed */

#endif
