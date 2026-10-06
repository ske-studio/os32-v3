#!/usr/bin/env python3
"""Snapshot low PT0, or --compare base.json other.json [--ignore-ad].

Take the baseline after an application has run. Use the deployed kernel.map.
The fixed PD location comes from memmap.h; observation globals use kernel.map.
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from np21w_mcp import np21w_client as emu
from np21w_mcp.symbols import SymbolTable

PTE_COUNT = 256
WORD_BYTES = 4
AD_BITS = 0x60
PAGE_OFFSET_MASK = 0xfff
SYMBOLS = ("v86_restore_mismatch", "kselftest_pass", "kselftest_fail",
           "fault_kill_count", "gfx_current_height", "gfx_flip_enabled")


def compare(base, other, ignore_ad=False):
    mask = ~AD_BITS if ignore_ad else ~0
    for snapshot in (base, other):
        if not isinstance(snapshot, dict) or not isinstance(snapshot.get("ptes"), list) or len(snapshot["ptes"]) != PTE_COUNT:
            raise ValueError("snapshot must contain 256 PTEs")
        words = [snapshot.get("pde0")] + snapshot["ptes"]
        if any(type(word) is not int or not 0 <= word <= 0xffffffff for word in words):
            raise ValueError("snapshot words must be u32")
    # The acceptance records PDE0 unchanged; only PTE A/D changes are ignored.
    return base["pde0"] == other["pde0"] and all(
        (a & mask) == (b & mask) for a, b in zip(base["ptes"], other["ptes"]))


def snapshot(map_path, read_mem):
    symbols = SymbolTable(str(map_path))
    addresses = {}
    for name in SYMBOLS:
        address = symbols.resolve(name)
        if address is None:
            raise ValueError("symbol missing from kernel.map: " + name)
        addresses[name] = address
    # page_directory is static and GNU ld's map does not export it.
    header = (ROOT / "include/memmap.h").read_text()
    match = re.search(r"^#define MEM_FIXED_PAGING_BASE\s+(0x[0-9a-fA-F]+)UL", header, re.M)
    if not match:
        raise ValueError("MEM_FIXED_PAGING_BASE definition missing")
    def word(address):
        return int.from_bytes(read_mem(address, WORD_BYTES), "little")
    pde0 = word(int(match.group(1), 16))
    raw = read_mem(pde0 & ~PAGE_OFFSET_MASK, PTE_COUNT * WORD_BYTES)
    result = {"pde0": pde0, "ptes": [int.from_bytes(raw[i:i + WORD_BYTES], "little")
              for i in range(0, len(raw), WORD_BYTES)],
              "vals": {name: word(address) for name, address in addresses.items()}}
    compare(result, result)
    return result


def read_mem(address, length):
    data = json.loads(emu.get("/api/mem?addr=0x%x&len=%d&space=phys" % (address, length), timeout=20))
    raw = bytes.fromhex(data["hex"])
    if len(raw) != length:
        raise ValueError("short /api/mem response")
    return raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", nargs="?", type=Path)
    parser.add_argument("--map", type=Path, default=ROOT / "build/out/kernel.map")
    parser.add_argument("--compare", nargs=2, type=Path)
    parser.add_argument("--ignore-ad", action="store_true")
    args = parser.parse_args()
    try:
        if args.compare:
            if args.out:
                parser.error("out cannot be combined with --compare")
            same = compare(*(json.loads(p.read_text()) for p in args.compare), ignore_ad=args.ignore_ad)
            print("MATCH" if same else "MISMATCH")
            return 0 if same else 1
        if not args.out or args.ignore_ad:
            parser.error("provide out.json, or --compare")
        data = snapshot(args.map, read_mem)
        args.out.write_text(json.dumps(data, indent=2) + "\n")
        print(json.dumps(data["vals"], sort_keys=True))
    except (ValueError, OSError, KeyError, emu.EmuError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
