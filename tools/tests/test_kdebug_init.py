"""S1a: actual init has no imports; actual loader rejects and recovers.
記録: tools/tests/kdebug_init_tdd.md
"""
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

import host32

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_kdebug_imports as gen
import mkmod
from test_mkmod import vk32

FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__']
INCLUDES = ['-I' + str(ROOT / d) for d in
            ('.', 'include', 'kernel', 'lib', 'fs', 'arch/x86', 'platform/pc98',
             'sdk/include', 'sdk/include/os32', 'exec', 'gfx', 'drivers', 'kapi', 'kdebug')]


def command(args):
    subprocess.run([str(a) for a in args], check=True)


def seal(image):
    struct.pack_into('<I', image, 128, 0)
    struct.pack_into('<I', image, 128, zlib.crc32(image))
    return image


def boot_dispatch(kernel):
    """Compile the actual four kernel call sites, including the skipped path."""
    lines = []
    for op in ('selftest_run', 'selftest_post_exec', 'selftest_post_unicode', 'selftest_post_probe'):
        found = re.findall(r'^    if \(.*\) kdebug\.ops\.' + op + r'\(\);$', kernel, re.M)
        assert len(found) == 1, op
        lines += found
        if op == 'selftest_run':
            assert found[0] + '\n    else kselftest_skipped();' in kernel
            lines.append('    else kselftest_skipped();')
    return 'static void host_boot_dispatch(void) {\n' + '\n'.join(lines) + '\n}\n'


