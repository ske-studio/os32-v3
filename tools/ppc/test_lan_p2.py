#!/usr/bin/env python3
"""Compile the GEM contract and compare it with the OpenBSD reference."""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

CHECK = dict(name="ppc-lan-p2", tier=2, budget=60,
             covers=["drivers/net/gem/gem.h", "tools/ppc/test_lan_p2.py",
                     "include/types.h", "include/callconv.h", "build/ppc-kboot.mk"])

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "drivers/net/gem/gem.h"
BSD = Path("/home/hight/os32-tmp/ref/openbsd/sys/dev/ic/gemreg.h")
LAN = ROOT / "platform/powermac/lan_boot.h"
# Explicit product-to-reference mapping; offsets in OpenBSD are bank-relative.
PAIRS = [(name, name) for name in """
GEM_ERROR_STATUS GEM_ERROR_STAT_BADACK GEM_ERROR_STAT_DTRTO GEM_ERROR_STAT_OTHERS GEM_MII_CONFIG GEM_MII_DATAPATH_MODE GEM_MII_CONFIG_ENABLE GEM_MII_DATAPATH_SERIAL GEM_MII_DATAPATH_SERDES GEM_MII_DATAPATH_MII GEM_MII_DATAPATH_MIIOUT GEM_CONFIG GEM_INTMASK GEM_STATUS_ALIAS GEM_PCI_BANK2_OFFSET GEM_RESET GEM_CONFIG_BURST_INF GEM_CONFIG_TXDMA_LIMIT GEM_CONFIG_RXDMA_LIMIT GEM_CONFIG_RONPAULBIT GEM_CONFIG_BUG2FIX GEM_INTR_RX_TAG_ERR GEM_INTR_TX_MAC GEM_INTR_RX_MAC GEM_INTR_BERR GEM_RESET_TX GEM_RESET_RX GEM_TX_KICK GEM_TX_CONFIG GEM_TX_RING_PTR_LO GEM_TX_RING_PTR_HI GEM_TX_COMPLETION GEM_TX_CONFIG_TXDMA_EN GEM_TX_CONFIG_TXRING_SZ GEM_TX_CONFIG_TXFIFO_TH GEM_RING_SZ_32 GEM_TX_COMPLETION_MASK GEM_RX_CONFIG GEM_RX_RING_PTR_LO GEM_RX_RING_PTR_HI GEM_RX_FIFO_WR_PTR GEM_RX_FIFO_RD_PTR GEM_RX_PAUSE_THRESH GEM_RX_KICK GEM_RX_BLANKING GEM_RX_FIFO_SIZE GEM_RX_CONFIG_RXDMA_EN GEM_RX_CONFIG_RXRING_SZ GEM_RX_CONFIG_FBOFF GEM_RX_CONFIG_CXM_START GEM_RX_CONFIG_FIFO_THRS GEM_THRSH_1024 GEM_RX_CONFIG_FIFO_THRS_SHIFT GEM_RX_CONFIG_FBOFF_SHFT GEM_RX_PTH_XOFF_THRESH GEM_RX_PTH_XON_THRESH GEM_MAC_SEND_PAUSE_CMD GEM_MAC_TX_STATUS GEM_MAC_RX_STATUS GEM_MAC_TX_MASK GEM_MAC_RX_MASK GEM_MAC_CONTROL_MASK GEM_MAC_TX_CONFIG GEM_MAC_RX_CONFIG GEM_MAC_CONTROL_CONFIG GEM_MAC_XIF_CONFIG GEM_MAC_IPG0 GEM_MAC_IPG1 GEM_MAC_IPG2 GEM_MAC_MAC_MIN_FRAME GEM_MAC_MAC_MAX_FRAME GEM_MAC_PREAMBLE_LEN GEM_MAC_JAM_SIZE GEM_MAC_ATTEMPT_LIMIT GEM_MAC_CONTROL_TYPE GEM_MAC_ADR_FLT_MASK1_2 GEM_MAC_ADR_FLT_MASK0 GEM_MAC_NORM_COLL_CNT GEM_MAC_FIRST_COLL_CNT GEM_MAC_EXCESS_COLL_CNT GEM_MAC_LATE_COLL_CNT GEM_MAC_DEFER_TMR_CNT GEM_MAC_PEAK_ATTEMPTS GEM_MAC_RX_FRAME_COUNT GEM_MAC_RX_LEN_ERR_CNT GEM_MAC_RX_ALIGN_ERR GEM_MAC_RX_CRC_ERR_CNT GEM_MAC_RX_CODE_VIOL GEM_MAC_RANDOM_SEED GEM_MAC_MAC_STATE GEM_MAC_TX_UNDERRUN GEM_MAC_TX_PKT_TOO_LONG GEM_MAC_RX_DONE GEM_MAC_RX_OVERFLOW GEM_MAC_RX_FRAME_CNT GEM_MAC_XIF_TX_MII_ENA GEM_MAC_XIF_GMII_MODE GEM_MAC_XIF_LINK_LED GEM_MAC_XIF_FDPLX_LED GEM_MAC_TX_ENABLE GEM_MAC_TX_IGN_CARRIER GEM_MAC_TX_IGN_COLLIS GEM_MAC_TX_ENA_IPG0 GEM_MAC_TX_NGU GEM_MAC_TX_NGU_LIMIT GEM_MAC_RX_ENABLE GEM_MAC_RX_STRIP_CRC GEM_MAC_RX_PROMISCUOUS GEM_MAC_RX_PROMISC_GRP GEM_MAC_RX_HASH_FILTER GEM_MAC_CC_TX_PAUSE GEM_MAC_CC_RX_PAUSE GEM_MAC_STATE_OVERFLOW GEM_MIF_FRAME GEM_MIF_CONFIG GEM_MIF_FRAME_DATA GEM_MIF_FRAME_TA0 GEM_MIF_FRAME_READ GEM_MIF_FRAME_WRITE GEM_MIF_REG_SHIFT GEM_MIF_PHY_SHIFT GEM_MIF_CONFIG_PHY_SEL GEM_MIF_CONFIG_POLL_ENA GEM_MIF_CONFIG_BB_ENA GEM_MIF_CONFIG_MDI0 GEM_MIF_CONFIG_MDI1 GEM_TD_BUFSIZE GEM_TD_END_OF_PACKET GEM_TD_START_OF_PACKET GEM_RD_BUFSIZE GEM_RD_OWN GEM_RD_BAD_CRC GEM_RD_BUFSHIFT GEM_MAC_ADDR0 GEM_MAC_ADDR1 GEM_MAC_ADDR2 GEM_MAC_ADDR3 GEM_MAC_ADDR4 GEM_MAC_ADDR5 GEM_MAC_ADDR6 GEM_MAC_ADDR7 GEM_MAC_ADDR8 GEM_MAC_ADDR_FILTER0 GEM_MAC_ADDR_FILTER1 GEM_MAC_ADDR_FILTER2 GEM_MAC_HASH0 GEM_MAC_HASH1 GEM_MAC_HASH2 GEM_MAC_HASH3 GEM_MAC_HASH4 GEM_MAC_HASH5 GEM_MAC_HASH6 GEM_MAC_HASH7 GEM_MAC_HASH8 GEM_MAC_HASH9 GEM_MAC_HASH10 GEM_MAC_HASH11 GEM_MAC_HASH12 GEM_MAC_HASH13 GEM_MAC_HASH14 GEM_MAC_HASH15
""".split()]
EXTRA = {"GEM_DESC_FLAGS_OFFSET": 0, "GEM_DESC_ADDR_OFFSET": 8,
         "GEM_DESC_SIZE": 16, "GEM_RING_COUNT": 32,
         "GEM_RX_PUBLISHED": 28, "GEM_MMIO_SPAN": 0x10000}
