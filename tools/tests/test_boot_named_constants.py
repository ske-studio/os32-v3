#!/usr/bin/env python3
"""Assemble boot constants and compare their values with canonical C headers.

Called by check-bootinfo-host. No emulator or built OS artifacts are needed.
NASM evaluates expressions itself; GCC checks the resulting integers against
C definitions, so expressions/aliases and C integer suffixes stay supported.
"""
from pathlib import Path
import os
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MIRRORS = {
    "memmap_defs.inc": "include/memmap.h",
    "pc98_defs.inc": "include/pc98.h",
    "gdt_defs.inc": "arch/x86/kernel/gdt.h",
    "hex_defs.inc": "lib/kstring.h",
}


def asm_values(tmp, filename, names):
    src = tmp / "constants.asm"
    out = tmp / "constants.bin"
    src.write_text(f'%include "boot/{filename}"\n' +
                   ''.join(f'dd {name}\n' for name in names))
    subprocess.run(["nasm", "-f", "bin", str(src), "-o", str(out)],
                   cwd=ROOT, check=True)
    data = out.read_bytes()
    assert len(data) == 4 * len(names), (filename, len(data))
    return struct.unpack('<' + 'I' * len(names), data)


def check_c(header, pairs):
    source = f'#include "{header}"\n' + ''.join(
        f'_Static_assert(({name}) == {value}ULL, "{name}");\n'
        for name, value in pairs)
    subprocess.run([os.environ.get("CC", "gcc"), "-std=gnu11", "-m32",
                    "-ffreestanding", "-fsyntax-only", "-x", "c",
                    "-I.", "-Iinclude", "-Iarch/x86", "-Isdk/include/os32", "-"],
                   input=source, text=True, cwd=ROOT, check=True)


