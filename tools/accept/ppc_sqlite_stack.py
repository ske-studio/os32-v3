#!/usr/bin/env python3
"""期待の出所: PPC_SQLITE_STACK_DESIGN §6 / PPC_FP_NEWLIB_DESIGN §8 S1."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ppc'))
from kboot_vectors import CROSS, FLAGS
from run import QMP, ROM, ROOT, command, require

SOURCES = (
    'arch/ppc/entry.S', 'arch/ppc/ofw_note.S', 'platform/powermac/ofw.c',
    'platform/powermac/escc_tree.c', 'platform/powermac/escc.c', 'lib/kstring_c.c',
    'kernel/sqlite_engine.c', 'arch/ppc/sqlite_stack_ppc.S', 'arch/ppc/kernel_fp.c',
    'arch/ppc/fp_context.S', 'arch/ppc/mmu.S', 'arch/ppc/cache_copy.S',
    'tools/tests/ppc_sqlite_stack_guest.c', 'tools/tests/ppc_sqlite_stack_regs.S',
)
ENGINE_INC = [
    '-I' + str(ROOT / d) for d in (
        'kernel', 'init', 'kernel/console', 'kernel/irq', 'kernel/time', 'mm',
        'sdk/include', 'sdk/include/os32', 'lib/sqlite3', 'fs',
    )
] + ['-D__KERNEL_BUILD__']
CC = str(CROSS) + 'gcc'
BUILD_FLAGS = [*FLAGS, '-ffunction-sections', '-fdata-sections']
MUTATIONS = (
    ('bad-sp', 'arch/ppc/sqlite_stack_ppc.S', '    blr\n.size sqlite_call_on_alt_stack,',
     '    addi 1,1,16\n    blr\n.size sqlite_call_on_alt_stack,'),
    ('bad-fp', 'kernel/sqlite_engine.c', '    arch_kernel_fp_end(&guard);\n', ''),
    ('bad-ee', 'kernel/sqlite_engine.c',
     '    sqlite_engine_active = 1;\n    irq_restore(flags);\n',
     '    sqlite_engine_active = 1;\n'),
)
TYPE_MUTATIONS = (
    ('arch_kernel_fp_begin', 'int arch_kernel_fp_begin(', 'long arch_kernel_fp_begin('),
    ('arch_kernel_fp_end', 'arch_kernel_fp_end(const struct', 'arch_kernel_fp_end(struct'),
)
VERDICTS = {
    'normal': ('sqlite_s1_done', 0, 'S1-PASS'),
    'bad-sp': ('sqlite_s1_bad_sp', 1, 'S1-FAIL-SP'),
    'bad-fp': ('sqlite_s1_bad_fp', 2, 'S1-FAIL-FP'),
    'bad-ee': ('sqlite_s1_fail', 3, 'S1-FAIL-CALLBACK-EE'),
}
LINKER = '''OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(7); note PT_NOTE FLAGS(4); }
SECTIONS {
 . = 0x20000;
 .text : { *(.text .text.*) } :image
 .rodata : { *(.rodata .rodata.*) } :image
 .note.ofw : { KEEP(*(.note.ofw)) } :image :note
 .data : { *(.data .data.*) *(.sdata*) } :image
 . = ALIGN(4); _edata = .;
 .bss (NOLOAD) : {
  *(.bss .bss.*) *(.sbss*) *(COMMON)
  . = ALIGN(16); s1_low = .; . += 16;
  s1_alt = .; . += 32768; s1_high = .; . += 16;
  s1_old = .; . += 16384; __stack_top = .; _end = .;
 } :image
 /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note.GNU-stack) }
 ASSERT(_end < 0x400000, "fixture below 4MiB")
}'''


def compile_object(out, name, source, extra_flags=()):
    obj = out / (Path(name).stem + '.o')
    flags = []
    if name == 'kernel/sqlite_engine.c':
        flags = ENGINE_INC
    elif name == 'tools/tests/ppc_sqlite_stack_guest.c':
        flags = ['-I' + str(ROOT / 'kernel')]
    command([CC, *extra_flags, *BUILD_FLAGS, *flags, '-c', source, '-o', obj])
    return obj


def check_type_mutations(work):
    engine = 'kernel/sqlite_engine.c'
    header = ROOT / 'arch/ppc/arch_kernel_fp.h'
    original = header.read_text()
    for function, anchor, replacement in TYPE_MUTATIONS:
        out = work / (function + '-type')
        out.mkdir()
        require(original.count(anchor) == 1, f'{function}: mutation anchor must be unique')
        (out / header.name).write_text(original.replace(anchor, replacement))
        try:
            compile_object(out, engine, ROOT / engine, ('-I' + str(out),))
        except RuntimeError as error:
            diagnostic = str(error)
            (out / 'compile.log').write_text(diagnostic + '\n')
            require(f'error: static assertion failed: "{function} signature"' in diagnostic,
                    f'{function}: unexpected compiler failure, evidence {out}')
            print(f'RED {function}: signature assertion', flush=True)
        else:
            require(False, f'{function}: signature mutation compiled')
    out = work / 'type-control'
    out.mkdir()
    compile_object(out, engine, ROOT / engine)
    print('GREEN type control: product engine compiled', flush=True)


def link_image(out, objects):
    linker, mapfile, elf = (out / name for name in ('fixture.ld', 'fixture.map', 'SSTACK.ELF'))
    linker.write_text(LINKER)
    command([CC, *BUILD_FLAGS, '-no-pie', '-Wl,-N,--gc-sections,--no-undefined',
             '-Wl,-T,' + str(linker), '-Wl,-Map,' + str(mapfile), *objects.values(), '-o', elf])
    nm = command([str(CROSS) + 'nm', '-n', elf])
    (out / 'symbols.log').write_text(nm)
    sym = {fields[2]: int(fields[0], 16) for line in nm.splitlines()
           if len(fields := line.split()) == 3}
    live = mapfile.read_text().split('Linker script and memory map', 1)[1]
    require(re.search(r'\.text\.sqlite_engine_run\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+\S*/sqlite_engine\.o',
                      live) and 'sqlite_engine_run' in sym, 'missing linked product engine')
    low, alt, high, old, top = (sym[k] for k in ('s1_low', 's1_alt', 's1_high', 's1_old', '__stack_top'))
    require(low % 16 == 0 and sym['_edata'] <= low and
            (alt-low, high-alt, old-high, top-old) == (16, 32768, 16, 16384) and
            top == sym['_end'] < 0x400000, 'fixture stack bounds')
    providers = r'os32_sqlite_|sqlite3|sqlite_module_|module_(?:load|start)\b|(?:ime|fep|dictionary|dict)_|services_boot\b'
    require(not re.search(r'^ *0x[0-9a-f]+ +(?:' + providers + ')', live, re.M)
            and not any(re.match(providers, name) for name in sym),
            'unexpected SQLite module/dictionary/IME provider')
    cd = out / 'cd'
    cd.mkdir()
    shutil.copyfile(elf, cd / elf.name)
    iso = out / 'fixture.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    return iso, sym


def observe(out, image, cpu, case):
    iso, sym = image
    out.mkdir()
    serial_a, serial_b = (out / name for name in ('serial-a.log', 'serial-b.log'))
    args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-accel', 'tcg',
            '-bios', ROM, '-cdrom', iso, '-boot', 'd', '-prom-env', r'boot-device=cd:,\SSTACK.ELF',
            '-prom-env', 'auto-boot?=true', '-nographic', '-serial', 'file:' + str(serial_a),
            '-serial', 'file:' + str(serial_b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot']
    stop, status, diagnostic = VERDICTS[case]
    terminals = {sym[name] for name, _, _ in VERDICTS.values()} | {sym['sqlite_s1_fail']}
    deadline = time.monotonic() + 30
    with (out / 'qemu.stderr').open('w') as err, (out / 'qmp.log').open('w') as log:
        proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err)
        try:
            qmp = QMP(proc, deadline)
            last = None
            while time.monotonic() < deadline:
                qmp.call('stop')
                regs = qmp.call('human-monitor-command', {'command-line': 'info registers'})
                match = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                pc = int(match[1], 16) if match else None
                print(f'NIP {pc:#x}' if pc is not None else regs, file=log, flush=True)
                if pc in terminals and pc == last:
                    print(regs, file=log)
                    break
                last = pc
                qmp.call('cont')
                time.sleep(0.1)
            else:
                raise TimeoutError(f'{cpu} {case}: no stable terminal PC, evidence {out}')
            word = qmp.call('human-monitor-command', {
                'command-line': f"xp /1wx {sym['sqlite_s1_status']:#x}"})
            print(word, file=log)
            qmp.call('quit')
            rc = proc.wait(timeout=2)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            proc.stdin.close()
            proc.stdout.close()
    markers = ['S1-BEGIN', 'S1-ABI-CALLBACK']
    if case != 'bad-sp':
        markers.append('S1-ENGINE-CALLBACK')
    markers.append(diagnostic)
    actual = [line for line in serial_a.read_text(errors='replace').splitlines()
              if line.startswith('S1-')]
    require(rc == 0 and pc == sym[stop] and word.strip().endswith(f'0x{status:08x}') and
            actual == markers, f'{cpu} {case}: rc={rc} PC={pc:#x} status={word!r} serial={actual!r}')
    print(f"{'GREEN' if status == 0 else 'RED'} {cpu} {out.parent.name}/{out.name}: {diagnostic}/{status} "
          f"{stop}={pc:#x} QEMU rc={rc}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    missing = [t for t in (CC, str(CROSS) + 'nm', 'qemu-system-ppc', 'genisoimage') if not shutil.which(t)]
    if not ROM.is_file():
        missing.append(str(ROM))
    if missing:
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ' ppc-sqlite-stack-guest: missing ' + ', '.join(missing))
        return int(required)

    parent = Path(tempfile.gettempdir()) / 'sb'
    parent.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='sqlite-stack-', dir=parent))
    print(f'evidence={work}', flush=True)
    normal_out = work / 'normal'
    normal_out.mkdir()
    objects = {name: compile_object(normal_out, name, ROOT / name) for name in SOURCES}
    normal = link_image(normal_out, objects)
    cpus = ('7450', '7455')
    for cpu in cpus:
        observe(normal_out / cpu, normal, cpu, 'normal')
    if args.mutate:
        for case, name, anchor, replacement in MUTATIONS:
            out = work / case
            out.mkdir()
            source = out / Path(name).with_suffix('.mut' + Path(name).suffix).name
            original = (ROOT / name).read_text()
            require(original.count(anchor) == 1, f'{case}: mutation anchor must be unique')
            source.write_text(original.replace(anchor, replacement))
            image = link_image(out, {**objects, name: compile_object(out, name, source)})
            for cpu in cpus:
                observe(out / cpu, image, cpu, case)
                observe(out / (cpu + '-control'), normal, cpu, 'normal')
        check_type_mutations(work)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
