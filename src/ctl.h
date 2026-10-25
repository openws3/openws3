/*
 * Control laws.  Pure integer maths, no hardware, no floats.
 *
 * Integer implementations of two simple loops:
 *   per-port:  err = iset - imeas;  duty += 0.5*err;  clamp 0..400
 *   rail:      err = meas - setpoint; S += err;
 *                                      duty += 0.1*err + 0.001*S;  clamp 0..400
 * The rail integral S saturates (with anti-windup) instead of wrapping at 16 bits, and the rail duty keeps fractional state.
 */
#ifndef CTL_H
#define CTL_H
#include "compat.h"
#include "board.h"

int16_t ctl_clamp_duty(int32_t duty);

/* Per-port current loop: returns the new duty: duty + floor((iset - imeas)/2), clamped.  Same units for iset and imeas. */
int16_t ctl_port_step(int16_t duty, int16_t iset, int16_t imeas);

/* Rail PI.  Higher duty lowers the rail (measured), so a rail above setpoint raises duty.
 * The controller owns its output (kept in Q8 so truncation never discards the remainder).  Gains are per call. */
void    ctl_rail_reset(int16_t duty);
int16_t ctl_rail_step(int16_t meas, int16_t setpoint);   /* returns the new duty, 0..PWM_PERIOD */
int32_t ctl_rail_integral(void);

#endif
