#!/usr/bin/env python3
"""T4c-1: paired product SQLite controls, write-free rejection and mutants."""
import argparse
import json
import os
import re
import shlex
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import mkmod
import gen_sqlite_imports
from test_module_image import build_validator, validate, fixture, crc, word, POLICY
from test_mkmod import rejected
from test_module_runtime import FLAGS as RUNTIME_FLAGS, INCLUDES as RUNTIME_INCLUDES
import host32

def function(source, name):
    match = re.search(r'^(?:static )?(?:int|u32|void) ' + name + r'\([^)]*\)\n\{', source, re.M)
    assert match, name
    return source[match.start():source.index('\n}', match.end()) + 2]


def check(run, rc, name):
    expected = b'GREEN\n' if rc == 0 else f'RED {rc:02}\n'.encode()
    assert run.returncode == bool(rc) and run.stdout == expected, (
        name, run.returncode, run.stdout, run.stderr)


def relocation_suite(exe, runner):
    # First relocation is valid. The second is invalid so an eager writer is
    # caught by the full-buffer comparison in the C harness (including BSS).
    for base in (0, 0x1200000, 0xffffefff):
        check(validate(exe, fixture(), relocate_base=base, runner=runner, **POLICY), 0, 'unaligned')
    check(validate(exe, fixture(), relocate_base=0xfffff000, runner=runner, **POLICY),
          11, 'base overflow')
    for name, offset in (('tail-minus-two', 62), ('BSS', 64), ('rounding', 1000),
                         ('duplicate', 1), ('partial overlap', 3), ('import overlap', 32),
                         ('prefix-as-offset', 0xfffffffc)):
        bad = fixture()
        word(bad, 37, offset)
        check(validate(exe, crc(bad), relocate_base=0x1200000, runner=runner, **POLICY), 11, name)
    # Exact last four payload bytes are valid.
    tail = fixture()
    word(tail, 37, 60)
    struct.pack_into('<I', tail, 4096 + 60, 64)
    check(validate(exe, crc(tail), relocate_base=0x1200000, runner=runner, **POLICY), 0, 'last word')


def real_suite(exe, directory, vmkernel, runner, packer=mkmod):
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((directory / 'sqlite_imports.json').read_text())
    elf = (directory / 'sqlite.elf').read_bytes()
    kernel = vmkernel.read_bytes()
    pair = mkmod.pair_crc(kernel)
    image, stats = packer.build(elf, metadata, kernel, spec['version'],
                               spec['generations']['memory_layout'], module_id=2)
    assert image == (directory / 'sqlite.bin').read_bytes(), 'stale prototype image'
    parsed = mkmod.Elf(elf)
    assert parsed.symbol('sqlite_module_pool')['size'] == 4
    assert 'sqlite_mem_pool' not in parsed.symbols and 'tick_count' not in parsed.symbols
    assert len(metadata['imports']) == 29 and len(metadata['imports']) <= 32
    assert all(e['context'] == 'normal' and not e['irq'] for e in metadata['imports'])
    assert not {'memcpy', 'memset', 'tick_count'} & {e['name'] for e in metadata['imports']}
    assert stats['mem_size'] < 384 * 1024, 'static pool was retained'
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], module_id=2, ops=2, img_valid=1, pair=pair,
                    imports=[(mkmod.fnv1a(e['name'].encode()), e['type_ver'], e['kind'])
                             for e in metadata['imports']],
                    exports=[(1, mkmod.fnv1a(b'initialize'), 1)], runner=runner)
    check(validate(exe, image, **contract), 0, 'real SQLite validate')
    check(validate(exe, image, relocate_base=0x1200000, **contract), 0, 'real SQLite relocate')
    check(validate(exe, image, relocate_base=0xffffffff - stats['mem_size'] + 1, **contract),
          11, 'real SQLite base overflow')
    h = struct.unpack_from('<36I', image)
    assert h[9] == 1 and h[31] == pair and h[33] == 2
    for name, field, value, rc in (
            ('PAIR required', 9, 0, 3), ('old/new pair', 31, pair ^ 1, 4),
            ('old layout', 5, spec['generations']['memory_layout'] ^ 1, 6),
            ('old module ABI', 6, metadata['module_abi_gen'] ^ 1, 6),
            ('old ops v1', 33, 1, 6)):
        bad = bytearray(image)
        word(bad, field, value)
        check(validate(exe, crc(bad), relocate_base=0x1200000, **contract), rc, name)
    check(validate(exe, image, **dict(contract, img_valid=0)), 5, 'missing boot CRC')
    rejected(lambda: packer.build(elf, metadata, b'', spec['version'],
                                 spec['generations']['memory_layout'], module_id=2), 'VK32')
    for field, value in ((22, 4097), (24, 33), (26, 2), (10, 528384)):
        bad = bytearray(image)
        word(bad, field, value)
        check(validate(exe, crc(bad), relocate_base=0x1200000, **contract), 3, 'SQLite policy cap')
    # Separate image/load-span caps with all other ranges still legal.
    for prefix, memory in ((147456, h[10]), (143360, 524288)):
        bad = bytearray(image[:h[11]])
        bad.extend(bytes(prefix - len(bad)))
        bad.extend(image[h[11]:])
        for field, value in ((3, len(bad)), (11, prefix), (10, memory)):
            word(bad, field, value)
        check(validate(exe, crc(bad), relocate_base=0x1200000, **contract), 3, 'image/load span cap')
    # Producer refusal based on actual linked relocations/symbols, not synthetic metadata.
    rel = next(s for s in parsed.sections if s['type'] == 9 and s['size'] and
               parsed.sections[s['info']]['flags'] & 2)
    info = struct.unpack_from('<I', elf, rel['offset'] + 4)[0]
    symtab = parsed.sections[rel['link']]
    symoff = symtab['offset'] + (info >> 8) * 16
    def build(data):
        return packer.build(data, metadata, kernel, spec['version'],
                            spec['generations']['memory_layout'], module_id=2)
    bad = bytearray(elf)
    struct.pack_into('<H', bad, symoff + 14, 0)
    rejected(lambda: build(bad), 'unresolved')
    bad = bytearray(elf)
    struct.pack_into('<H', bad, symoff + 14, 0xfff1)
    rejected(lambda: build(bad), 'allocated body symbol')
    bad = bytearray(elf)
    struct.pack_into('<I', bad, rel['offset'] + 4, (info & ~255) | 3)
    rejected(lambda: build(bad), 'relocation type')
    # R_386_32 addends cannot hide MMIO or one-past without the named end contract.
    for section in parsed.sections:
        if section['type'] != 9 or not section['size']:
            continue
        target = parsed.sections[section['info']]
        if not target['flags'] & 2:
            continue
        records = parsed.section_data(section)
        for at in range(0, len(records), 8):
            where, rinfo = struct.unpack_from('<II', records, at)
            if rinfo & 255 != 1:
                continue
            bad = bytearray(elf)
            struct.pack_into('<I', bad, target['offset'] + where - target['addr'], stats['mem_size'])
            rejected(lambda: build(bad), 'value outside body')
            # Only the explicitly named end symbol may carry body one-past.
            strings = parsed.section_data(parsed.sections[parsed.sections[section['link']]['link']])
            end_name = strings.index(b'__module_end\0')
            symbol_at = parsed.sections[section['link']]['offset'] + (rinfo >> 8) * 16
            struct.pack_into('<II', bad, symbol_at, end_name, stats['mem_size'])
            build(bad)
            return stats
    raise AssertionError('no absolute relocation in real SQLite')


