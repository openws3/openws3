/*
 * USB telemetry (pages 1..3, layout in USB.md), built here once a control pass from the app's live state.
 * The app owns the source data (the charger and supervisor outputs, the screens, ...); it hands it over once,
 * at tele_init, as a context of pointers, so the packets always show what the app is doing without copying.
 */
#ifndef TELE_H
#define TELE_H
#include "compat.h"
#include "chg.h"
#include "sup.h"

/* the boot phases and the events the pages report (USB byte layout) */
enum { PH_RAIL_WAIT = 1, PH_SPLASH, PH_VCC_GATE, PH_SELFTEST, PH_INPUT, PH_RUN = 7, PH_FATAL = 0x80 };
enum { EV_PORT_FULL = 1, EV_PORT_FAULT, EV_STOPPED };

/* the app state the pages read: pointers into the app's own variables, all live (tele_init copies them once) */
typedef struct {
    chg_out_t XD *out;         /* the charger's output (chg.h) */
    chg_in_t  XD *in;          /* what the charger was given */
    sup_out_t XD *sup;         /* the supervisor's output (sup.h) */
    int16_t   XD *rail_duty;   /* the rail actuator duty */
    uint8_t   XD *screen;      /* which screen is showing (app.h SCR_*) */
    uint8_t   XD *run, *rail_hold, *strap, *stop, *alarm, *stack;
} tele_ctx_t;

void    tele_init(const tele_ctx_t XD *ctx);
void    tele_pass(uint8_t ph);                   /* one pass: page 1 and page 2 or 3 */
void    tele_event(uint8_t code, uint8_t port);  /* EV_*: a port finished / faulted, or a stop */
void    tele_cmd(uint8_t seq, uint8_t res);      /* the echo of the last host command */
void    tele_put16(uint8_t XD *b, uint8_t at, uint16_t v);   /* big-endian 16-bit into a page */

#endif