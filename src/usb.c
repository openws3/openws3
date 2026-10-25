/*
 * USB HID monitor interface for the STC8H USB device.  The SIE sequence (register indices, endpoint set-up, control
 * transfer handling) is the one proven on this board by a bench firmware; everything else is new.
 *
 * Endpoint 0: control.  Endpoint 1: interrupt IN (telemetry) and OUT (commands), 64 bytes, polled every 10 ms.
 * Only standard enumeration requests and SET_IDLE are answered; anything else is stalled.
 */
#include "usb.h"

__sfr __at (0xAF) IE2;
__sfr __at (0xB8) IP;
__sfr __at (0xDC) USBCLK;
__sfr __at (0xF4) USBCON;
__sfr __at (0xFC) USBADR;
__sfr __at (0xEC) USBDAT;
__sfr __at (0xBA) P_SW2;
__xdata __at (0xFE07) volatile uint8_t IRC48MCR;

/* SIE register indices */
enum { FADDR = 0, POWER = 1, INTRIN1 = 2, INTROUT1 = 4, INTRUSB = 6, INTRIN1E = 7, INTROUT1E = 9, INTRUSBE = 11,
       INDEX = 14, INMAXP = 16, CSR0 = 17, INCSR1 = 17, INCSR2 = 18, OUTMAXP = 19, OUTCSR1 = 20, OUTCSR2 = 21,
       COUNT0 = 22, FIFO0 = 32, FIFO1 = 33 };
#define EP0IF      0x01
#define EP1INIF    0x02
#define EP1OUTIF   0x02
#define RSTIF      0x04
#define INCLRDT    0x40
#define INSTSTL    0x20
#define INUNDRUN   0x04
#define INIPRDY    0x01
#define INMODEIN   0x20
#define INMODEOUT  0x00
#define OUTCLRDT   0x80
#define OUTSTSTL   0x40
#define OUTOPRDY   0x01
#define OUTAUTOCLR 0x80
#define SSUEND     0x80
#define SOPRDY     0x40
#define SDSTL      0x20
#define SUEND      0x10
#define DATEND     0x08
#define STSTL      0x04
#define IPRDY      0x02
#define OPRDY      0x01

enum { EPIDLE = 0, EPSTATUS, EPDATAIN, EPDATAOUT, EPSTALL };

#define REQ_GET_DESCRIPTOR 0x06
#define REQ_SET_ADDRESS    0x05
#define REQ_SET_CONFIG     0x09
#define REQ_SET_IDLE       0x0A
#define DESC_DEVICE        0x01
#define DESC_CONFIG        0x02
#define DESC_STRING        0x03
#define DESC_HIDREPORT     0x22
#define TYPE_MASK          0x60
#define TYPE_STANDARD      0x00
#define TYPE_CLASS         0x20

typedef struct { uint8_t type, request, value_l, value_h, index_l, index_h, length_l, length_h; } setup_t;

static const __code uint8_t DEV_DESC[18] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
    0xbf, 0x34, 0x04, 0x10, 0x00, 0x01,            /* VID 34bf, PID 1004 */
    0x01, 0x02, 0x00, 0x01
};
static const __code uint8_t CFG_DESC[41] = {
    0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,                 /* one HID interface, two endpoints */
    0x09, 0x21, 0x01, 0x01, 0x00, 0x01, 0x22, 0x17, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x0A,                             /* EP1 IN,  interrupt, 64 B, 10 ms */
    0x07, 0x05, 0x01, 0x03, 0x40, 0x00, 0x0A                              /* EP1 OUT, interrupt, 64 B, 10 ms */
};
static const __code uint8_t HID_DESC[23] = {
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x15, 0x00, 0x25, 0xff, 0x75, 0x08, 0x95, 0x40,
    0x09, 0x01, 0x81, 0x02, 0x09, 0x01, 0x91, 0x02, 0xc0
};
static const __code uint8_t STR_LANG[4]  = { 0x04, 0x03, 0x09, 0x04 };
static const __code uint8_t STR_MANUF[8] = { 0x08, 0x03, 'S', 0, 'T', 0, 'C', 0 };
static const __code uint8_t STR_PROD[16] = { 0x10, 0x03, 'O', 0, 'p', 0, 'e', 0, 'n', 0, 'W', 0, 'S', 0, '3', 0 };

