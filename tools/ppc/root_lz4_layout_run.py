#!/usr/bin/env python3
"""PPC_BOOT_DESIGN §§2,6–7 and root-LZ4 stage 1. Boot from CD via OF,
compare memory before boot and at the final guest query, then observe reset.
Apple's Power Mac G4 Developer Note (January 2002), pp. 55, 57, allows
one DIMM and lists 32 MB in Table 4-1. Test it and the existing 128 MiB
mac99 configuration independently. Never boot into a known collision.
Candidate ranges [0x580000,0x5d3000) and [0xd00000,0xd53000) leave room
after the shell and before the 15 MiB window end. NetBSD ofwboot links
at 0xe00000, OpenBSD at 0x20000 (PPC_BOOT_DESIGN §2). Apple TN2001 lists
load-base 0x800000 and real-base -1; the latter delegates placement to OF.
The actual staging extent and firmware reservations are queried before boot.
mac99 accepts 32 MiB. PPC OpenBIOS defaults to load-base 64 MiB, real-base
-1 and identity-maps an 8 MiB staging area. Only the 32 MiB emulator case
requests staging at 16 MiB, leaving other aligned slots for the HPT.
https://developer.apple.com/library/archive/technotes/tn/tn2001.html
https://github.com/NetBSD/src/blob/trunk/sys/arch/macppc/stand/ofwboot/Makefile
https://github.com/openbios/openbios/blob/master/forth/admin/nvram.fs
https://github.com/openbios/openbios/blob/master/arch/ppc/qemu/init.c
https://github.com/qemu/qemu/blob/master/hw/ppc/mac_newworld.c
https://www.manualzz.com/doc/36657785/apple-power-mac-g4-computer-developer-note
"""
import argparse
import json
import os
from pathlib import Path
import re
import select
import struct
import subprocess
import time
import zlib

from lan_probe import PROMPT, ROM, cells as property_cells, properties
from root_lz4_inventory import PAGE, ROOT, envelope, segments

LOW = 0x100000
WINDOW = 0xf00000
HPT = 0x20000


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def build(out, sizes, candidate):
    cross = Path(os.environ['PPC_CROSS_DIR'])/'bin'
    end, frame = sizes['resident_end'], sizes['frame']
    require(LOW < end < sizes['chain_end'] <= WINDOW and end % PAGE == frame % PAGE == 0
            and frame > 0, 'invalid measured envelope')
    require(candidate % PAGE == 0 and max(0x500000, sizes['chain_end']) <= candidate
            and candidate + frame <= WINDOW, 'candidate outside page-aligned pool')
    # A distinct big-endian word at every offset in each patterned region.
    def pattern(base, size):
        return b''.join(struct.pack('>I', base+i) for i in range(0, size, 4))
    (out/'high.bin').write_bytes(pattern(candidate, frame))
    asm = out/'entry.S'
    asm.write_text('''.section .root,"a"
.incbin "high.bin"
.section .note.GNU-stack,"",@progbits
''')
    linker = out/'probe.ld'
    linker.write_text(f'''ENTRY(ppc_kernel_entry)
PHDRS {{ low PT_LOAD FLAGS(7); high PT_LOAD FLAGS(4); note PT_NOTE FLAGS(4); }}
SECTIONS {{
 . = {LOW};
 .text : {{ *(.text.entry) *(.text*) }} :low
 .rodata : {{ *(.rodata*) }} :low
 .note.ofw : {{ KEEP(*(.note.ofw)) }} :low :note
 .data : {{ *(.data*) *(.sdata*) }} :low
 .pattern ALIGN(4) : {{
    low_pattern_start = .;
    . = ABSOLUTE({LOW + sizes['low_filesz'] - 1}); BYTE(0)
 }} :low
 low_file_end = .;
 .bss (NOLOAD) : {{
    __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) __bss_end = .;
    . = ALIGN(16); stack_floor = .;
    . = ABSOLUTE({end}); low_end = .; __kernel_stack_top = .;
 }} :low
 ASSERT(low_end - stack_floor >= 32768, "no diagnostic stack")
 . = {candidate};
 .root : {{ high_start = .; KEEP(*(.root)) high_end = .; }} :high
 /DISCARD/ : {{ *(.comment .eh_frame .note.GNU-stack .note.gnu.build-id) }}
}}
''')
    cd = out/'cd'
    cd.mkdir(exist_ok=True)
    # Use the same generated memory bounds as arch/ppc/kernel.ld.
    layout = out/'memmap_layout.h'
    subprocess.run(['python3', '-B', ROOT/'tools/gen_memmap.py', '--layout-header', layout],
                   cwd=ROOT, check=True)
    limits = {name: int(value, 0) for name, value in
              re.findall(r'^#define (LAYOUT_\w+) (0x[0-9a-fA-F]+)$', layout.read_text(), re.M)}
    require(LOW == limits['LAYOUT_KERNEL_LOAD_ADDR'] and
            end-LOW <= limits['LAYOUT_MEM_KERNEL_IMAGE_MAX'] and
            sizes['chain_end'] <= limits['LAYOUT_MEM_FIXED_PAGING_GUARD_BASE'],
            'FAIL measured low LOAD exceeds the product budget; return to stage 1 §5 for review')
    elf = cd/'LAYOUT.ELF'
    subprocess.run([cross/'powerpc-elf-gcc', '-mcpu=7450', '-m32', '-mbig-endian',
                    '-msdata=none', '-DPPC_MMU', '-ffreestanding', '-fno-pie', '-nostdlib', '-O2',
                    '-D__cdecl=', '-fno-stack-protector', '-fno-builtin', '-ffunction-sections',
                    '-Wl,--gc-sections,-z,noexecstack,-z,max-page-size=4096', '-Wl,-T,'+str(linker),
                    *['-I'+str(ROOT/p) for p in ('include','lib','arch/ppc','platform/powermac')],
                    asm, ROOT/'arch/ppc/entry.S', ROOT/'arch/ppc/kernel_entry.S',
                    ROOT/'lib/kstring_c.c', ROOT/'arch/ppc/ofw_note.S', ROOT/'tools/ppc/root_lz4_probe.c',
                    ROOT/'platform/powermac/ofw.c', '-o', elf], cwd=out, check=True)
    subprocess.run([cross/'powerpc-elf-readelf', '-lW', elf], check=True)
    data = bytearray(elf.read_bytes())
    ph = segments(data)
    loads = [p for p in ph if p[0] == 1]
    require(len(loads) == 2 and sum(p[0] == 4 for p in ph) == 1, 'LOAD/NOTE count')
    lo, hi = loads
    # Patch only the reserved file-backed pattern section, before mastering
    # the CD. Byte identity in code, note and payload is checked by one CRC.
    symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output(
        [cross/'powerpc-elf-nm',elf], text=True).splitlines() if len(f := line.split()) == 3}
    start = symbols['low_pattern_start']
    size = symbols['low_file_end'] - start
    require(lo[4] == sizes['low_filesz'] and LOW <= start < LOW + lo[4]
            and start + size == LOW + lo[4], 'low file dimensions')
    off = lo[1] + start - LOW
    data[off:off+size] = pattern(start, size)[:size]
    elf.write_bytes(data)
    for p, va, mem, flags in ((lo, LOW, end-LOW, 7), (hi, candidate, frame, 4)):
        _, off, v, pa, f, m, fl, align = p
        require((v, pa, m, fl) == (va, va, mem, flags) and 0 < f <= m
                and align == PAGE and (off-v) % align == 0 and off+f <= len(data),
                'LOAD dimensions')
    require(struct.unpack_from('>I', data, 24)[0] == LOW and lo[2]+lo[5] <= hi[2]
            and 0 <= hi[1]-lo[1]-lo[4] < PAGE and hi[4] == frame
            and data[hi[1]:hi[1]+frame] == (out/'high.bin').read_bytes(), 'LOAD packing')
    subprocess.run(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop',
                    '-o', out/'layout.iso', cd], check=True)
    return data, loads