def main():
    with tempfile.TemporaryDirectory(prefix='kdebug-init-') as td:
        folder = Path(td)
        meta = gen.generate(gen.load_def(), folder, 'S5')
        for generated in (ROOT / 'kdebug/generated').iterdir():
            if generated.suffix in ('.c', '.h', '.S', '.json'):
                assert generated.read_bytes() == (folder / generated.name).read_bytes(), generated
        cross = Path(os.environ['CROSS_DIR']) / 'bin'
        inc = [*INCLUDES, '-I' + td, '-I' + str(ROOT/'hwdev/generated')]
        host = ['gcc', *FLAGS, *inc, '-nostdlib', '-static', '-no-pie']
        exe = folder / 'init'
        harness = ROOT / 'tools/tests/kdebug_init_host.c'
        source = ROOT / 'kdebug/kdebug_main.c'
        command([*host, harness, source, '-o', exe])
        for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
            assert host32.run([str(exe)], runner=runner).returncode == 0
        trace_exe = folder / 'trace'
        command([*host, ROOT / 'tools/tests/nic_trace_host.c', '-o', trace_exe])
        for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
            assert host32.run([str(trace_exe)], runner=runner).returncode == 0
        print('PASS nic_trace: identity, append/sync, read/write/sync failure latch (13 scenarios, all 25 LB3 stage IDs at u32 width)')
        # A called import and a missing export must each fail by assertion.
        if '--mutate' in sys.argv:
            for label, old, new in (
                ('init-import', '    *candidate = kdebug_ops;',
                 '    kprintf(7, "forbidden");\n    *candidate = kdebug_ops;'),
                ('init-empty', '    *candidate = kdebug_ops;', '    (void)candidate;')):
                mutant = folder / (label + '.c')
                mutant.write_text('#include "kprintf.h"\n' + source.read_text().replace(old, new))
                command([*host, harness, mutant, '-o', exe])
                for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                    assert host32.run([str(exe)], runner=runner).returncode == 1, label
                print('MUTATE', label, 'RED', flush=True)
        if '--mutate' in sys.argv:
            trace_source = (ROOT / 'kdebug/nic_trace.c').read_text()
            trace_harness = (ROOT / 'tools/tests/nic_trace_host.c').read_text()
            for label, old, new in (
                ('trace-cap', '#define NIC_TRACE_CAP 768U', '#define NIC_TRACE_CAP 512U'),
                ('trace-sync', 'vfs_sync() != VFS_OK', '0'),
                ('trace-latch', 'if (trace_off) return;', 'if (0) return;'),
                ('trace-crc', 'crc != image.image_crc', '0'),
                ('trace-fresh', 'if (stage == E82557_TRACE_TRY) trace_len = 0;', '')):
                assert old in trace_source
                mutant = folder / (label + '.c')
                mutant.write_text(trace_source.replace(old, new))
                harness_copy = folder / (label + '-host.c')
                harness_copy.write_text(trace_harness.replace('../../kdebug/nic_trace.c', str(mutant)))
                command([*host, '-I' + str(ROOT / 'kdebug'), harness_copy, '-o', trace_exe])
                for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                    assert host32.run([str(trace_exe)], runner=runner).returncode == 1, label
                print('MUTATE', label, 'RED', flush=True)
        # Build the real module independently of pre-existing product artifacts.
        objects = []
        for src in (ROOT / 'kdebug/crt_diag.c', ROOT / 'kdebug/kselftest.c',
                    ROOT / 'kdebug/nic_trace.c', ROOT / 'kdebug/v86_gcap.c',
                    ROOT / 'kdebug/v86_gcap_math.c', ROOT / 'kdebug/kpeek.c', ROOT / 'kdebug/kdebug_window.c',
                    ROOT / 'kdebug/kpeek_allow.c', ROOT / 'kdebug/recover.c',
                    ROOT / 'kdebug/recover_profile.c', ROOT / 'hwdev/generated/hwdev_core.c', source,
                    folder / 'kdebug_import_slots.c', folder / 'kdebug_import_stubs.S'):
            obj = folder / (src.stem + '.o')
            command([cross / 'i386-elf-gcc', '-DRECOVER_MODULE', *FLAGS, *inc, '-c', src, '-o', obj])
            objects.append(obj)
        elf = folder / 'module.elf'
        command([cross / 'i386-elf-ld', '-m', 'elf_i386', '--emit-relocs',
                 '-T', ROOT / 'build/module.ld', *objects, '-o', elf])
        kernel, pair = vk32()
        spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
        image, stats = mkmod.build(elf.read_bytes(), meta, kernel,
                                  kapi_abi_gen=spec['version'],
                                  layout_gen=spec['generations']['memory_layout'])
        exe = folder / 'loader'
        # All real S2 imports bind to a counting trap: init must call none.
        exports = (folder / 'kdebug_kexports.c').read_text()
        start = exports.index('void (*const kdebug_kexports[])(void) = {')
        end = exports.index('};', start) + 2
        targets = {'kdi_get_kselftest_fail': 'kdebug_test_fail',
                   'kdi_kaudit_skipped': 'kdebug_test_skipped'}
        exports = ('#include "kdebug_imports.h"\nextern void kdebug_forbidden_import(void);\n' +
                   'extern u32 kdebug_test_fail(void);\nextern int kdebug_test_skipped(int tag);\n' +
                   'void (*const kdebug_kexports[])(void) = {\n' +
                   ''.join('    (void (*)(void))' + targets.get(e['name'], 'kdebug_forbidden_import') + ',\n'
                           for e in meta['imports']) + '};\n' + exports[end:])
        (folder / 'loader_exports.c').write_text(exports)
        kernel_source = (ROOT / 'kernel/kernel.c').read_text()
        (folder / 'kernel_selftest_dispatch.h').write_text(boot_dispatch(kernel_source))
        def build_loader(module_source, call_source=ROOT / 'kernel/kdebug_call.c'):
            command([*host, ROOT / 'tools/tests/kdebug_loader_host.c', call_source,
                     module_source, ROOT / 'kernel/module_image.c', ROOT / 'kernel/module_runtime.c',
                     ROOT / 'kernel/kselftest_report.c', folder / 'loader_exports.c', '-o', exe])
        build_loader(ROOT / 'kernel/module.c')
        cases = [('off', 0, 0, 0, image), ('other-config', 2, 0, 0, image),
                 ('running', 1, 0, 1, image)]
        for fail, state in ((1, 2), (2, 8), (3, 8), (4, 7), (5, 7), (6, 8), (7, 5), (8, 10)):
            cases.append((f'failure-{fail}', 1, fail, state, image))
        for label, word, value, state in (
            ('header', 0, 0, 3), ('old-pair', 31, pair ^ 1, 4),
            ('abi', 6, meta['module_abi_gen'] ^ 1, 6), ('old-ops-v1', 33, 1, 6),
            ('future-format', 1, 2, 3), ('wrong-id', 7, 2, 3),
            ('wrong-phase', 8, 1, 3), ('missing-pair', 9, 0, 3),
            ('crc', 32, 0, 9)):
            bad = bytearray(image)
            struct.pack_into('<I', bad, word * 4, value)
            if word != 32:
                seal(bad)
            cases.append((label, 1, 0, state, bad))
        words = struct.unpack_from('<36I', image)
        for label, off, value, state in (
            ('reloc-range', words[21], words[12], 11),
            ('import-hash', words[23], 0, 12),
            ('duplicate-import-slot', words[23] + 12 + 8,
             struct.unpack_from('<I', image, words[23] + 8)[0], 12),
            ('duplicate-reloc', words[21] + 4,
             struct.unpack_from('<I', image, words[21])[0], 11),
            ('export-slot', words[25] + 4, 0x10000, 3)):
            bad = bytearray(image)
            struct.pack_into('<I', bad, off, value)
            cases.append((label, 1, 0, state, seal(bad)))
        bad = bytearray(image)
        bad[words[25] + 12:words[25] + 24] = bad[words[25]:words[25] + 12]
        cases.append(('duplicate-export', 1, 0, 3, seal(bad)))
        bad = bytearray(image)
        struct.pack_into('<I', bad, 23 * 4, words[21])
        cases.append(('table-overlap', 1, 0, 3, seal(bad)))
        # The actual loader calls this deliberately failing, side-effect-free init.
        bad = bytearray(image)
        init = words[11] + words[29]
        bad[init:init + 6] = b'\xb8\x01\x00\x00\x00\xc3'
        cases.append(('init-failed', 1, 0, 10, seal(bad)))
        for label, config, fail, state, data in cases:
            if state not in (1, 10):
                # A rejected/OFF image must never execute init. A CPU trap
                # probe makes early init observable even though real init is
                # side-effect-free and calls no imports.
                probe = bytearray(data)
                trap = b'\x0f\x0b'  # UD2: any execution fails the host run.
                assert init + len(trap) <= words[11] + words[13] + words[14]
                relocs = struct.unpack_from('<' + 'I' * words[22], image, words[21])
                assert all(r + 4 <= words[29] or r >= words[29] + len(trap) for r in relocs)
                probe[init:init + len(trap)] = trap
                data = seal(probe) if state != 9 else probe
            payload = struct.pack('<5I', config, fail, state, len(data), pair) + data
            for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                result = host32.run([str(exe)], input=payload, runner=runner)
                assert result.returncode == 0, (label, result.returncode)
            print('PASS loader', label, flush=True)
        if '--mutate' in sys.argv or '--mutate-running' in sys.argv:
            module_source = (ROOT / 'kernel/module.c').read_text()
            assignment = '    kdebug.state = KDEBUG_RUNNING;'
            assert module_source.count(assignment) == 1
            without_gate = kernel_source
            for op in ('selftest_run', 'selftest_post_exec', 'selftest_post_unicode', 'selftest_post_probe'):
                old = 'if (kdebug.state == KDEBUG_RUNNING) kdebug.ops.' + op
                assert old in without_gate
                without_gate = without_gate.replace(old, 'if (kdebug.ops.' + op + ') kdebug.ops.' + op)
            for label, module_text, kernel_text in (
                ('loader-running-state', module_source.replace(assignment, ''), kernel_source),
                ('kernel-running-gate', module_source, without_gate)):
                mutant = folder / (label + '.c')
                mutant.write_text(module_text)
                (folder / 'kernel_selftest_dispatch.h').write_text(boot_dispatch(kernel_text))
                build_loader(mutant)
                payload = struct.pack('<5I', 1, 0, 1, len(image), pair) + image
                for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                    result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
                    assert result.returncode == 1 and b'FAIL ' in result.stdout, (label, result)
                print('MUTATE', label, 'RED (runtime assertion)', flush=True)
            (folder / 'kernel_selftest_dispatch.h').write_text(boot_dispatch(kernel_source))
            call_source = (ROOT / 'kernel/kdebug_call.c').read_text()
            gate = 'return kdebug.ops.dbg_call ?'
            assert call_source.count(gate) == 1
            mutant = folder / 'kdebug-call-always-nosys.c'
            mutant.write_text(call_source.replace(gate, 'return 0 ?'))
            build_loader(ROOT / 'kernel/module.c', mutant)
            payload = struct.pack('<5I', 0, 0, 0, len(image), pair) + image
            for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
                assert result.returncode == 1 and b'FAIL ' in result.stdout, ('kdebug-call', result)
            print('MUTATE kdebug-call-always-nosys RED (runtime assertion)', flush=True)
        print('PASS init: candidate only, imports=0; loader cases=' + str(len(cases)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
