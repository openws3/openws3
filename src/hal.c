/*
 * STC8H8K64U @ 40 MHz.  Init values for PWM/ADC/ports are those of a bench firmware that has run on this board.
 */
#include "hal.h"

__sfr __at (0x80) P0;   __sfr __at (0x90) P1;   __sfr __at (0xA0) P2;
__sfr __at (0xB0) P3;   __sfr __at (0xC0) P4;   __sfr __at (0xC8) P5;
__sfr __at (0x93) P0M1; __sfr __at (0x94) P0M0; __sfr __at (0x91) P1M1; __sfr __at (0x92) P1M0;
__sfr __at (0x95) P2M1; __sfr __at (0x96) P2M0; __sfr __at (0xB1) P3M1; __sfr __at (0xB2) P3M0;
__sfr __at (0xB3) P4M1; __sfr __at (0xB4) P4M0; __sfr __at (0xC9) P5M1; __sfr __at (0xCA) P5M0;
__sfr __at (0xBA) P_SW2;
__sfr __at (0x88) TCON; __sfr __at (0x89) TMOD; __sfr __at (0x8A) TL0; __sfr __at (0x8C) TH0;
__sfr __at (0xF4) USBCON;
__sfr __at (0x8E) AUXR; __sfr __at (0xA8) IE;
__sfr __at (0xC2) IAP_DATA; __sfr __at (0xC3) IAP_ADDRH; __sfr __at (0xC4) IAP_ADDRL;
__sfr __at (0xC5) IAP_CMD;  __sfr __at (0xC6) IAP_TRIG;  __sfr __at (0xC7) IAP_CONTR; __sfr __at (0xF5) IAP_TPS;
__sfr __at (0xBC) ADC_CONTR; __sfr __at (0xBD) ADC_RES; __sfr __at (0xBE) ADC_RESL; __sfr __at (0xDE) ADCCFG;

__sbit __at (0xC5) EN_P1;    /* P4.5 */
__sbit __at (0xA7) EN_P2;    /* P2.7 */
__sbit __at (0xA5) EN_P3;    /* P2.5 */
__sbit __at (0xC2) EN_P4;    /* P4.2 */
__sbit __at (0xC3) EN_P5;    /* P4.3 */
__sbit __at (0xC4) EN_P6;    /* P4.4 */
__sbit __at (0xCA) GLOBAL_EN;/* P5.2 */
__sbit __at (0xC7) POWER_EN; /* P4.7 */
__sbit __at (0xCB) PROFILE;  /* P5.3 */
__sfr __at (0x87) PCON;
__sbit __at (0xC6) TONE;     /* P4.6 */
__sbit __at (0x87) ALARM;    /* P0.7 */
__sbit __at (0xB2) BTN1;     /* P3.2 */
__sbit __at (0xB6) BTN2;     /* P3.6 */
__sbit __at (0xB4) STRAP;    /* P3.4 */

#define XB(a)  (*(volatile uint8_t __xdata *)(a))
#define EAXFR  0x80

static const __code uint16_t ccr_addr[NPORTS] = PORT_CCR_INIT;
static volatile uint32_t __xdata g_ticks;       /* 0.5 ms ticks since boot */

/* STARTUP RULE: no initialised XRAM variables anywhere.  SDCC's startup copies them with `movx @r0` and uses P2 as
 * the high address byte, finishing with P2 = 0xFF - which would drive every PWM pin and two port enables high.
 * A boot test that watches the enables catches any violation.
 *
 * Runs before the C runtime: drive every power-path pin low while they are still quasi-bidirectional
 * (a strong 0 even before the port mode registers are written).  */
unsigned char __sdcc_external_startup(void)
{
    P2 = 0x00;                 /* PWM pins + two enables low */
    P4 = 0x02;                 /* enables, beep, power low; LCD CS (P4.1) idle high */
    P5 = 0x03;                 /* P5.2/P5.3 low; LCD RST/DC idle high */
    P0 = 0x00;                 /* P0.7 low (P0.0-6 are analog inputs) */
    P3 &= ~0x08;               /* P3.3 rail actuator low */
    return 0;
}