uint8_t XD usb_tx[USB_REPORT];

static setup_t XD setup;
static uint8_t XD ep0_stage;
static uint16_t XD ep0_left;
static const __code uint8_t * XD ep0_ptr;
static uint8_t XD rx[8];
static volatile uint8_t XD rx_pending, configured, start_stream;

static uint8_t sie_read(uint8_t addr)
{
    while (USBADR & 0x80) ;
    USBADR = (uint8_t)(addr | 0x80);
    while (USBADR & 0x80) ;
    return USBDAT;
}

static void sie_write(uint8_t addr, uint8_t v)
{
    while (USBADR & 0x80) ;
    USBADR = (uint8_t)(addr & 0x7F);
    USBDAT = v;
}

static void fifo_put_code(uint8_t fifo, const __code uint8_t *p, uint8_t n) { while (n--) sie_write(fifo, *p++); }

static void arm_in(void)
{
    uint8_t i;
    sie_write(INDEX, 1);
    sie_write(INCSR2, INMODEIN);
    for (i = 0; i < USB_REPORT; i++) sie_write(FIFO1, usb_tx[i]);
    sie_write(INCSR1, INIPRDY);
}

void usb_init(void)
{
    P_SW2 |= 0x80;                     /* XFR access */
    IRC48MCR = 0x80;                   /* 48 MHz internal oscillator for the SIE */
    while (!(IRC48MCR & 0x01)) ;
    USBCLK = 0x00;
    USBCON = 0x90;                     /* USB enable, D+ pull-up */

    sie_write(FADDR, 0x00);
    sie_write(POWER, 0x08);
    sie_write(INTRIN1E, 0x3F);
    sie_write(INTROUT1E, 0x3F);
    sie_write(INTRUSBE, 0x00);
    sie_write(POWER, 0x01);

    ep0_stage = EPIDLE; rx_pending = 0; configured = 0; start_stream = 0;
    IP |= 0x02;                        /* the 0.5 ms tick (timer 0) outranks USB */
    IE2 |= 0x80;                       /* EUSB */
}

uint8_t usb_configured(void) { return configured; }

uint8_t usb_cmd_get(uint8_t *cmd8)
{
    uint8_t i, got = 0;
    IE2 &= (uint8_t)~0x80;
    if (rx_pending) { for (i = 0; i < 8; i++) cmd8[i] = rx[i]; rx_pending = 0; got = 1; }
    IE2 |= 0x80;
    return got;
}

static void ep0_send_chunk(void)
{
    uint8_t n = ep0_left > 64 ? 64 : (uint8_t)ep0_left;
    fifo_put_code(FIFO0, ep0_ptr, n);
    ep0_left -= n; ep0_ptr += n;
    if (ep0_left == 0) { sie_write(CSR0, IPRDY | DATEND); ep0_stage = EPIDLE; }
    else sie_write(CSR0, IPRDY);
}

