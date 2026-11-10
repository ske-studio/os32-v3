/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * Copyright (c) 2000, Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000, BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* PCI Local Bus 3.0 section 6.2.5.1; Intel 8255x SDM rev 1.3
 * section 4.1.10. BSD reference: FreeBSD releng/14.3 pci.c:3117-3201
 * pci_read_bar and :3320-3417 pci_add_map. No resource reassignment. */
#include <stdbool.h>
#include "pci.h"
#include "pci_barsize.h"
#include "kprintf.h"
#include "io.h"

#define PCI_RESOURCE_SLOTS (PCI_CFG_BAR_COUNT + 1)
#define PCI_ROM_ADDR_MASK 0xfffff800U
#define PCI_ROM_SIZE_VALUE 0xfffffffeU
struct pci_resource_record {
    u32 bus, dev, fn;
    struct pci_bar_resource bars[PCI_RESOURCE_SLOTS];
    u32 rom_reg;
};
static struct pci_resource_record records[PCI_MAX_DEVS];
static u32 record_count;

void pci_barsize_reset(void) { record_count = 0; }

static void measure_locked(u32 bus, u32 dev, u32 fn, u32 reg, bool rom,
                    bool allow64, struct pci_bar_resource *r)
{
    *r = (struct pci_bar_resource){0};
    r->raw = pci_cfg_read32(bus, dev, fn, reg);
    bool io = !rom && (r->raw & PCI_BAR_SPACE_IO);
    bool wide = !rom && !io && (r->raw & PCI_BAR_MEM_TYPE) == PCI_BAR_MEM_TYPE64;
    r->kind = rom ? PCI_BAR_MEM32 : io ? PCI_BAR_IO : wide ? PCI_BAR_MEM64 :
        (r->raw & PCI_BAR_MEM_TYPE) == 2 ? PCI_BAR_MEM1M : PCI_BAR_MEM32;
    r->prefetch = !rom && !io && !!(r->raw & PCI_BAR_MEM_PREFETCH);
    if (wide && !allow64) return;
    if (wide) r->high = pci_cfg_read32(bus, dev, fn, reg + PCI_CFG_BAR_STRIDE);
    u32 addr_mask = rom ? PCI_ROM_ADDR_MASK : io ? PCI_BAR_IO_ADDR_MASK : PCI_BAR_MEM_ADDR_MASK;
    r->base = ((unsigned long long)r->high << 32) | (r->raw & addr_mask);
    u16 cmd = pci_cfg_read16(bus, dev, fn, PCI_CFG_COMMAND);
    /* ROM: FreeBSD does not alter COMMAND; bit0 stays clear in the probe.
     * Ordinary BARs disable the corresponding decoder, preserving bus master. */
    if (!rom) pci_cfg_write16(bus, dev, fn, PCI_CFG_COMMAND,
                             cmd & ~(io ? PCI_CMD_IO_ENABLE : PCI_CMD_MEM_ENABLE));
    pci_cfg_write32(bus, dev, fn, reg, rom ? PCI_ROM_SIZE_VALUE : U32_MAX);
    u32 high = 0;
    if (wide) {
        pci_cfg_write32(bus, dev, fn, reg + PCI_CFG_BAR_STRIDE, U32_MAX);
        high = pci_cfg_read32(bus, dev, fn, reg + PCI_CFG_BAR_STRIDE);
    }
    u32 low = pci_cfg_read32(bus, dev, fn, reg);
    pci_cfg_write32(bus, dev, fn, reg, r->raw);
    if (wide) pci_cfg_write32(bus, dev, fn, reg + PCI_CFG_BAR_STRIDE, r->high);
    if (!rom) pci_cfg_write16(bus, dev, fn, PCI_CFG_COMMAND, cmd);
    unsigned long long mask = ((unsigned long long)high << 32) | (low & addr_mask);
    if (!mask || (!rom && io && (low & 2U)) ||
        (!rom && !io && (r->raw & PCI_BAR_MEM_TYPE) == PCI_BAR_MEM_TYPE)) return;
    /* FreeBSD pci_mapsize uses the lowest implemented address bit, including
     * legacy I/O BARs whose upper 16 mask bits read as zero. */
    r->len = mask & (0ULL - mask);
    if (r->len < (rom ? 2048U : io ? 4U : 16U)) { r->len = 0; return; }
    r->unassigned = !r->base || (r->raw == low && r->high == high);
}

