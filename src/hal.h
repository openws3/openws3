/*
 * Hardware layer — the ONLY code that touches SFRs/XFRs.  Port numbers are 0..5 = physical ports 1..6.
 * Everything defaults to the safe state: all enables low, all PWM 0, rail actuator 0.
 */
#ifndef HAL_H
#define HAL_H
#include "compat.h"
#include "board.h"

void     hal_init(void);                    /* port modes, PWM, ADC, 0.5 ms tick; interrupts stay off */
void     hal_irq_on(void);

void     hal_pwm(uint8_t port, uint16_t duty);   /* per-port current control, 0..PWM_PERIOD */
void     hal_rail(uint16_t duty);                /* rail actuator (P3.3), 0..PWM_PERIOD */
void     hal_sw(uint8_t port, uint8_t on);       /* per-port switch pin: 0 = charge path, 1 = bleed path (see board.h) */
void     hal_global(uint8_t on);                 /* P5.2 */
void     hal_power(uint8_t on);                  /* P4.7 */
void     hal_profile(uint8_t on);                /* P5.3 */
void     hal_reboot_isp(void);                   /* outputs off, then a software reset into the USB ISP loader; does not return */
void     hal_all_off(void);                      /* every enable low, every duty 0, P5.2 low */

uint16_t hal_adc(uint8_t ch);                    /* one 12-bit conversion */

/* Settings flash (IAP): one page, see cfg.h. */
uint8_t  hal_ee_read(uint16_t addr);
void     hal_ee_erase(uint16_t addr);            /* erases the 512-byte sector containing addr */
void     hal_ee_write(uint16_t addr, uint8_t v); /* program one byte (sector must have been erased) */
uint32_t hal_ms(void);                           /* ms since boot, atomic read */
uint32_t hal_ticks(void);                        /* 0.5 ms ticks since boot, atomic read */
void     hal_delay_ms(uint16_t ms);              /* busy wait on the tick; interrupts must be on */


void     hal_tick(void);                         /* called from the 0.5 ms interrupt (defined in main.c: the vector must live with main()) */
void     hal_tone(uint8_t level);                /* P4.6 */
void     hal_fan(uint8_t on);                    /* P0.7 */
uint8_t  hal_btn(uint8_t n);                     /* 1 = button 1 (P3.2) pressed, 2 = button 2 (P3.6); active low pins */
uint8_t  hal_strap(void);                        /* P3.4 */

#endif
