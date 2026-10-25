/* per-port ADC channel lookup (tables live in board.h) */
#ifndef PORTS_H
#define PORTS_H
#include "compat.h"
uint8_t port_v_ch(uint8_t port);     /* port voltage sense channel */
uint8_t port_i_ch(uint8_t port);     /* port current sense channel */
#endif