static void pwm_init(void)
{
    P_SW2 |= EAXFR;
    XB(0xFECC) = 0; XB(0xFECD) = 0;
    XB(0xFEC8) = 0x78; XB(0xFEC9) = 0x78; XB(0xFECA) = 0x78; XB(0xFECB) = 0x78;
    XB(0xFECC) = 0x33; XB(0xFECD) = 0x33;
    XB(0xFEEC) = 0; XB(0xFEED) = 0;
    XB(0xFEE8) = 0x78; XB(0xFEE9) = 0x78; XB(0xFEEA) = 0x78; XB(0xFEEB) = 0x78;
    XB(0xFEEC) = 0x33; XB(0xFEED) = 0x33;
    XB(0xFED2) = PWM_PERIOD >> 8; XB(0xFED3) = PWM_PERIOD & 0xFF;      /* PWMA_ARR */
    XB(0xFEF2) = PWM_PERIOD >> 8; XB(0xFEF3) = PWM_PERIOD & 0xFF;      /* PWMB_ARR */
    XB(0xFED0) = 0; XB(0xFED1) = 0; XB(0xFEF0) = 0; XB(0xFEF1) = 0;    /* prescalers */
    XB(0xFEB1) = 0x54;                                                  /* PWMA_ENO: PWM2P/3P/4P */
    XB(0xFEB5) = 0x55;                                                  /* PWMB_ENO: PWM5..8 */
    XB(0xFEB2) = 0x54;                                                  /* PWMA_PS */
    XB(0xFEB6) = 0x10;                                                  /* PWMB_PS: PWM7 on P3.3 */
    XB(0xFEDD) = 0x80; XB(0xFEFD) = 0x80;                               /* BKR: main output enable */
    XB(0xFEC0) |= 0x01; XB(0xFEE0) |= 0x01;                             /* CR1: counters on */
}

void hal_init(void)
{
    /* pin modes */
    P0M1 = 0x7F; P0M0 = 0x80;  P1M1 = 0xFF; P1M0 = 0x00;
    P2M1 = 0x00; P2M0 = 0xFF;  P3M1 = 0xB3; P3M0 = 0xA8;
    P4M1 = 0x02; P4M0 = 0xFE;  P5M1 = 0x13; P5M0 = 0x0F;

    pwm_init();
    hal_all_off();

    XB(0xFEA8) = 0x3F;         /* ADC sample timing */
    ADCCFG = 0x2A;             /* right-aligned result, speed 0xA */
    ADC_CONTR = 0x80;          /* ADC power on */

    AUXR |= 0x80;              /* Timer0 1T */
    TMOD &= 0xF0;              /* mode 0: 16-bit auto-reload */
    TL0 = (65536UL - F_SYS / TICK_HZ) & 0xFF;
    TH0 = (65536UL - F_SYS / TICK_HZ) >> 8;
    TCON |= 0x10;              /* TR0 */
    IE |= 0x02;                /* ET0 */
}

void hal_irq_on(void) { IE |= 0x80; }

void hal_tick(void) { g_ticks++; }

uint32_t hal_ticks(void)
{
    uint32_t t;
    IE &= ~0x80;
    t = g_ticks;
    IE |= 0x80;
    return t;
}

uint32_t hal_ms(void) { return hal_ticks() >> 1; }       /* TICK_HZ = 2000 */

void hal_delay_ms(uint16_t ms)
{
    uint32_t t0 = hal_ms();
    while ((uint16_t)(hal_ms() - t0) < ms);
}