MUTATIONS = (
    ('payload-boundary', '!within(off, MODULE_WORD_BYTES, h[MI_PAYLOAD_SIZE])', '!within(off, 2, h[MI_PAYLOAD_SIZE])'),
    ('base-overflow', 'if (base > U32_MAX - h[MI_MEM_SIZE])', 'if (0)'),
    ('import-crossing', 'if (overlap(off, MODULE_WORD_BYTES, slot, MODULE_WORD_BYTES))', 'if (0 && overlap(off, MODULE_WORD_BYTES, slot, MODULE_WORD_BYTES))'),
    ('duplicate-write', 'if (overlap(off, MODULE_WORD_BYTES, prev, MODULE_WORD_BYTES))', 'if (0 && overlap(off, MODULE_WORD_BYTES, prev, MODULE_WORD_BYTES))'),
)


def pair_runtime_suite(folder, mutate=False):
    """Reuse the actual transaction fixture to observe rejection before init."""
    harness = (ROOT / 'tools/tests/module_runtime_host.c').read_text()
    harness = harness.replace('MODULE_ID_KDEBUG', 'MODULE_ID_SQLITE')
    harness = harness.replace('exports[4]', 'exports[0]')
    image_source = (ROOT / 'kernel/module/module_image.c').read_text().replace(
        '"../lib/crc32_core.inc"', '"' + str(ROOT / 'lib/crc32_core.inc') + '"')
    schema = (ROOT / 'kernel/module/module_schema.h').read_text()
    # The shared fixture follows KDEBUG's ops version; the caller policy and
    # image must carry the actual SQLITE row's version so that only the field
    # under test (flags / pair CRC / ops) decides the rejection.
    sqlite_row = schema.split('.module_id = 2U,', 1)[1].split('.module_id = 3U,', 1)[0]
    sqlite_ops = int(re.search(r'\.ops_version = (\d+)U,', sqlite_row).group(1))
    assert sqlite_ops != 1  # run(33, 1, 6) must stay a real mismatch
    caller_policy = '.policy = {75, 3, 0x12345678, 0xabcdef01, '
    assert harness.count(caller_policy) == 1
    head, tail = harness.split(caller_policy)
    harness = head + caller_policy + str(sqlite_ops) + tail[tail.index(','):]
    harness_path = folder / 'sqlite_runtime_host.c'
    harness_path.write_text(harness)
    (folder / 'module_image.h').write_bytes((ROOT / 'kernel/module/module_image.h').read_bytes())
    source_path = folder / 'module_image.c'
    schema_path = folder / 'module_schema.h'
    exe = folder / 'sqlite-runtime'

    def build(source=image_source, policy=schema):
        source_path.write_text(source)
        schema_path.write_text(policy)
        subprocess.run(['gcc', *RUNTIME_FLAGS, '-Werror=vla', '-Wno-unused-parameter',
                        *RUNTIME_INCLUDES, '-nostdlib', '-static', '-no-pie',
                        '-Wl,--gc-sections', harness_path, ROOT / 'kernel/module/module_runtime.c',
                        ROOT / 'fs/vfs.c', ROOT / 'drivers/dev.c', source_path,
                        '-o', exe], check=True)

    def run(field, value, rc, expected=0):
        data = fixture()
        word(data, 7, 2)
        # One export with the real product slot/name; imports remain the small
        # independent transaction fixture's kprintf contract.
        name = b'initialize'
        struct.pack_into('<IHHI', data, 164, mkmod.fnv1a(name), 1, 1, 10)
        names = b'kprintf\0' + name + b'\0'
        data[176:176 + len(names)] = names
        word(data, 28, len(names))
        word(data, 33, sqlite_ops)
        word(data, field, value)
        payload = struct.pack('<2I', 9, rc) + crc(data)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
            assert result.returncode == expected, (field, runner, result)
            if expected:
                assert b'FAIL rc == (int)input[1] && !starts && !module_exports(h)' in result.stdout

    build()
    run(9, 0, 3)
    run(31, 0xabcdef00, 4)
    run(33, 1, 6)
    print('PASS SQLite runtime: PAIR/old-new/ops1 rejected, init calls=0, exports absent')
    if mutate:
        # Change the actual SQLITE row, preserving KDEBUG and allowed flags.
        marker = '.module_id = 2U,'
        assert schema.count(marker) == 1
        head, tail = schema.split(marker)
        row, following = tail.split('.module_id = 3U,', 1)
        assert row.count('.required_flags = 1U,') == 1
        policy = head + marker + row.replace('.required_flags = 1U,', '.required_flags = 0U,') + '.module_id = 3U,' + following
        build(policy=policy)
        run(9, 0, 3, expected=1)
        print('MUTATE sqlite-pair-policy RED (compiled, runtime acceptance assertion)')
        old = 'h[MI_PAIR_CRC] != policy->pair_image_crc'
        assert image_source.count(old) == 1
        build(source=image_source.replace(old, '0'))
        run(31, 0xabcdef00, 4, expected=1)
        print('MUTATE sqlite-mixed-pair RED (compiled, runtime acceptance assertion)')


