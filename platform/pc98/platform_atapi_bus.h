#pragma once
#include "cpu_calibrate.h"

#define ATAPI_PROBE_TARGET(i) 1
#define ATAPI_CONTROLLER 1
#define atapi_bus_delay cpu_delay_us
#define ATAPI_DELAY_MAX CPU_DELAY_US_MAX

static inline void atapi_pio_send_cdb(const u8 *cdb)
{
    for (int i = 0; i < ATAPI_CDB_BYTES / (int)sizeof(u16); i++) {
        u16 w = (u16)cdb[i * sizeof(u16)] | ((u16)cdb[i * sizeof(u16) + 1] << 8);
        outpw(IDE_DATA, (unsigned)w);
    }
}

#define ATAPI_NATIVE_LE_WORDS 1
