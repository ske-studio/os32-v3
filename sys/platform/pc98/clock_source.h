#ifndef PC98_CLOCK_SOURCE_H
#define PC98_CLOCK_SOURCE_H
#include "../../hal/time/clock_source.h"
#include "sys/kern/io_clock.h"

extern const struct io_clock_ops pc98_io_clock_ops;
/* A5 installs this after platform clock initialization (IO_TIER P6). */
void pc98_io_clock_install(void);
#endif