def covered(ranges, first, end):
    for start, stop in sorted(ranges):
        if start > first:
            break
        first = max(first, stop)
        if first >= end:
            return True
    return False


def overlap(ranges, first, end):
    return any(first < b and a < end for a, b in ranges)


def check_before(before, loads, length):
    stage = before['load-base']
    for p in loads:
        start, end = p[3], p[3]+p[5]
        require(covered(before['reg'], start, end) and covered(before['available'], start, end),
                f'pre-existing reservation or non-RAM at {start:#x}')
        require(not overlap([(stage, stage+length)], start, end), 'OF file copy intersects LOAD')
    require(covered(before['reg'], stage, stage+length), 'OF file copy exceeds RAM')


def verify(before, after, data, loads):
    check_before(before, loads, len(data))
    require(after['ENTRY'][0][0] == before['entry'] == LOW and after['ENTRY'][0][1] and
            after['MMU'][0][0] == before['mmu'] and
            after['LOAD-BASE'] == [(before['load-base'], 0)] and
            after['REAL-BASE'] == [(before['real-base'], 0)], 'entry or OF environment')
    ram = intervals(after['RAM'])
    avail = intervals(after['AVAILABLE'])
    require(ram == before['reg'], 'RAM changed')
    for p in loads:
        _, off, va, pa, size, mem, _, _ = p
        require(not overlap(avail, pa, pa+mem), f'LOAD still available {pa:#x}')
        require((va, va+mem) in after['TRANSLATED'], f'incomplete translate {va:#x}')
        require((va, zlib.crc32(data[off:off+size])) in after['CRC'], f'file bytes {va:#x}')
    end = loads[0][3]+loads[0][5]
    # Only the compressed frame can return to the pool after expansion.
    # KHEAP_BASE starts at the resident end, even when OF claims slack.
    high = loads[1][3]
    require(covered(ram, end, WINDOW) and covered(avail, end, high)
            and covered(avail, high + loads[1][5], WINDOW), 'heap/chain/15 MiB window')
    require(any(covered(avail, a, a+HPT) and covered(ram, a, a+HPT)
                for a in range(0x1000000, 0x2000000, HPT)), 'no aligned HPT')
    require(after['LAYOUT-END'] == [(1, 0)], 'guest did not finish')


