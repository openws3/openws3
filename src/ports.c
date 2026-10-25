#include "ports.h"
#include "board.h"
static const CODE uint8_t vch[NPORTS] = PORT_ADC_V_INIT;
static const CODE uint8_t ich[NPORTS] = PORT_ADC_I_INIT;
uint8_t port_v_ch(uint8_t port) { return vch[port]; }
uint8_t port_i_ch(uint8_t port) { return ich[port]; }
