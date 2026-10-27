/* Lets the pure modules (cal, ctl, ...) build for both SDCC/8051 and the host. */
#ifndef COMPAT_H
#define COMPAT_H

#include <stdint.h>

#ifdef __SDCC
#define XD   __xdata
#define CODE __code
#define REENT __reentrant   /* locals and compiler temporaries on the stack instead of in direct RAM (which is full): costs stack, not data */
#else
#define XD
#define CODE
#define REENT
#endif

#endif