/* Keep the entire save/probe/restore transaction atomic, including callers
 * whose individual config accesses save and restore IRQ state themselves. */
static void measure(u32 bus, u32 dev, u32 fn, u32 reg, bool rom,
                    bool allow64, struct pci_bar_resource *r)
{
    unsigned int flags = irq_save();
    measure_locked(bus, dev, fn, reg, rom, allow64, r);
    irq_restore(flags);
}

void pci_barsize_record(u32 bus, u32 dev, u32 fn, u32 header)
{
    struct pci_resource_record scratch = {0};
    struct pci_resource_record *d = &scratch;
    if (record_count < PCI_MAX_DEVS) d = &records[record_count++];
    *d = (struct pci_resource_record){.bus=bus, .dev=dev, .fn=fn};
    u32 layout = header & PCI_HDR_LAYOUT_MASK;
    u32 bars = layout == PCI_HDR_LAYOUT_DEVICE ? PCI_CFG_BAR_COUNT :
               layout == PCI_HDR_LAYOUT_BRIDGE ? PCI_BRIDGE_BAR_COUNT :
               layout == PCI_HDR_LAYOUT_CARDBUS ? 1U : 0U;
    u32 class_rev = pci_cfg_read32(bus, dev, fn, PCI_CFG_REVISION_ID);
    bool ide = (class_rev >> 16) == 0x0101U;
    u32 progif = (class_rev >> 8) & 0xffU;
    for (u32 i = 0; i < bars; i++) {
        /* FreeBSD pci_ata_maps: compatibility channels use fixed legacy
         * ports, so their BAR pairs must not be probed. */
        if (ide && i < 4 && !(progif & (i < 2 ? 1U : 4U))) continue;
        struct pci_bar_resource *r = &d->bars[i];
        measure(bus, dev, fn, PCI_CFG_BAR0 + i * PCI_CFG_BAR_STRIDE, false, i + 1 < bars, r);
        if (r->len) kprintf(KLOG_ATTR,
            "[pci] %u:%u.%u bar%u %s%s len=0x%x%08x base=0x%x%08x\n",
            bus, dev, fn, i, r->kind == PCI_BAR_IO ? "io" :
            r->kind == PCI_BAR_MEM64 ? "mem64" : "mem32",
            r->prefetch ? " prefetch" : "", (u32)(r->len >> 32), (u32)r->len,
            (u32)(r->base >> 32), (u32)r->base);
        if (r->kind == PCI_BAR_MEM64 && i + 1 < bars) { i++; d->bars[i] = (struct pci_bar_resource){0}; }
    }
    d->rom_reg = layout == PCI_HDR_LAYOUT_DEVICE ? PCI_CFG_ROM_ENDPOINT :
                 layout == PCI_HDR_LAYOUT_BRIDGE ? PCI_CFG_ROM_BRIDGE : 0;
    /* Probe only ROMs enabled by firmware; never write a disabled ROM. */
    if (d->rom_reg && (pci_cfg_read32(bus, dev, fn, d->rom_reg) & 1U))
        measure(bus, dev, fn, d->rom_reg, true, false, &d->bars[PCI_CFG_BAR_COUNT]);
}

int pci_bar_get(u32 bus, u32 dev, u32 fn, u32 reg, void *out)
{
    if (!out) return -1;
    for (u32 i = 0; i < record_count; i++) {
        const struct pci_resource_record *d = &records[i];
        if (d->bus != bus || d->dev != dev || d->fn != fn) continue;
        u32 slot;
        if (reg >= PCI_CFG_BAR0 && reg < PCI_CFG_BAR0 + PCI_CFG_BAR_COUNT * PCI_CFG_BAR_STRIDE && !(reg & 3U))
            slot = (reg - PCI_CFG_BAR0) / PCI_CFG_BAR_STRIDE;
        else if (reg && reg == d->rom_reg) slot = PCI_CFG_BAR_COUNT;
        else return -1;
        *(struct pci_bar_resource *)out = d->bars[slot];
        return 0;
    }
    return -1;
}