static void ep0_setup(void)
{
    uint8_t i, len = 0;
    uint8_t *p = (uint8_t *)&setup;
    for (i = 0, len = sie_read(COUNT0); len && i < 8; len--, i++) p[i] = sie_read(FIFO0);
    ep0_stage = EPSTATUS;
    ep0_left = ((uint16_t)setup.length_h << 8) | setup.length_l;

    if ((setup.type & TYPE_MASK) == TYPE_STANDARD) {
        switch (setup.request) {
        case REQ_SET_ADDRESS: sie_write(FADDR, setup.value_l); break;
        case REQ_SET_CONFIG:
            sie_write(INDEX, 1); sie_write(INCSR2, INMODEIN);  sie_write(INMAXP, 64);
            sie_write(INDEX, 1); sie_write(INCSR2, INMODEOUT); sie_write(OUTMAXP, 64); sie_write(OUTCSR2, OUTAUTOCLR);
            sie_write(INDEX, 0);
            start_stream = 1; configured = 1;
            break;
        case REQ_GET_DESCRIPTOR: {
            uint16_t have = 0;
            ep0_stage = EPDATAIN;
            switch (setup.value_h) {
            case DESC_DEVICE: ep0_ptr = DEV_DESC; have = sizeof DEV_DESC; break;
            case DESC_CONFIG: ep0_ptr = CFG_DESC; have = sizeof CFG_DESC; break;
            case DESC_HIDREPORT: ep0_ptr = HID_DESC; have = sizeof HID_DESC; break;
            case DESC_STRING:
                if (setup.value_l == 0) { ep0_ptr = STR_LANG; have = sizeof STR_LANG; }
                else if (setup.value_l == 1) { ep0_ptr = STR_MANUF; have = sizeof STR_MANUF; }
                else if (setup.value_l == 2) { ep0_ptr = STR_PROD; have = sizeof STR_PROD; }
                else ep0_stage = EPSTALL;
                break;
            default: ep0_stage = EPSTALL; break;
            }
            if (have < ep0_left) ep0_left = have;
            break; }
        default: ep0_stage = EPSTALL; break;
        }
    } else if ((setup.type & TYPE_MASK) == TYPE_CLASS) {
        if (setup.request != REQ_SET_IDLE) ep0_stage = EPSTALL;
    } else {
        ep0_stage = EPSTALL;
    }

    switch (ep0_stage) {
    case EPDATAIN:
        sie_write(CSR0, SOPRDY);
        ep0_send_chunk();
        break;
    case EPSTATUS:
        sie_write(INDEX, 0);
        sie_write(CSR0, SOPRDY | DATEND);
        ep0_stage = EPIDLE;
        if (start_stream) { start_stream = 0; arm_in(); }
        break;
    default:                           /* EPSTALL */
        sie_write(INDEX, 0); sie_write(CSR0, SOPRDY | SDSTL);
        ep0_stage = EPIDLE;
        break;
    }
}

void usb_isr(void)
{
    uint8_t usb_if = sie_read(INTRUSB), in_if = sie_read(INTRIN1), out_if = sie_read(INTROUT1), csr, i;

    if (usb_if & RSTIF) {
        sie_write(INDEX, 1); sie_write(INCSR1, INCLRDT);
        sie_write(INDEX, 1); sie_write(OUTCSR1, OUTCLRDT);
        ep0_stage = EPIDLE; configured = 0;
    }

    if (in_if & EP0IF) {
        sie_write(INDEX, 0);
        csr = sie_read(CSR0);
        if (csr & STSTL) { sie_write(CSR0, csr & (uint8_t)~STSTL); ep0_stage = EPIDLE; }
        if (csr & SUEND) sie_write(CSR0, csr | SSUEND);
        if (ep0_stage == EPIDLE) { if (csr & OPRDY) ep0_setup(); }
        else if (ep0_stage == EPDATAIN && !(csr & IPRDY)) ep0_send_chunk();
    }

    if (in_if & EP1INIF) {
        sie_write(INDEX, 1);
        csr = sie_read(INCSR1);
        if (csr & INSTSTL) sie_write(INCSR1, INCLRDT);
        if (csr & INUNDRUN) sie_write(INCSR1, 0);
        arm_in();
    }

    if (out_if & EP1OUTIF) {
        sie_write(INDEX, 1);
        sie_write(INCSR2, INMODEOUT);
        csr = sie_read(OUTCSR1);
        if (csr & OUTSTSTL) sie_write(OUTCSR1, OUTCLRDT);
        if (csr & OUTOPRDY) {
            uint8_t n = sie_read(COUNT0);
            for (i = 0; i < n; i++) { uint8_t b = sie_read(FIFO1); if (i < 8) rx[i] = b; }
            sie_write(OUTCSR1, 0);
            rx_pending = 1;
        }
    }
}
