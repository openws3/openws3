#include "cal.h"

uint16_t cal_port_mv(uint16_t raw)
{
    return (uint16_t)(((uint32_t)raw * 1125u) >> 10);          /* 4500/4096 */
}

uint16_t cal_port_ma(uint16_t raw)
{
    return (uint16_t)(((uint32_t)raw * 4000u + 136u) / 273u);   /* 1000/68.25 */
}

uint16_t cal_ma_to_port(uint16_t ma)
{
    return (uint16_t)((uint32_t)ma * 273u / 4000u);
}

uint16_t cal_vin_mv(uint16_t raw)
{
    /* (raw+1)*4/61 in 0.1 V  ->  mV = (raw+1) * 400 / 61 */
    return (uint16_t)((((uint32_t)raw + 1u) * 400u) / 61u);
}

uint16_t cal_set_to_ma(uint8_t units)
{
    return (uint16_t)((uint32_t)units * 100u / CUR_UNITS_PER_100MA);
}

uint8_t cal_ma_step_up(uint8_t units)
{
    uint16_t n = (uint16_t)units + CUR_UNITS_PER_100MA;
    if (units < CUR_MIN || n > CUR_MAX) return CUR_MIN;   /* wraps; also repairs an erased/garbage setting */
    return (uint8_t)n;
}