void hal_pwm(uint8_t port, uint16_t duty)
{
    uint16_t a;
    if (port >= NPORTS) return;
    if (duty > PWM_PERIOD) duty = PWM_PERIOD;
    a = ccr_addr[port];
    XB(a) = duty >> 8; XB(a + 1) = duty & 0xFF;
}

void hal_rail(uint16_t duty)
{
    if (duty > PWM_PERIOD) duty = PWM_PERIOD;
    XB(RAIL_CCR) = duty >> 8; XB(RAIL_CCR + 1) = duty & 0xFF;
}

void hal_sw(uint8_t port, uint8_t on)
{
    switch (port) {
    case 0: EN_P1 = on; break;
    case 1: EN_P2 = on; break;
    case 2: EN_P3 = on; break;
    case 3: EN_P4 = on; break;
    case 4: EN_P5 = on; break;
    case 5: EN_P6 = on; break;
    }
}

void hal_global(uint8_t on) { GLOBAL_EN = on; }
void hal_power(uint8_t on)  { POWER_EN = on; }
void hal_profile(uint8_t on){ PROFILE = on; }

void hal_power_down(void)
{
    hal_all_off();
    POWER_EN = 0; PROFILE = 0; ALARM = 0; TONE = 0;
    IE = 0;
    PCON |= 0x02;                      /* STOP mode: only a reset (power cycle) wakes it */
    for (;;);
}

void hal_all_off(void)
{
    uint8_t i;
    GLOBAL_EN = 0;
    for (i = 0; i < NPORTS; i++) { hal_sw(i, 0); hal_pwm(i, 0); }
    hal_rail(0);
}

uint16_t hal_adc(uint8_t ch)
{
    ADC_RES = 0; ADC_RESL = 0;
    ADC_CONTR = (ADC_CONTR & 0xE0) | (ch & 0x0F);
    ADC_CONTR |= 0x40;                 /* start */
    __asm
        nop
        nop
        nop
        nop
    __endasm;
    while (!(ADC_CONTR & 0x20));
    ADC_CONTR &= ~0x20;
    return ((uint16_t)ADC_RES << 8) | ADC_RESL;
}

void hal_reboot_isp(void)
{
    static volatile uint16_t XD d;
    hal_all_off();
    IE &= 0x7F;
    USBCON = 0;                        /* drop the D+ pull-up: the host sees a disconnect, the loader enumerates afresh */
    for (d = 0; d < 60000u; d++) __asm nop __endasm;
    IAP_CONTR = 0x60;                  /* SWBS=1, SWRST=1: reset and start in the ISP area */
    for (;;) ;
}

static void iap(uint8_t cmd, uint16_t addr, uint8_t v)
{
    IAP_CONTR = 0x80;                  /* enable, fastest wait */
    IAP_TPS = 0x28;
    IAP_CMD = cmd;
    IAP_ADDRH = addr >> 8;
    IAP_ADDRL = addr & 0xFF;
    IAP_DATA = v;
    IAP_TRIG = 0x5A;
    IAP_TRIG = 0xA5;
    __asm
        nop
    __endasm;
}

static void iap_idle(void)             /* leave the IAP interface idle */
{
    IAP_CONTR = 0; IAP_CMD = 0; IAP_TRIG = 0;
    IAP_ADDRH = 0x80; IAP_ADDRL = 0;
}

uint8_t hal_ee_read(uint16_t addr)
{
    uint8_t v;
    iap(1, addr, 0);
    v = IAP_DATA;
    iap_idle();
    return v;
}

void hal_ee_erase(uint16_t addr) { iap(3, addr, 0); iap_idle(); }
void hal_ee_write(uint16_t addr, uint8_t v) { iap(2, addr, v); iap_idle(); }

void hal_tone(uint8_t level)  { TONE = level; }
void hal_alarm(uint8_t level) { ALARM = level; }
uint8_t hal_btn(uint8_t n)    { return n == 1 ? !BTN1 : !BTN2; }
uint8_t hal_strap(void)       { return STRAP; }
