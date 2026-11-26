"""Read instructions at objdump boundaries in the selected distribution images."""
from pathlib import Path
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "sdk")]
from tests.support.media import Failure, main, make
from tests.support.product import commands, recipe_lines
from os32x_hdr import (parse_header, OS32X_MAGIC, OS32X_APP_LOAD_ADDR, OS32X_SHELL_LOAD_ADDR,
                       OS32X_FLAG_SHLIB, OS32X_FLAG_RING3)

# The 386 two-byte opcode map. Group 7 /7 (INVLPG) was added by the 486.
EXTENDED = {0, 1, 2, 3, 6, 0x20, 0x21, 0x22, 0x23, 0x24, 0x26,
            *range(0x80, 0xa0), 0xa0, 0xa1, 0xa3, 0xa4, 0xa5, 0xa8, 0xa9,
            0xab, 0xac, 0xad, 0xaf, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
            0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf}
PREFIX = {0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0xf0, 0xf2, 0xf3}
FAULT = {"userland/diag/faulttest.elf", "userland/diag/h3a.elf", "userland/diag/h3b.elf"}


def extended_386(code, mnemonic):
    if len(code) < 2:
        return False
    second = code[1]
    if second == 0x0b:
        return mnemonic == "ud2"
    if second not in EXTENDED:
        return False
    # Later CPUs reused previously invalid register forms in these groups.
    if second in (0, 1):
        if len(code) < 3:
            return False
        group = code[2] >> 3 & 7
        register = code[2] >> 6 == 3
        if second == 0:
            return group < 6
        return group in (4, 6) or group < 4 and not register
    if second in (0x20, 0x22):
        return len(code) >= 3 and (code[2] >> 3 & 7) in (0, 2, 3)
    if second in (0x24, 0x26):
        return len(code) >= 3 and (code[2] >> 3 & 7) in (6, 7)
    return True


def issues(code, asm, ring3, integer):
    while len(code) > 1 and code[0] in PREFIX:
        code = code[1:]
    op = code[0]
    words = asm.split()
    if ring3 and (op in {0xf4, 0xfa, 0xfb, *range(0xe4, 0xe8), *range(0xec, 0xf0), *range(0x6c, 0x70)}
                  or re.search(r'\b(lgdt|lidt|ltr|lldt|clts|lmsw|invlpg|wbinvd|invd|rdmsr|wrmsr)\b|%(cr|db|dr|tr)\d', asm)):
        yield "privileged"
    if (op == 0x0f and not extended_386(code, words[0])
            or op in (0xc4, 0xc5, 0x62) and len(code) >= 2 and code[1] >> 6 == 3
            or op == 0x8f and len(code) >= 2 and (code[1] >> 3 & 7) != 0
            or "(bad)" in asm or words[0] in (".byte", "pause", "invlpg", "popcnt", "lzcnt", "tzcnt", "xbegin", "xabort", "salc", "int1")
            or words[0].startswith(("fcmov", "fcomi", "fucomi", "fisttp", "v"))
            or integer and (0xd8 <= op <= 0xdf or op == 0x9b)):
        yield "isa386"


def packages(config, links):
    raw_sources, result = {}, {}
    for line in recipe_lines(config, ("programs",)):
        copy = re.search(r'\b[\w/-]*objcopy -O binary (\S+) (\S+)', line)
        if copy:
            raw_sources[copy[2]] = copy[1]
        pack = re.search(r'\bpython3 (?:sdk/mkos32x|tools/mkshlib)\.py (\S+) (\S+) --elf (\S+)', line)
        if pack:
            raw, image, elf = pack.groups()
            if raw_sources.get(raw) != elf or elf not in links:
                raise OSError(f"{image}: cannot trace selected ELF through {raw}")
            result.setdefault(elf, []).append(Path(image))
    if links.keys() != result.keys():
        raise OSError(f"unpackaged links: {sorted(links.keys() - result.keys())}")
    return result


def verify(config):
    links = {}
    for output, args in commands(config, ("programs",)):
        if not output.endswith(".elf") or "-T" not in args:
            continue
        script = Path(args[args.index("-T")+1]).name
        if script in ("app.ld", "app_sys.ld", "shlib.ld"):
            links[output] = script
    if set(links.values()) != {"app.ld", "app_sys.ld", "shlib.ld"}:
        raise OSError("incomplete program link roles")
    # Re-link source mutations through the product rules.
    make("userland/diag/ring3_hello.bin", "userland/diag/faulttest.bin")
    dump = Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-objdump"
    selected = packages(config, links)
    errors = []
    for elf, script in sorted(links.items()):
        ring3 = script != "app_sys.ld"
        for image in selected[elf]:
            header = parse_header(image.read_bytes())
            expected_addr = {"app.ld": OS32X_APP_LOAD_ADDR, "app_sys.ld": OS32X_SHELL_LOAD_ADDR}.get(script)
            if header["magic"] != OS32X_MAGIC or expected_addr is not None and header.get("load_addr") != expected_addr:
                raise OSError(f"{elf}: wrong magic or load address")
            if bool(header["flags"] & OS32X_FLAG_SHLIB) != (script == "shlib.ld") or not ring3 and header["flags"] & OS32X_FLAG_RING3:
                raise OSError(f"{elf}: inconsistent packaging")
        proc = subprocess.run([dump, "-dw", elf], text=True, capture_output=True)
        if proc.returncode:
            raise OSError(proc.stderr)
        symbol = ""
        fault_address = None
        count = faults = 0
        for line in proc.stdout.splitlines():
            label = re.match(r'^\w+ <(.+)>:$', line)
            if label:
                symbol = label[1]
                if symbol == "os32_gp_injection":
                    fault_address = line.split()[0].lstrip("0")
            fields = line.strip().split("\t")
            if len(fields) < 3 or not fields[0].endswith(":"):
                continue
            count += 1
            asm = fields[2].strip()
            for rule in issues(bytes.fromhex(fields[1]), asm, ring3, Path(elf).stem == "libos32snd"):
                if rule == "privileged" and asm == "hlt" and elf in FAULT and fields[0][:-1] == fault_address:
                    faults += 1
                    if faults == 1:
                        continue
                errors.append(f"{elf}: {rule} {asm} at {fields[0]} <{symbol}>")
        if elf in FAULT and faults != 1:
            errors.append(f"{elf}: missing GP injection hlt")
        if not count:
            raise OSError(f"{elf}: empty disassembly")
    if errors:
        raise Failure("\n".join(errors))


if __name__ == "__main__":
    main(lambda config: make("programs"), verify)
