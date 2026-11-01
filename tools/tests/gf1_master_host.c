#include "hwdev/gf/foundation.h"
#include "drivers/pci/pci.h"
#include "kdebug/recover.h"
static u16 command, cache;
static u32 status_writes, writes, fail_write, bad_command, bad_latency, bad_reads;
static u8 header = 0x51, bist = 0xa7;
int pci_cfg_write16(u32 b, u32 d, u32 f, u32 reg, u16 value)
{
    if (b != RECOVER_GF_BUS || d != RECOVER_GF_DEV || f != RECOVER_GF_FN) return -1;
    writes++;
    if (fail_write == writes) return -1;
    if (reg == PCI_CFG_COMMAND) command = value;
    else if (reg == PCI_CFG_CACHE_LINE) cache = value;
    else return -1;
    return 0;
}
void pci_cfg_write32(u32 b, u32 d, u32 f, u32 reg, u32 value)
{ (void)b; (void)d; (void)f; (void)reg; (void)value; status_writes++; }
u16 pci_cfg_read16(u32 b, u32 d, u32 f, u32 reg)
{
    if (b != RECOVER_GF_BUS || d != RECOVER_GF_DEV || f != RECOVER_GF_FN ||
        (reg != PCI_CFG_COMMAND && reg != PCI_CFG_CACHE_LINE)) {
        bad_reads++;
        return 0;
    }
    if (reg == PCI_CFG_COMMAND) return command ^ (bad_command ? 1 : 0);
    return cache;
}
u8 pci_cfg_read8(u32 b, u32 d, u32 f, u32 reg)
{
    if (b != RECOVER_GF_BUS || d != RECOVER_GF_DEV || f != RECOVER_GF_FN ||
        reg != PCI_CFG_LATENCY_TIMER) {
        bad_reads++;
        return 0;
    }
    return (u8)((cache >> 8) ^ (bad_latency && writes > 1 ? 1 : 0));
}
#include MASTER_SOURCE
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int test(void)
{
    const u8 values[] = {0, 15, 16, 64, 255};
    for (unsigned initial = 0; initial < 2; initial++) for (unsigned i = 0; i < 5; i++) {
        struct gf_foundation f = {0};
        command = initial ? RECOVER_COMMAND_RUN : RECOVER_COMMAND_STOP;
        cache = ((u16)values[i] << 8) | 0x39; writes = status_writes = 0;
        CHECK(gf_master_enable(&f) == GF1_OK);
        CHECK(command == RECOVER_COMMAND_RUN && f.bm && f.expected_command == command);
        CHECK((cache & 255) == 0x39 && (cache >> 8) == (values[i] < 16 ? 64 : values[i]));
        CHECK(writes == (values[i] < 16 ? 2U : 1U));
        CHECK(!status_writes && header == 0x51 && bist == 0xa7);
        CHECK(gf_master_disable(&f) == GF1_OK);
        CHECK(command == RECOVER_COMMAND_STOP && !f.bm && f.expected_command == command);
        CHECK((cache >> 8) == (values[i] < 16 ? 64 : values[i]));
        CHECK(gf_master_enable(&f) == GF1_OK && command == RECOVER_COMMAND_RUN);
    }
    struct gf_foundation f = {0};
    writes = 0; fail_write = 1;
    CHECK(gf_master_enable(&f) == GF1_EPCI && !f.bm);
    fail_write = 0; writes = 0; bad_command = 1;
    CHECK(gf_master_enable(&f) == GF1_EPCI);
    bad_command = 0; cache = 0x0039; writes = 0; bad_latency = 1;
    CHECK(gf_master_enable(&f) == GF1_EPCI && f.bm && f.expected_command == RECOVER_COMMAND_RUN);
    bad_latency = 0; writes = 0; fail_write = 2; cache = 0x0039;
    CHECK(gf_master_enable(&f) == GF1_EPCI && f.bm);
    fail_write = 0; writes = 0; bad_command = 1;
    CHECK(gf_master_disable(&f) == GF1_EPCI && f.bm && f.expected_command == RECOVER_COMMAND_RUN);
    CHECK(!bad_reads);
    return 0;
}
void _start(void) { int rc = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