def main():
    with tempfile.TemporaryDirectory(prefix="boot-named-") as directory:
        tmp = Path(directory)
        count = 0
        for filename, header in MIRRORS.items():
            names = re.findall(r'^(\w+)\s+EQU\s+',
                               (ROOT / "boot" / filename).read_text(), re.M)
            if filename == "hex_defs.inc":
                # Only these widths have a shared C canonical definition.
                names = ["BITS_PER_BYTE", "HEX_NIBBLE_BITS", "HEX32_DIGITS"]
            assert names, filename
            values = asm_values(tmp, filename, names)
            check_c(header, zip(names, values))
            count += len(names)
        ld_text = (ROOT / "boot/loader.ld").read_text()
        address = re.search(r'^MEM_LOADER_START\s*=\s*(0x[0-9a-fA-F]+);',
                            ld_text, re.M)
        assert address, "loader.ld must name its load address"
        assert re.search(r'\.\s*=\s*MEM_LOADER_START;', ld_text)
        check_c("include/memmap.h", [("MEM_LOADER_START",
                                         int(address[1], 16))])
        # Local wire-format/policy constants also get independent fixed values.
        local = {
            "realmode_defs.inc": {"RM_SEGMENT_SHIFT": 4,
                                  "RM_SEGMENT_OFFSET_MASK": 15,
                                  "RM_SEGMENT_WINDOW_BYTES": 0x10000},
            "fat12_defs.inc": {"FAT12_NAME_BYTES": 11, "FAT12_DIRENT_BYTES": 32,
                              "FAT12_DIRENT_DELETED": 0xE5,
                              "FAT12_DIRENT_CLUSTER_OFF": 26,
                              "FAT12_FIRST_CLUSTER": 2, "FAT12_EOC_MIN": 0xFF8,
                              "FAT12_CLUSTER_MASK": 0xFFF, "FAT12_ODD_SHIFT": 4,
                              "FAT12_DIRENT_SIZE_OFF": 28},
            "hex_defs.inc": {"HEX_NIBBLE_SHIFT": 2, "HEX_DIGIT_MASK": 15,
                             "HEX8_DIGITS": 2,
                             "HEX_ALPHA_BIAS": 7},
            "boot_probe.inc": {"BOOT_PROBE_STEP_KIB": 512,
                               "BOOT_PROBE_PATTERN_A": 0xAA55AA55,
                               "BOOT_PROBE_PATTERN_B": 0x55AA55AA},
        }
        local.update({
            "vk32_defs.inc": {"VK32_COMMON_SIZE": 16, "VK32_ENTRY_SIZE": 16,
                              "VK32_ENTRY_SHIFT": 4, "VK32_WORD_BYTES": 4,
                              "VK32_TRAILER_BYTES": 8, "VK32_OFF_HEADER_SIZE": 4,
                              "VK32_OFF_VERSION": 8, "VK32_OFF_ENTRY_COUNT": 12,
                              "VK32_ENTRY_OFF_RAW_SIZE": 4,
                              "VK32_ENTRY_OFF_DATA_OFFSET": 8,
                              "VK32_ENTRY_OFF_COMPRESSED_SIZE": 12},
            "lz4_defs.inc": {"LZ4_TOKEN_BITS": 4, "LZ4_LENGTH_MASK": 15,
                             "LZ4_OFFSET_BYTES": 2, "BOOT_LZ4_ERR_OUTPUT": 0xFFFFFFFE},
            "crc32_defs.inc": {"CRC32_INIT": 0xFFFFFFFF,
                               "CRC32_NIBBLE_BITS": 4, "CRC32_NIBBLE_MASK": 15,
                               "CRC32_TABLE_WORD_BYTES": 4},
        })
        assert 1 << local["vk32_defs.inc"]["VK32_ENTRY_SHIFT"] == local["vk32_defs.inc"]["VK32_ENTRY_SIZE"]
        # The wire header/entry sizes already have C canonical definitions.
        names = ["VK32_COMMON_SIZE", "VK32_ENTRY_SIZE"]
        check_c("boot/boot_defs.h", zip(names, asm_values(tmp, "vk32_defs.inc", names)))
        check_c("include/bootinfo.h", [("BOOTINFO_DA_HD1", 0x81)])
        # Compile the CRC canonical definition with the types it requires.
        source = ('typedef unsigned char u8; typedef unsigned int u32;\n'
                  '#include "lib/crc32_core.inc"\n'
                  '_Static_assert(CRC32_INIT == 0xFFFFFFFFUL, "CRC32_INIT");\n')
        subprocess.run([os.environ.get("CC", "gcc"), "-std=gnu11", "-m32",
                        "-fsyntax-only", "-x", "c", "-I.", "-"],
                       input=source, text=True, cwd=ROOT, check=True)
        # Evaluate the actual frame definitions used by the extracted code,
        # independently pinning each cdecl slot and allocation size.
        loader = (ROOT / "boot/loader_fat_new.asm").read_text()
        frames = loader.split(";; >>> FRAME_DEFS_BEGIN", 1)[1].split(";; <<< FRAME_DEFS_END", 1)[0]
        expected_frames = {
            "VK_ARG_FILE": 8, "VK_ARG_SIZE": 12, "VK_ARG_WINDOW": 16,
            "VK_ARG_OUT_CRC": 20, "VK_FRAME_COUNT": -4,
            "VK_FRAME_HEADER_SIZE": -8, "VK_FRAME_CRC_OFF": -12,
            "VK_FRAME_INDEX": -16, "VK_FRAME_DST": -20, "VK_FRAME_RAW_SIZE": -24,
            "VK_FRAME_BYTES": 24, "VK_SAVED_BYTES": 12,
            "CRC_ARG_STATE": 8, "CRC_ARG_DATA": 12, "CRC_ARG_SIZE": 16,
            "LZ4_ARG_SRC": 8, "LZ4_ARG_SIZE": 12, "LZ4_ARG_DST": 16,
            "LZ4_ARG_CAP": 20, "LZ4_SAVED_BYTES": 20,
            "LZ4_FRAME_TOKEN": -24, "LZ4_FRAME_OFFSET": -28, "LZ4_FRAME_BYTES": 8,
        }
        src = tmp / "frames.asm"
        out = tmp / "frames.bin"
        src.write_text(frames + ''.join(f'dd {name}\n' for name in expected_frames))
        subprocess.run(["nasm", "-f", "bin", str(src), "-o", str(out)], check=True)
        assert struct.unpack('<' + 'i' * len(expected_frames), out.read_bytes()) == tuple(expected_frames.values())
        assert re.search(r'^BOOTINFO_DA_HD1 EQU 0x81$', loader, re.M)
        for filename, expected in local.items():
            actual = dict(zip(expected, asm_values(tmp, filename, expected)))
            assert actual == expected, (filename, actual, expected)
        print(f"BOOT named constants PASS: {count} C mirrors + linker + local formats")


if __name__ == "__main__":
    main()