def intervals(records):
    result = sorted((a, a+n) for a, n in records)
    require(result and all(a < b <= 1 << 32 for a,b in result) and
            all(x[1] <= y[0] for x,y in zip(result, result[1:])), 'malformed memory map')
    return result


def capture(out, cpu, ram_mib, data, loads):
    cmd = ['qemu-system-ppc', '-M','mac99','-cpu',cpu,'-m',f'{ram_mib}M','-accel','tcg',
           '-bios',ROM,'-cdrom',out/'layout.iso','-nographic','-prom-env','auto-boot?=false',
           '-serial','stdio','-monitor','none','-nic','none','-no-reboot']
    if ram_mib == 32:
        cmd.extend(['-prom-env', 'load-base=1000000'])
    with (out/f'{cpu}-{ram_mib}M.log').open('wb') as log, subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            bufsize=0) as proc:
        def response():
            raw = bytearray()
            deadline = time.monotonic()+30
            while time.monotonic() < deadline:
                if not select.select([proc.stdout], [], [], 1)[0]:
                    continue
                piece = os.read(proc.stdout.fileno(), 65536)
                require(piece, 'OF closed serial')
                raw.extend(piece)
                log.write(piece)
                if PROMPT.search(raw):
                    return raw.decode(errors='replace')
            raise TimeoutError('OpenBIOS prompt')
        def send(command):
            proc.stdin.write((command+'\r').encode())
        def query(command):
            send(command)
            return response()
        def values(command):
            reply = query(command)
            match = re.search(r'(?m)^OBS +([0-9a-fA-F ]+)[\r\n]', reply)
            require(match, 'missing OF observation '+command)
            return [int(x,16) for x in match[1].split()]
        try:
            response()
            query('hex')
            before = {key: values(f'cr ." OBS " {key} u. cr')[0] for key in ('load-base', 'real-base')}
            root = properties(query('dev / .properties'))
            chosen = properties(query('dev /chosen .properties'))
            memory = properties(query('dev /memory .properties'))
            before['mmu'], = property_cells(chosen['mmu'])
            ac, = property_cells(root['#address-cells'])
            sc, = property_cells(root['#size-cells'])
            require(ac in (1,2) and sc in (1,2), 'memory cell widths')
            for key in ('reg','available'):
                cells = property_cells(memory[key])
                stride = ac+sc
                require(len(cells)%stride == 0, 'truncated memory map')
                def number(parts):
                    return int.from_bytes(b''.join(struct.pack('>I', v) for v in parts), 'big')
                before[key] = intervals([(number(cells[i:i+ac]),number(cells[i+ac:i+stride]))
                                         for i in range(0,len(cells),stride)])
            entry, = struct.unpack_from('>I', data, 24)
            before['entry'] = entry
            (out/f'{cpu}-{ram_mib}M-before.json').write_text(json.dumps(before, indent=2))
            if ram_mib == 32:
                require(before['load-base'] == 0x1000000, '32 MiB staging base not applied')
            check_before(before, loads, len(data))
            send(r'boot cd:,\LAYOUT.ELF')
            try:
                tail, _ = proc.communicate(timeout=90)
            except subprocess.TimeoutExpired:
                proc.kill()
                tail, _ = proc.communicate()
                log.write(tail)
                raise
            log.write(tail)
            require(proc.returncode == 0, 'reset exit status')
            after = {}
            for name,a,b in re.findall(rb'(?m)^([A-Z-]+) ([0-9a-f]{8}) ([0-9a-f]{8})\r*$',tail):
                after.setdefault(name.decode(),[]).append((int(a,16),int(b,16)))
            (out/f'{cpu}-{ram_mib}M-after.json').write_text(json.dumps(after, indent=2))
            verify(before, after, data, loads)
        finally:
            if proc.poll() is None:
                proc.kill()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--inventory', type=Path, required=True)
    ap.add_argument('--candidate', type=lambda value: int(value, 0), required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--build-only', action='store_true')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rows = json.loads(args.inventory.read_text())
    sizes = envelope(rows)
    sizes['resident_end'] = LOW + max(row['low_memsz'] for row in rows)
    sizes['low_filesz'] = max(row['low_filesz'] for row in rows)
    os.environ.setdefault('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')
    data, loads = build(out, sizes, args.candidate)
    if args.build_only:
        print('BUILT', out/'cd/LAYOUT.ELF')
        return
    failures = []
    for ram_mib in (128, 32):
        for cpu in ('7450', '7455'):
            try:
                capture(out, cpu, ram_mib, data, loads)
            except (ValueError, KeyError, IndexError, OSError, subprocess.SubprocessError) as error:
                failures.append(f'{cpu}/{ram_mib} MiB: {error}')
    if failures:
        raise SystemExit('FAIL ' + ' | '.join(failures) + '; return to stage 1 §5 for review')
    print(f'PASS mac99 low={LOW:#x}+{loads[0][5]:#x} high={args.candidate:#x}+{loads[1][5]:#x}')


if __name__ == '__main__':
    main()
