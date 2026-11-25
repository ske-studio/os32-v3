#pragma once
#include "ide_bus.h"
#include "ppc_oea.h"
#include "kboot_info.h"
#include "kernel_boot.h"

#define ATAPI_PROBE_TARGET(i) ((i) == pm_cd_target)
#define ATAPI_CONTROLLER pm_cd_bank
#define ATAPI_DELAY_MAX 100000u
static inline void atapi_bus_delay(u32 us)
{
    u32 start = ppc_read_tb();
    u32 ticks = (ppc_boot_info()->tb_hz / 1000000u + 1u) * us;
    while (ppc_read_tb() - start < ticks) {}
}

static inline void atapi_pio_send_cdb(const u8 *cdb)
{
    ide_bus_data_write(cdb, ATAPI_CDB_BYTES / sizeof(u16));
}

#define ATAPI_NATIVE_LE_WORDS 0