ERRORS = {"OK": 0, "AGAIN": 1, "EINVAL": -1, "ENODEV": -2,
          "ERANGE": -3, "ETIME": -4, "EIO": -5, "EUNSUPPORTED": -6,
          "EBUSY": -7}
MUTATIONS = [
    ("rx-kick", "#define GEM_RX_KICK 0x4100 ", "#define GEM_RX_KICK 0x4104 ", "GEM_RX_KICK"),
    ("own", "#define GEM_RD_OWN 0x0000000080000000LL ", "#define GEM_RD_OWN 0x0000000040000000LL ", "GEM_RD_OWN"),
    ("ring", "#define GEM_RING_COUNT 32\n", "#define GEM_RING_COUNT 64\n", "GEM_RING_COUNT"),
    ("span", "#define GEM_MMIO_SPAN 0x10000\n", "#define GEM_MMIO_SPAN 0x200000\n", "GEM_MMIO_SPAN"),
]
PPC = "/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc"
FLAGS = """-std=gnu11 -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec
-msdata=none -ffreestanding -fno-pie -fno-stack-protector -nostdlib -fcommon
-fsigned-char -fno-short-enums -fno-builtin -fno-asynchronous-unwind-tables -O2
-Wall -Wextra -Werror -Werror=vla -Werror=implicit-int
-Werror=implicit-function-declaration -ffunction-sections -fdata-sections
-D__cdecl= -D__KERNEL_BUILD__ -DKBOOT_NEGATIVE=0 -MMD -MP""".split()


