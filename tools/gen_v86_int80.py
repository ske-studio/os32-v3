#!/usr/bin/env python3
"""Generate the E10-6 IPL probe: v86 -b /host/test/int80.img.

RAW PC-98 2HD 1232KB is recognized by drivers/loop_dev.c (77/2/8, 1024B sectors).
v86_boot2 loads C0/H0/S1 at 1FC0:0000. INT80 uses the inherited guest IVT;
PM observes the reflected frame's VM/IF bits. HLT exits if that ISR returns.
This is a boot image, not a DOS COM file or input code for v86 -d.
"""
import argparse
from pathlib import Path

CYLINDERS, HEADS, SECTORS, SECTOR_BYTES = 77, 2, 8, 1024
IPL = bytes.fromhex("cd 80 f4")  # INT 80h; HLT (V86_EXIT_HLT)


def image_bytes():
    image = bytearray(CYLINDERS * HEADS * SECTORS * SECTOR_BYTES)
    image[:len(IPL)] = IPL
    return bytes(image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("build/out/int80.img"))
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image_bytes())
    print(f"{args.out}: {args.out.stat().st_size} bytes (RAW 77/2/8/1024)")


if __name__ == "__main__":
    main()
