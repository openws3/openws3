/*
 * WhoopStor V3 board — single source of truth for pins, channels and constants.
 *
 * Port index 0..5 = PHYSICAL ports 1..6 (the original firmware numbers its channels from 0 with an offset).
 * Keep the table macros on one line each (other tools parse them with a regex).
 */
#ifndef BOARD_H
#define BOARD_H

#define F_SYS        40000000UL   /* IRC trimmed to ~40 MHz on this board (ISP handshake 39.97 MHz) */
#define TICK_HZ      2000UL       /* Timer0 tick: 20000 clocks = 0.5 ms */
#define NPORTS       6
#define PWM_PERIOD   400            /* PWMA/PWMB ARR, prescaler 0: 100 kHz at 40 MHz */

/* ADC channels (internal mux).  Per-port channels are in the tables below.
 * ADC1..6 sense port VOLTAGE and ADC 12,14,13,9,10,11 sense port CURRENT. */
#define ADC_IIN      0              /* P1.0  (name is historical) most likely a board thermistor (the debug screen shows it): the fan is switched on below 1300 at the storage voltages and off above 1451 */
#define ADC_VIN      7              /* P1.7  input voltage (divider ratio unverified) */
#define ADC_RAIL     8              /* P0.0  common rail, regulated to the setpoint of the target voltage by PWM7 */
#define ADC_BANDGAP  15             /* internal 1.19 V reference -> VCC */
#define NADC         16

/* index 0..5 = physical port 1..6 */
#define PORT_ADC_V_INIT   { 1, 2, 3, 4, 5, 6 }              /* voltage: 1 count = 4.5 V / 4096 = 1.0986 mV */
#define PORT_ADC_I_INIT   { 12, 14, 13, 9, 10, 11 }         /* current: 1 count = 1/68.25 A = 14.65 mA */
/* per-port current-control PWM compare register (high byte address; low byte = +1) */
#define PORT_CCR_INIT     { 0xFEFB, 0xFED7, 0xFED9, 0xFEDB, 0xFEF5, 0xFEF7 }
/* per-port switch pin as { port 0..5, bit }: P4.5 P2.7 P2.5 P4.2 P4.3 P4.4.
 * Polarity (inferred from closed-loop behaviour, not measured directly): LOW = charge path conducting, current set by
 * the PWM duty; HIGH = the discharge path used at the storage voltages.  So "enable" is a misnomer. */
#define PORT_EN_INIT      { {4,5}, {2,7}, {2,5}, {4,2}, {4,3}, {4,4} }
#define RAIL_CCR          0xFEF9    /* PWMB_CCR3 -> P3.3 rail actuator (higher duty = lower rail) */

/* Single-bit signals.  Roles marked ? are inferred from behaviour, not verified on the hardware. */
/*   P4.7  main power enable (first on / last off)      P5.2  global charge enable
 *   P5.3  input-profile enable, set when strap P3.4=0 ?  P3.4  input strap (DC / Lipo) ?
 *   P3.2  button 1 (INT0)   P3.6  button 2 (INT2)
 *   P4.6  buzzer, 1 kHz square wave in the beep bursts; P0.7 = FAN enable (on at boot, then by the thermistor ADC0, see sup.c)   */

#endif
