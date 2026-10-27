#include "ctl.h"

#define RAIL_I_LIMIT  100000L        /* |S| cap: S/1000 alone can span the whole duty range */

static XD int32_t rail_i;       /* S: sum of errors */
static XD int32_t rail_q8;      /* duty * 256 */

int16_t ctl_clamp_duty(int32_t duty)
{
    if (duty < 0) return 0;
    if (duty > PWM_PERIOD) return PWM_PERIOD;
    return (int16_t)duty;
}

int16_t ctl_port_step(int16_t duty, int16_t iset, int16_t imeas)
{
    int16_t err = iset - imeas;                                  /* 16 bits: the 8051 has no cheap 32-bit divide */
    int16_t half = err >= 0 ? err >> 1 : -((-err + 1) >> 1);    /* floor(err/2) */
    return ctl_clamp_duty((int32_t)duty + half);
}

void ctl_rail_reset(int16_t duty)
{
    rail_i = 0;
    rail_q8 = (int32_t)ctl_clamp_duty(duty) << 8;
}

int32_t ctl_rail_integral(void) { return rail_i; }

int16_t ctl_rail_step(int16_t meas, int16_t setpoint)
{
    const int32_t top = (int32_t)PWM_PERIOD << 8;
    int32_t err = (int32_t)meas - setpoint;

    rail_i += err;
    if (rail_i >  RAIL_I_LIMIT) rail_i =  RAIL_I_LIMIT;
    if (rail_i < -RAIL_I_LIMIT) rail_i = -RAIL_I_LIMIT;
    rail_q8 += ((err * 6554) >> 8) + ((rail_i * 16777) >> 16);     /* x 256/10 and x 256/1000: multiply and shift, no 32-bit divide */
    if (rail_q8 > top)      { rail_q8 = top; if (err > 0) rail_i -= err; }   /* anti-windup */
    else if (rail_q8 < 0)   { rail_q8 = 0;   if (err < 0) rail_i -= err; }
    return (int16_t)((rail_q8 + 128) >> 8);
}