def real_runtime_suite(folder, directory, vmkernel, mutate=False, stack_mutate=False, fep_mutate=False, policy_mutate=False):
    """Execute the product module with the full adapter and real stack switch."""
    generated = ROOT / 'lib/sqlite3/generated'
    (folder / 'sys_build_info.inc').write_text(
        function((ROOT / 'kapi/kapi_sys.c').read_text(), 'build_time_copy') + '\n' +
        function((ROOT / 'kapi/kapi_sys.c').read_text(), 'kapi_sys_get_build_info'))
    fep_directory = ROOT / 'build/out/fep'
    fep_elf = mkmod.Elf((fep_directory / 'fep.elf').read_bytes())
    (folder / 'fep_host_offsets.h').write_text(
        '#define HOST_FEP_STATE ' + str(next(sym['value'] for table in fep_elf.symtabs.values() for sym in table if sym['name'] == 'g_ime')) + 'u\n')
    (folder / 'fep_module.c').write_bytes((ROOT / 'kernel/ime/fep_module.c').read_bytes())
    smoke = (ROOT / 'lib/sqlite3/os32_sqlite_test.c').read_text()
    (folder / 'sqlite_smoke.c').write_text(smoke)
    for name in ('sqlite_engine', 'sqlite_outer'):
        source = (ROOT / ('kernel/ime' if name in ('ime', 'ime_dict') else 'kernel') / (name + '.c')).read_text()
        # Disk settings I/O belongs to the VFS fixture; use a private RAM DB here.
        source = source.replace('SYS_SETTINGS_DB, SQLITE_OPEN_READONLY', '":memory:", SQLITE_OPEN_READWRITE')
        if name == 'sqlite_engine':
            source = ('struct sqlite3; extern void host_seed_settings(struct sqlite3 *db);\n'
                      'extern int host_final_boot_check(int, int);\n') + source.replace(
                'os32_sqlite_outer_boot_check(1, reserve)', 'host_final_boot_check(1, reserve)').replace(
                '    if (!rc && !os32_sqlite_prepare_v2(db,',
                '    if (!rc) host_seed_settings(db);\n    if (!rc && !os32_sqlite_prepare_v2(db,')
        (folder / (name + '.c')).write_text(source.replace(
            '../lib/sqlite3/generated/', str(generated) + '/'))
    (folder / 'io.h').write_text(
        'extern unsigned long host_flags;\n'
        'void host_stop(void);\n'
        'static inline int _irq_enabled(void) { return (host_flags & 512UL) != 0; }\n'
        'static inline void _disable(void) { host_flags &= ~512UL; }\n'
        'static inline void _stop(void) { host_stop(); }\n'
        'static inline unsigned long irq_save(void) { unsigned long f=host_flags; '
        'host_flags &= ~512UL; return f ^ 512UL; }\n'
        'static inline void irq_restore(unsigned long f) { host_flags=f ^ 512UL; }\n')
    handlers = (ROOT / 'arch/x86/kernel/isr_handlers.c').read_text()
    start = handlers.index('void sqlite_engine_fault(')
    (folder / 'sqlite_fault.inc').write_text(
        '#include "io.h"\n#include "pc98.h"\n' + handlers[start:handlers.index('\n}', start) + 2])
    # Real contracts, with unneeded disk/heap imports bound to a failing probe.
    # :memory: still executes the actual module's VFS registry and MEMSYS5.
    exports = (generated / 'sqlite_kexports.c').read_text()
    end = exports.index('};', exports.index('const u32 sqlite_kexports[]')) + 2
    metadata = json.loads((directory / 'sqlite_imports.json').read_text())
    used = {'kprintf', 'sys_time', 'ktime_ticks', 'vfs_count_sqlite', 'vfs_quarantine_sqlite'}
    (folder / 'sqlite_kexports.c').write_text(
        '#include "sqlite_imports.h"\n#include "module_image.h"\n'
        'extern void sqlite_forbidden_import(void);\n'
        'const u32 sqlite_kexports[] = {\n' + ''.join(
            '    (u32)(uptr)' + (row['name'] if row['name'] in used else 'sqlite_forbidden_import') + ',\n'
            for row in metadata['imports']) + '};\n' + exports[end:])
    fep_metadata = json.loads((fep_directory / 'fep_imports.json').read_text())
    fep_exports = (fep_directory / 'fep_kexports.c').read_text()
    end = fep_exports.index('};', fep_exports.index('const u32 fep_kexports[]')) + 2
    used = {'kmemcpy', 'kmemset', 'kstrncpy', 'kstrlen', 'kprintf', 'kmalloc',
            'kfree', 'kmalloc_block_size', 'serial_puts', 'sqlite_engine_available',
            'con_sink_is_enabled', 'kbd_trygetkey', 'kbd_trygetchar', 'kbd_getkey_wait',
            'ring3_call_from_user', 'res_owner_get', 'ring3_abort_check',
            'tvram_set_scroll_reserve'}
    wrappers = {'os32_sqlite_outer_group_open': 'host_fep_group_open',
                'os32_sqlite_outer_group_acquire': 'host_fep_group_acquire',
                'os32_sqlite_prepare_v2': 'host_fep_prepare'}
    (folder / 'fep_kexports.c').write_text(
        '#include "fep_imports.h"\n#include "module_image.h"\n'
        'extern void sqlite_forbidden_import(void);\n'
        'extern int host_fep_group_open(const Os32SqliteGroup *, const char *, int, sqlite3 **);\n'
        'extern int host_fep_group_acquire(int, int, Os32SqliteGroup *);\n'
        'extern int host_fep_prepare(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);\n'
        'const u32 fep_kexports[] = {\n' + ''.join(
            '    (u32)(uptr)' + wrappers.get(row['name'], row['name'] if
                row['name'] in used or row['name'].startswith(('os32_sqlite_', 'fep_'))
                else 'sqlite_forbidden_import') + ',\n' for row in fep_metadata['imports'])
        + '};\n' + fep_exports[end:])
    obj = folder / 'sqlite_stack.o'
    subprocess.run(['nasm', '-f', 'elf32', ROOT / 'lib/sqlite3/sqlite_stack.asm', '-o', obj], check=True)
    exe = folder / 'sqlite-module'
    command = ['gcc', *RUNTIME_FLAGS, '-Werror=vla', '-I' + str(folder), *RUNTIME_INCLUDES,
                    '-I' + str(ROOT / 'lib/sqlite3'), '-I' + str(generated),
                    '-I' + str(fep_directory), '-I' + str(ROOT / 'drivers'),
                    '-nostdlib', '-static', '-no-pie',
                    ROOT / 'tools/tests/sqlite_module_host.c',
                    folder / 'sqlite_engine.c', folder / 'sqlite_outer.c',
                    folder / 'sqlite_kexports.c', folder / 'fep_kexports.c', folder / 'fep_module.c',
                    ROOT / 'kernel/module/module_runtime.c',
                    ROOT / 'kernel/module/module_image.c', ROOT / 'kernel/console/unicode_file.c',
                    folder / 'sqlite_smoke.c', obj, '-o', exe]
    subprocess.run(command, check=True)
    pair = mkmod.pair_crc(vmkernel.read_bytes())
    image = (directory / 'sqlite.bin').read_bytes()
    fep_image = (ROOT / 'build/out/fep.mod').read_bytes()
    fep_payload = struct.pack('<I', len(fep_image)) + fep_image
    for label, mode, expected in (('running', 0, 0), ('missing', 1, 2),
                                  ('mixed-pair', 2, 4), ('prefix-failure', 3, 10),
                                  ('pool-failure', 4, 10), ('stack-failure', 5, 10),
                                  ('stack-overflow', 6, 0)):
        data = bytearray(image)
        if mode == 2:
            word(data, 31, pair ^ 1)
            crc(data)
        payload = struct.pack('<4I', mode, len(data), pair, expected) + data + fep_payload
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
            assert result.returncode == (77 if mode == 6 else 0), (label, runner, result)
            output = result.stdout.decode()
            if mode == 0:
                print(output, end='')
            if mode == 6:
                assert 'PASS host signal seam -> engine fail-stop (NOT IDT/#DF)' in output, result
                print(output, end='')
        print('PASS real SQLite adapter ' + label, flush=True)
    # Keep stack modes separate from boot/settings modes 10..36 in the harness.
    for label, mode in (('expr-depth-100', 40), ('compound-5', 41),
                        ('sort-1024', 42), ('index-1024', 43), ('trigger-omitted', 44)):
        payload = struct.pack('<4I', mode, len(image), pair, 0) + image + fep_payload
        result = host32.run([str(exe)], input=payload, runner='qemu', capture_output=True)
        print(result.stdout.decode(), end='', flush=True)
        assert result.returncode == 0 and f'STACK workload={mode} '.encode() in result.stdout, (label, result)
    if stack_mutate:
        # Depth 100 exceeds 16 KiB: require an observed runtime guard fault.
        header = (ROOT / 'kernel/sqlite_engine.h').read_text()
        old = '(32u * 1024u)'
        assert header.count(old) == 1
        (folder / 'sqlite_engine.h').write_text(header.replace(old, '(16u * 1024u)'))
        subprocess.run(command, check=True)
        payload = struct.pack('<4I', 40, len(image), pair, 0) + image + fep_payload
        result = host32.run([str(exe)], input=payload, runner='qemu', capture_output=True)
        assert result.returncode == 77 and b'PASS host signal seam -> engine fail-stop (NOT IDT/#DF)' in result.stdout, result
        print('MUTATE undersized-stack RED (compiled runtime): ' + result.stdout.decode().strip(), flush=True)
        (folder / 'sqlite_engine.h').unlink()
        subprocess.run(command, check=True)
    if mutate:
        path = folder / 'sqlite_engine.c'
        source = path.read_text()
        fill = ('    kmemset((void *)(uptr)(sqlite_engine_stack_top - SQLITE_ENGINE_STACK_SIZE),\n'
                '            SQLITE_STACK_MARK, SQLITE_ENGINE_STACK_SIZE);')
        assert source.count(fill) == 1
        path.write_text(source.replace(fill, '    /* mutation: omit initial stack fill */'))
        subprocess.run(command, check=True)
        payload = struct.pack('<4I', 0, len(image), pair, 0) + image + fep_payload
        result = host32.run([str(exe)], input=payload, runner='qemu', capture_output=True)
        assert result.returncode == 1 and b'FAIL deep_water > 0 && deep_water < SQLITE_ENGINE_STACK_SIZE' in result.stdout, result
        print('MUTATE stack-fill RED (compiled runtime): ' + result.stdout.decode().strip(), flush=True)
        path.write_text(source)
        subprocess.run(command, check=True)
    boot_cases = [('services-ready', 10), ('missing-sqlite', 11), ('mixed-sqlite', 12),
                  ('image-allocation', 13), ('unicode-missing', 14), ('unicode-old', 15),
                  ('unicode-io', 16), ('unicode-allocation', 17), ('partial-FEP', 18),
                  ('settings-reserve', 19), ('settings-lower-reserve', 20),
                  ('learn-prepare-failure', 32), ('close-fault-after-ready', 33),
                  ('settings-below-minimum', 34), ('settings-minimum', 35),
                  ('settings-minimum-minus-one', 36), ('missing-FEP', 50),
                  ('final-ready-check-failure', 54), ('mixed-FEP', 51), ('FEP-allocation', 52), ('FEP-PAIR-required', 53),
                  ('no-dictionary-sqlite-only', 55), ('dictionary-stat-io', 56),
                  ('no-dictionary-missing-sqlite', 57), ('no-dictionary-sqlite-init', 58),
                  ('no-dictionary-settings-close', 59), ('no-dictionary-missing-fep', 60)]
    boot_cases += [(name, 20 + fault) for fault, name in enumerate((
        'pool', 'stack', 'sqlite-init', 'settings-close', 'fep-text', 'fep-attr',
        'fep-crc', 'dictionary', 'reserve', 'fep-close', 'shutdown'), 1)]
    def boot_run(mode, fep_body=None, sqlite_body=None):
        data = bytearray(image if sqlite_body is None else sqlite_body)
        if mode == 12:
            word(data, 31, pair ^ 1)
            crc(data)
        fep_data = bytearray(fep_image if fep_body is None else fep_body)
        if mode in (51, 53):
            word(fep_data, 31 if mode == 51 else 9, pair ^ 1 if mode == 51 else 0)
            crc(fep_data)
        return host32.run([str(exe)], input=struct.pack('<4I', mode, len(data), pair, 0) + data
                          + struct.pack('<I', len(fep_data)) + fep_data,
                          runner='qemu', capture_output=True)
    for label, mode in boot_cases:
        result = boot_run(mode)
        assert result.returncode == 0 and b'occupancy sqlite_pages=' in result.stdout, (label, result)
        print('PASS boot ' + label + ': ' + result.stdout.decode().strip(), flush=True)
    if policy_mutate:
        path = folder / 'sqlite_engine.c'
        source = path.read_text()
        old = 'if (vfs_stat(required_paths[i], &dictionary) != VFS_OK) goto rollback;'
        assert source.count(old) == 1
        path.write_text(source.replace(old,
            '(void)vfs_stat(required_paths[i], &dictionary);'))
        subprocess.run(command, check=True)
        result = boot_run(60)
        assert result.returncode == 1 and b'FAIL !services_public' in result.stdout, result
        print('MUTATE required-check-removed RED (compiled runtime): ' + result.stdout.decode().strip(), flush=True)
        path.write_text(source)
    if mutate:
        path = folder / 'sqlite_engine.c'
        source = path.read_text()
        start = source.index('    if (paging_set_not_present(')
        end = source.index('\n    sqlite_engine_stack_top =', start)
        path.write_text(source[:start] + source[end:])
        subprocess.run(command, check=True)
        payload = struct.pack('<4I', 0, len(image), pair, 0) + image + fep_payload
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
            assert result.returncode == 1 and b'FAIL guard_np &&' in result.stdout, result
        print('MUTATE stack-guard-removed RED (compiled, runtime guard assertion)')
        path.write_text(source)
        # Revert the ABSENT filters separately; the final-check failure reuses
        # both reclaimed IDs before calling the real ver report path.
        for name in ('sqlite_engine.c', 'fep_module.c'):
            target = folder / name
            original = target.read_text()
            old = 'r && r->state != MODULE_ABSENT && r->owner ?'
            assert old in original, name
            target.write_text(original.replace(old, 'r && r->owner ?'))
            subprocess.run(command, check=True)
            result = boot_run(54)
            assert result.returncode == 1 and b'FAIL ' in result.stdout, (name, result)
            print('MUTATE absent-owner-reuse ' + name + ' RED (compiled runtime): '
                  + result.stdout.decode().strip(), flush=True)
            target.write_text(original)
        runtime_path = folder / 'module_runtime_boot.c'
        runtime_source = (ROOT / 'kernel/module/module_runtime.c').read_text()
        runtime_path.write_text(runtime_source)
        boot_command = [runtime_path if item == ROOT / 'kernel/module/module_runtime.c' else item for item in command]
        mutations = [
            ('no-dictionary-loads-FEP', path, source,
             'vfs_stat(IME_DICT_PATH, &dictionary) == VFS_ERR_NOTFOUND',
             '(vfs_stat(IME_DICT_PATH, &dictionary), 0)', 55),
            ('dictionary-present-sqlite-only', path, source,
             'vfs_stat(IME_DICT_PATH, &dictionary) == VFS_ERR_NOTFOUND',
             'vfs_stat(IME_DICT_PATH, &dictionary) == VFS_OK', 10),
            ('no-dictionary-MINIMAL', path, source,
             '    rc = os32_sqlite_start();',
             '    if (no_dictionary) { services_stage = SERVICES_MINIMAL; return; }\n'
             '    rc = os32_sqlite_start();', 55),
            ('sqlite-only-not-public', path, source,
             '        services_public = 1;', '        services_public = 0;', 55),
            ('sqlite-only-called-ready', path, source,
             '        services_stage = SERVICES_SQLITE_ONLY;',
             '        services_stage = SERVICES_READY;', 55),
            ('reserve-lower-MINIMAL', path, source,
             '*reserve = OS32_SQLITE_FEP_RESERVE_MIN;', '*reserve = -1;', 34),
            ('reserve-reject-lower', path, source,
             function(source, 'services_settings'),
             function(source, 'services_settings').replace('OS32_SQLITE_FEP_RESERVE_MIN',
                                                          'OS32_SQLITE_FEP_RESERVE'), 20),
            ('reserve-no-minimum', path, source,
             'if (kb < OS32_SQLITE_FEP_RESERVE_MIN / (int)KIB)', 'if (0)', 34),
            ('early-public', path, source,
             '    services_stage = SERVICES_SQLITE_INTERNAL_READY;',
             '    services_stage = SERVICES_SQLITE_INTERNAL_READY; services_public = 1;', 10),
            ('isolated-free', runtime_path, runtime_source,
             '    if (!stopped || r->irq_refs || r->tick_refs || r->callback_refs ||\n        r->dma_refs || r->active_refs || r->registration_refs) goto quarantined;',
             '    if (0 && (!stopped || r->irq_refs || r->tick_refs || r->callback_refs ||\n        r->dma_refs || r->active_refs || r->registration_refs)) goto quarantined;', 31),
            ('unicode-reclaim', path, source,
             '    cleanup_blocked = fep_module_abort() != 0;',
             '    u32 lost; ledger_reclaim_owner(LEDGER_OWNER_KERNEL, &lost);\n    cleanup_blocked = fep_module_abort() != 0;', 28),
        ]
        for label, target, original, old, new, mode in mutations:
            assert original.count(old) == 1, label
            target.write_text(original.replace(old, new))
            subprocess.run(boot_command, check=True)
            result = boot_run(mode)
            assert result.returncode == 1 and b'FAIL ' in result.stdout, (label, result)
            print('MUTATE ' + label + ' RED (compiled runtime): ' + result.stdout.decode().strip(), flush=True)
            target.write_text(original)
    if mutate:
        # Remove the actual module's zero-resident reservation release, then
        # repack the product body. The same runtime pressure assertion must fail.
        flags = shlex.split(subprocess.check_output([
            'make', '--no-print-directory', '-s', '-f', 'build/config.mk',
            '--eval=host-sqlite-flags: ; @echo $(CFLAGS_SQLITE) $(INC_SQLITE)',
            'host-sqlite-flags'], cwd=ROOT, text=True))
        compiler = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        source = (directory / 'os32_sqlite_vfs.c').read_text()
        old = '    if (!live && !reserve) budget->reserve = 0;'
        assert source.count(old) == 1
        path = folder / 'os32_sqlite_vfs.c'
        path.write_text(source.replace(old, '    /* mutation: keep unused FEP reservation */'))
        obj_path = folder / 'os32_sqlite_vfs.o'
        subprocess.run([compiler, *flags, '-I.', '-I' + str(directory),
                        '-include', ROOT / 'lib/sqlite3/os32_sqlite_config.h',
                        '-c', path, '-o', obj_path], cwd=ROOT, check=True)
        elf_path = folder / 'sqlite-no-release.elf'
        objects = [obj_path if name == 'os32_sqlite_vfs' else directory / (name + '.o')
                   for name in ('sqlite3', 'os32_sqlite_vfs', 'sqlite_module',
                                'sqlite_import_slots', 'sqlite_import_stubs',
                                'kstring', 'kstring_c', 'kmath')]
        libgcc = subprocess.check_output([compiler, '-print-libgcc-file-name'], text=True).strip()
        subprocess.run([compiler.with_name('i386-elf-ld'), '-m', 'elf_i386', '--emit-relocs',
                        '-T', ROOT / 'build/sqlite-module.ld', *objects, libgcc,
                        '-o', elf_path], cwd=ROOT, check=True)
        spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
        body, _ = mkmod.build(elf_path.read_bytes(), metadata, vmkernel.read_bytes(),
                             spec['version'], spec['generations']['memory_layout'], module_id=2)
        # Restore the host adapter after the preceding core mutations.
        subprocess.run(command, check=True)
        result = boot_run(55, sqlite_body=body)
        assert result.returncode == 1 and b'FAIL budget->reserve == 0' in result.stdout, result
        print('MUTATE sqlite-only-keeps-reserve RED (repacked product runtime): ' +
              result.stdout.decode().strip(), flush=True)

    if mutate or fep_mutate:
        # Retain the two T4c-2 mutations after moving the actual FEP code into
        # its paired image. Build only the changed production object in a temp
        # directory, link with the other product objects, then repack normally.
        flags = shlex.split(subprocess.check_output([
            'make', '--no-print-directory', '-s', '-f', 'build/config.mk',
            '--eval=host-fep-flags: ; @echo $(CFLAGS_BASE) -Os -DOS32_FEP_MODULE $(INC_KERNEL)',
            'host-fep-flags'], cwd=ROOT, text=True))
        compiler = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        linker = compiler.with_name('i386-elf-ld')
        libgcc = subprocess.check_output([compiler, '-print-libgcc-file-name'], text=True).strip()
        mutated_dir = folder / 'fep-body'
        mutated_dir.mkdir()
        body_objects = ('fep_body', 'ime', 'ime_dict', 'ime_romkana',
                        'ime_render_tvram', 'fep_import_slots', 'fep_import_stubs')
        spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
        original_offsets = (folder / 'fep_host_offsets.h').read_text()
        for label, name, old, new, mode in (
            ('learn-required-MINIMAL', 'ime', '    g_ime.dict_loaded = 1;',
             '    if (!g_ime.dict.learn_stmt) return -1;\n    g_ime.dict_loaded = 1;', 32),
            ('close-fault-after-ready', 'ime_dict',
             'services_stage < SERVICES_READY &&\n        ', '', 33)):
            source = (ROOT / ('kernel/ime' if name in ('ime', 'ime_dict') else 'kernel') / (name + '.c')).read_text()
            scope = function(source, 'ime_dict_boot' if name == 'ime' else 'ime_dict_close')
            assert scope.count(old) == 1, label
            source_path = mutated_dir / (name + '.c')
            source_path.write_text(source.replace(scope, scope.replace(old, new)))
            obj_path = source_path.with_suffix('.o')
            subprocess.run([compiler, *flags, '-I' + str(fep_directory),
                            '-c', source_path, '-o', obj_path], cwd=ROOT, check=True)
            elf_path = mutated_dir / 'fep.elf'
            objects = [obj_path if stem == name else fep_directory / (stem + '.o')
                       for stem in body_objects]
            subprocess.run([linker, '-m', 'elf_i386', '--emit-relocs', '-T',
                            ROOT / 'build/fep-module.ld', *objects, libgcc,
                            '-o', elf_path], cwd=ROOT, check=True)
            elf_data = elf_path.read_bytes()
            parsed = mkmod.Elf(elf_data)
            state_offset = next(sym['value'] for table in parsed.symtabs.values()
                                for sym in table if sym['name'] == 'g_ime')
            (folder / 'fep_host_offsets.h').write_text('#define HOST_FEP_STATE ' + str(state_offset) + 'u\n')
            body, _ = mkmod.build(elf_data, fep_metadata, vmkernel.read_bytes(),
                                 spec['version'], spec['generations']['memory_layout'], module_id=3)
            subprocess.run(command, check=True)
            result = boot_run(mode, body)
            assert result.returncode == 1 and b'FAIL ' in result.stdout, (label, result)
            print('MUTATE ' + label + ' RED (repacked product runtime): ' + result.stdout.decode().strip(), flush=True)
        (folder / 'fep_host_offsets.h').write_text(original_offsets)
    if mutate or fep_mutate:
        path = folder / 'fep_module.c'
        original = path.read_text()
        old = 'static int stub_feed_key(int keydata) { return keydata; }'
        assert original.count(old) == 1
        path.write_text(original.replace(old,
            'static int stub_feed_key(int keydata) { (void)keydata; return -1; }'))
        subprocess.run(command, check=True)
        result = boot_run(50)
        assert result.returncode == 1 and b'FAIL ime_feed_key(0x1e61) == 0x1e61' in result.stdout, result
        print('MUTATE fep-stub-always-minus-one RED (compiled runtime): ' + result.stdout.decode().strip(), flush=True)
        path.write_text(original)
        path = folder / 'sqlite_engine.c'
        original = path.read_text()
        old = '    cleanup_blocked = fep_module_abort() != 0;'
        assert original.count(old) == 1
        path.write_text(original.replace(old,
            '    u32 lost; ledger_reclaim_owner(module_records[sqlite_handle.slot].owner, &lost);\n' + old))
        subprocess.run(command, check=True)
        result = boot_run(30)
        assert result.returncode == 1 and b'FAIL ' in result.stdout, result
        print('MUTATE fep-dependency-first-free RED (compiled runtime): ' + result.stdout.decode().strip(), flush=True)
        path.write_text(original)
        old = '    cleanup_blocked = fep_module_abort() != 0;'
        abort = '    int isolated = module_abort(sqlite_handle);'
        assert original.count(old) == original.count(abort) == 1
        path.write_text(original.replace(old, abort + '\n' + old).replace(
            abort + '\n    sqlite_module_api', '    sqlite_module_api'))
        subprocess.run(command, check=True)
        result = boot_run(54)
        assert result.returncode == 1 and b'FAIL services_stage == SERVICES_MINIMAL' in result.stdout, result
        print('MUTATE sqlite-abort-before-fep RED (compiled runtime): ' + result.stdout.decode().strip(), flush=True)
        path.write_text(original)
        path = folder / 'fep_module.c'
        original = path.read_text()
        for label, old, new, failure in (
                ('stub-scan-only-poll', 'c < 0 || !(c & 0xff) ? -1', 'c < 0 ? -1',
                 b'FAIL ime_trygetchar() == -1'),
                ('stub-wait-once', 'while (!(c & 0xff));', 'while (0);',
                 b"FAIL ime_getchar() == 'a'")):
            assert original.count(old) == 1, label
            path.write_text(original.replace(old, new))
            subprocess.run(command, check=True)
            result = boot_run(54)
            assert result.returncode == 1 and failure in result.stdout, (label, result)
            print('MUTATE ' + label + ' RED (compiled runtime): ' + result.stdout.decode().strip(), flush=True)
            path.write_text(original)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT / 'build/out/sqlite-proto')
    parser.add_argument('--vmkernel', type=Path, default=ROOT / 'build/out/vmkernel.lz4')
    parser.add_argument('--policy-mutate', action='store_true', help='only T5c-6 required-body mutant')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--fep-mutate', action='store_true', help='only T5a-2 stub and rollback mutants')
    parser.add_argument('--stack-mutate', action='store_true', help='only T5-0b stack capacity mutant')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='sqlite-module-') as td:
        folder = Path(td)
        # Regeneration must be deterministic and separate from KDEBUG's 235 slots.
        generated = gen_sqlite_imports.generate(folder)
        for name in ('sqlite_imports.json', 'sqlite_imports.h', 'sqlite_import_stubs.S'):
            assert (folder / name).read_bytes() == (args.directory / name).read_bytes()
        for name in ('sqlite_outer_calls.inc', 'sqlite_facade.h'):
            assert (folder / name).read_bytes() == (ROOT / 'lib/sqlite3/generated' / name).read_bytes()
        assert generated['module_abi_gen'] != json.loads(
            (ROOT / 'kdebug/generated/kdebug_imports.json').read_text())['module_abi_gen']
        exe = build_validator(folder)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            relocation_suite(exe, runner)
            stats = real_suite(exe, args.directory, args.vmkernel, runner)
            print(f'PASS {runner}: unaligned/last-word reloc; tail-2/BSS/duplicate/import/overflow write=0')
        pair_runtime_suite(folder, mutate=args.mutate)
        real_runtime_suite(folder, args.directory, args.vmkernel, mutate=args.mutate,
                           stack_mutate=args.stack_mutate or args.mutate, fep_mutate=args.fep_mutate, policy_mutate=args.policy_mutate or args.mutate)
        print(json.dumps(stats, sort_keys=True))
        if args.mutate:
            source = (ROOT / 'kernel/module/module_image.c').read_text()
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, name
                path = folder / 'module_image.c'
                path.write_text(source.replace(old, new).replace('"../lib/crc32_core.inc"',
                                '"' + str(ROOT / 'lib/crc32_core.inc') + '"'))
                mutant = build_validator(folder, path)
                try:
                    relocation_suite(mutant, 'qemu')
                except AssertionError as exc:
                    # Only a normal return/output mismatch counts; never a crash.
                    assert exc.args and exc.args[0][1] in (0, 1, 2), exc
                    print('MUTATE', name, 'RED (runtime observation)')
                else:
                    raise AssertionError('surviving mutant: ' + name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