CONTRACT = r"""
#define CHECK_FIELD(s, field, type) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(((struct s *)0)->field), type), \
                   "field type: " #s "." #field)
typedef u32 (*read32_type)(void *, u32);
typedef void (*write32_type)(void *, u32, u32);
typedef unsigned long long (*tb_type)(void *);
typedef int (*sync_type)(void *, u32, enum dma_sync);
CHECK_FIELD(gem_dma, cpu, void *);
CHECK_FIELD(gem_dma, pa, u32);
CHECK_FIELD(gem_dma, bus, u32);
CHECK_FIELD(gem_dma, bytes, u32);
CHECK_FIELD(gem_io, ctx, void *);
CHECK_FIELD(gem_io, read32, read32_type);
CHECK_FIELD(gem_io, write32, write32_type);
CHECK_FIELD(gem_io, tb, tb_type);
CHECK_FIELD(gem_io, tb_hz, u32);
CHECK_FIELD(gem_io, sync, sync_type);
CHECK_FIELD(gem, io, struct gem_io);
CHECK_FIELD(gem, dma, struct gem_dma);
CHECK_FIELD(gem, state, u32);
CHECK_FIELD(gem, step, u32);
CHECK_FIELD(gem, cursor, u32);
CHECK_FIELD(gem, polls, u32);
CHECK_FIELD(gem, phy_addr, u32);
CHECK_FIELD(gem, phy_port, u32);
CHECK_FIELD(gem, tx_head, u32);
CHECK_FIELD(gem, tx_tail, u32);
CHECK_FIELD(gem, tx_pending, u32);
CHECK_FIELD(gem, rx_head, u32);
CHECK_FIELD(gem, rx_watch_wr, u32);
CHECK_FIELD(gem, rx_watch_rd, u32);
CHECK_FIELD(gem, rx_watching, u32);
CHECK_FIELD(gem, started, unsigned long long);
CHECK_FIELD(gem, tx_started, unsigned long long);
CHECK_FIELD(gem, rx_watch_started, unsigned long long);
CHECK_FIELD(gem, tx_packets, unsigned long long);
CHECK_FIELD(gem, rx_packets, unsigned long long);
CHECK_FIELD(gem, rx_drops, unsigned long long);
CHECK_FIELD(gem, uncertain_tx, unsigned long long);
void gem_contract(struct gem *g, struct gem_io *io, struct gem_dma *d) {
    g->io = *io; g->dma = *d;
    g->state = g->step = g->cursor = g->polls = 0;
    g->phy_addr = g->phy_port = 0;
    g->tx_head = g->tx_tail = g->tx_pending = g->rx_head = 0;
    g->rx_watch_wr = g->rx_watch_rd = g->rx_watching = 0;
    g->started = g->tx_started = g->rx_watch_started = 0;
    g->tx_packets = g->rx_packets = g->rx_drops = g->uncertain_tx = 0;
    d->cpu = io->ctx; d->pa = d->bus = d->bytes = io->tb_hz;
    u32 (*read32)(void *, u32) = io->read32;
    void (*write32)(void *, u32, u32) = io->write32;
    unsigned long long (*tb)(void *) = io->tb;
    int (*sync)(void *, u32, enum dma_sync) = io->sync;
    int (*reset)(struct gem *, unsigned long long) = gem_reset_step;
    int (*mac)(struct gem *, const u8 *) = gem_mac_init;
    int (*mif)(struct gem *) = gem_mif_init;
    int (*phy)(struct gem *, unsigned long long) = gem_phy_step;
    int (*prepare)(struct gem *, struct gem_dma *) = gem_ring_prepare;
    int (*publish)(struct gem *) = gem_ring_publish;
    int (*enable)(struct gem *, unsigned long long) = gem_mac_enable_step;
    int (*tx)(struct gem *, const void *, u32) = gem_tx;
    int (*rx)(struct gem *, void *, u32, u32 *) = gem_rx;
    int (*poll)(struct gem *, unsigned long long, u32) = gem_poll;
    (void)read32; (void)write32; (void)tb; (void)sync;
    (void)reset; (void)mac; (void)mif; (void)phy; (void)prepare;
    (void)publish; (void)enable; (void)tx; (void)rx; (void)poll;
}
"""

def execute(cmd):
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)


