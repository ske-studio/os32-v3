#!/usr/bin/env python3
"""Generate the E10-6 IPL probe: v86 -b /host/test/int80.img.

RAW PC-98 2HD 1232KB is recognized by drivers/loop_dev.c (77/2/8, 1024B sectors).
v86_boot2 loads C0/H0/S1 at 1FC0:0000. Install our own IVT[80h] ISR,
enable IF before INT80, save the ISR's FLAGS at 1FC0:0040, then IRET/HLT.
Expected IF is zero for v86 -b without -g. Break at v86_exit_to_kernel
after HLT (before V86 teardown), then read:
emu_read_mem space=linear at flags_guest_phys, or space=phys at flags_phys.
IPL RAM is remapped: --backing-phys must be the live session's
v86_session.backing_phys to emit an actual emulator physical address.
This is a boot image, not a DOS COM file or input code for v86 -d.
"""
import argparse
import json
from pathlib import Path
import struct

CYLINDERS, HEADS, SECTORS, SECTOR_BYTES = 77, 2, 8, 1024
IPL_SEG = 0x1FC0
INT_VECTOR = 0x80
ISR_OFFSET = 0x20
FLAGS_OFFSET = 0x40
FLAGS_INITIAL = 0xFFFF  # Distinguish an unexecuted ISR from captured IF=0.
IF_MASK = 1 << 9
REMAP_START = 0x1000  # v86_mem.c: backing starts at guest page 1.


def ipl_bytes():
    code = (bytes.fromhex("fa 31 c0 8e d8 c7 06")  # CLI; XOR AX,AX; MOV DS,AX
            + struct.pack('<HH', INT_VECTOR * 4, ISR_OFFSET)
            + bytes.fromhex("8c c8 a3")  # MOV AX,CS; MOV [IVT[80h].seg],AX
            + struct.pack('<H', INT_VECTOR * 4 + 2)
            + bytes.fromhex("fb cd 80 f4"))  # STI; INT 80h; HLT
    isr = (bytes.fromhex("9c 58 2e a3")  # PUSHF; POP AX; MOV [CS:flags],AX
           + struct.pack('<H', FLAGS_OFFSET) + b'\xcf')  # IRET
    ipl = bytearray(FLAGS_OFFSET + 2)
    ipl[:len(code)] = code
    ipl[ISR_OFFSET:ISR_OFFSET + len(isr)] = isr
    struct.pack_into('<H', ipl, FLAGS_OFFSET, FLAGS_INITIAL)
    return bytes(ipl)


IPL = ipl_bytes()


def observation(backing_phys=None):
    guest_phys = IPL_SEG * 16 + FLAGS_OFFSET
    offset = guest_phys - REMAP_START
    return {
        'flags_segment': IPL_SEG, 'flags_offset': FLAGS_OFFSET,
        'flags_guest_phys': guest_phys, 'flags_bytes': 2,
        'flags_initial': FLAGS_INITIAL, 'if_mask': IF_MASK, 'expected_if': 0,
        'backing_offset': offset, 'backing_phys': backing_phys,
        'flags_phys': None if backing_phys is None else backing_phys + offset,
        'read_before_teardown': True,
    }


def image_bytes():
    image = bytearray(CYLINDERS * HEADS * SECTORS * SECTOR_BYTES)
    image[:len(IPL)] = IPL
    return bytes(image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("build/out/int80.img"))
    parser.add_argument("--backing-phys", type=lambda value: int(value, 0),
                        help="live v86_session.backing_phys (e.g. 0x300000)")
    args = parser.parse_args()
    if args.backing_phys is not None and (args.backing_phys <= 0 or args.backing_phys % 4096):
        parser.error('--backing-phys must be a positive page-aligned address')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image_bytes())
    metadata = observation(args.backing_phys)
    args.out.with_suffix('.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
    print(f"{args.out}: {args.out.stat().st_size} bytes (RAW 77/2/8/1024)")
    print(f"FLAGS guest physical/linear=0x{metadata['flags_guest_phys']:05x}, "
          f"2 bytes LE, IF mask=0x{IF_MASK:04x}, expected IF=0 (without -g)")
    if metadata['flags_phys'] is not None:
        print(f"FLAGS emulator physical=0x{metadata['flags_phys']:x} (read before teardown)")
    else:
        print(f"FLAGS emulator physical=v86_session.backing_phys+0x{metadata['backing_offset']:x}; "
              "use --backing-phys or emu_read_mem space=linear before teardown")


if __name__ == "__main__":
    main()