def check(header, directory, values):
    directory = Path(directory)
    (directory / "gem.h").write_text(header)
    lines = ['#include "gem.h"', '#include <stdio.h>', 'int main(void) {']
    for name, value in values.items():
        lines += [f'puts("reached={name}");',
                  f'if ((unsigned long long)({name}) != (unsigned long long)({value})) '
                  f'{{ puts("mismatch={name}"); return 1; }}']
    lines += ['return 0;', '}']
    host = directory / "host.c"
    host.write_text("\n".join(lines) + "\n")
    cross = directory / "cross.c"
    cross.write_text('#include "gem.h"\n' + CONTRACT)
    includes = ["-I" + str(directory), "-I" + str(ROOT / "include")]
    for cmd in ([PPC, *FLAGS, *includes, "-c", str(cross), "-o", str(directory / "cross.o")],
                ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", *includes,
                 "-c", str(cross), "-o", str(directory / "host_contract.o")],
                ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", *includes,
                 str(host), "-o", str(directory / "host")]):
        result = execute(cmd)
        if result.returncode:
            raise RuntimeError("compile failed: " + result.stdout)
    return execute([str(directory / "host")])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mutate", action="store_true")
    args = parser.parse_args()
    if not BSD.is_file():
        print("OpenBSD reference missing: " + str(BSD))
        return 3
    source = BSD.read_text()
    definitions = dict(re.findall(r'^#define\s+(\w+)\s+([^\s/]+)', source, re.M))
    values = {product: definitions[reference] for product, reference in PAIRS}
    values.update(EXTRA)
    if LAN.exists():
        # Compare the compiler's actual enum values, without depending on formatting.
        for name in ERRORS:
            values["GEM_" + name] = "LAN_" + name
        lan_include = '#include "' + str(LAN) + '"\n'
    else:
        print("lan_boot.h absent: comparing error values with design section 3")
        values.update({"GEM_" + name: value for name, value in ERRORS.items()})
        lan_include = ""
    original = lan_include + HEADER.read_text()
    with tempfile.TemporaryDirectory(dir=None) as tmp:
        result = check(original, tmp, values)
        if result.returncode:
            print(result.stdout)
            return 1
        print(f"BSD definitions matched: {len(PAIRS)}; constants and errors matched")
        if args.mutate:
            before = "    u32 tb_hz;"
            after = "    u16 tb_hz;"
            if original.count(before) != 1:
                raise RuntimeError("mutation anchor is not unique: field-type")
            (Path(tmp) / "gem.h").write_text(original.replace(before, after))
            print("reached=field-type-compile")
            result = execute([PPC, *FLAGS, "-I" + tmp, "-I" + str(ROOT / "include"),
                              "-c", str(Path(tmp) / "cross.c"), "-o", str(Path(tmp) / "cross.o")])
            if result.returncode != 1 or 'static assertion failed: "field type: gem_io.tb_hz"' not in result.stdout:
                raise RuntimeError(f"unexpected field type mutation: rc={result.returncode} {result.stdout}")
            print('RED field-type: reached=field-type-compile static assertion failed: "field type: gem_io.tb_hz"')
            before = "int gem_reset_step(struct gem *, unsigned long long now);"
            after = "int gem_reset_step(struct gem *, u32 now);"
            if original.count(before) != 1:
                raise RuntimeError("mutation anchor is not unique: signature")
            (Path(tmp) / "gem.h").write_text(original.replace(before, after))
            print("reached=signature-compile")
            result = execute([PPC, *FLAGS, "-I" + tmp, "-I" + str(ROOT / "include"),
                              "-c", str(Path(tmp) / "cross.c"), "-o", str(Path(tmp) / "cross.o")])
            if result.returncode != 1 or "incompatible-pointer-types" not in result.stdout or "gem_reset_step" not in result.stdout:
                raise RuntimeError(f"unexpected signature mutation: rc={result.returncode} {result.stdout}")
            print("RED signature: reached=signature-compile incompatible-pointer-types gem_reset_step")
            for label, before, after, expected in MUTATIONS:
                if original.count(before) != 1:
                    raise RuntimeError("mutation anchor is not unique: " + label)
                result = check(original.replace(before, after), tmp, values)
                lines = result.stdout.splitlines()
                if result.returncode != 1 or f"reached={expected}" not in lines or f"mismatch={expected}" not in lines:
                    raise RuntimeError(f"unexpected mutation result {label}: rc={result.returncode} {result.stdout}")
                print(f"RED {label}: reached={expected} mismatch={expected}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, KeyError, subprocess.TimeoutExpired) as exc:
        print(f"test error: {exc}", file=sys.stderr)
        sys.exit(1)
