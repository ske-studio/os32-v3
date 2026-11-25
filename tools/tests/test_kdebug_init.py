"""S1a: actual init has no imports; actual loader rejects and recovers.
記録: tools/tests/kdebug_init_tdd.md
"""
CHECK = dict(name='kdebug-init-host', tier=1,
    covers=['sys/platform/pc98/module_pair.c', 'sys/include/platform_module_pair.h', 'sys/platform/pc98/boot_capture.c', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/dev/char/kdebug/probe.c', 'sys/kern/kdebug/probe_index.c', 'sys/kern/kdebug/probe_wire.h', 'sys/include/debug_probe.h', 'sys/kern/kdebug/hwb_access.c', 'sys/kern/kdebug/hwb_wire.h', 'sys/dev/pci/pci_hwb.h', 'sys/kern/exec/exec_hwb.h', 'sys/arch/x86/kdebug/selftest_exec.c', 'sys/kern/exec/ring3_str.h', 'sys/arch/x86/kdebug/selftest_lease.c', 'sys/kern/module/module_schema.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'build/kdebug.mk', 'build/module.ld', 'sys/include/config.h', 'sys/include/types.h', 'sys/include/bootinfo.h', 'sys/include/kprintf.h', 'sys/include/memmap.h', 'sys/kern/kdebug/kdebug_module.c', 'sys/kern/module/module_runtime.c', 'sys/kern/module/module_runtime.h', 'sys/kern/module/module.h', 'sys/kern/module/module_image.c', 'sys/kern/module/module_image.h', 'sys/kern/mm/pgalloc.h', 'sys/kern/init/sysconfig.h', 'sys/kern/module/kdebug_imports.def', 'sys/platform/pc98/kdebug/nic_trace.c', 'sys/platform/pc98/kdebug/nic_trace.h', 'sys/dev/net/e82557/e82557.h', 'sys/fs/vfs.h', 'tools/tests/nic_trace_host.c', 'sys/platform/pc98/kdebug/kdebug_main.c', 'sys/dev/video/trident/diag/tri_command_wire.h', 'sys/dev/video/trident/diag/trident_command_test.h', 'sys/dev/video/trident/diag/trident_command_test.c', 'drivers/video/trident/trident*.h', 'lib/serial_diag.h', 'sys/platform/pc98/kdebug/recover.c', 'kdebug/recover_common/*.inc', 'kdebug/recover_plan/*.inc', 'kdebug/recover_gf_stage/*.inc', 'sys/dev/video/geforce/diag/recover.h', 'sys/platform/pc98/kdebug/recover_profile.c', 'sys/dev/video/geforce/diag/recover_gf_locate.c', 'sys/dev/pci/pci_locate.c', 'sys/dev/pci/pci_locate.h', 'sys/kern/kdebug/recover_wire.h', 'sys/dev/video/geforce/diag/gfinit.h', 'sys/dev/char/serial.h', 'sys/dev/char/serial_plan.h', 'sys/kern/console/display_restore.h', 'sys/kern/module/kdebug_call.c', 'sys/platform/pc98/kdebug/kselftest.c', 'kdebug/kselftest/**', 'sys/kern/kdebug/selftest_internal.h', 'sys/kern/kselftest.h', 'sys/platform/pc98/kdebug/crt_diag.c', 'tools/gen_kdebug_imports.py', 'tools/mkmod.py', 'tools/tests/kdebug_init_host.c', 'sys/platform/pc98/kdebug/kpeek.h', 'tools/tests/kdebug_loader_host.c', 'tools/tests/kdebug_init_tdd.md', 'tools/tests/test_mkmod.py', 'tools/tests/host32.py', 'sdk/kapi.json', 'tools/tests/module_image_host.c', 'tools/tests/test_module_image.py', 'kdebug/generated/**', 'sys/platform/pc98/kdebug/v86_gcap.c', 'sys/platform/pc98/kdebug/v86_gcap_math.c', 'sys/platform/pc98/kdebug/kpeek.c', 'sys/platform/pc98/kernel.c', 'sys/kern/kselftest_report.c', 'sys/kern/mm/memory_boot.h', 'sys/arch/x86/kdebug/kdebug_window.c', 'sys/arch/x86/kdebug/kdebug_window.h', 'sys/dev/hwdev/generated/hwdev_core.c', 'sys/dev/hwdev/generated/hwdev_contract.h', 'sys/dev/hwdev/hwdev_private.h', 'sys/dev/hwdev/hwdev_module.h', 'sys/kern/kdebug/recover_virtual.h', 'sys/kern/kdebug/recover_virtual.c', 'sys/kern/kdebug/selftest_kbd_inject.c', 'sys/include/kselftest_probe.h', 'sys/platform/pc98/kdebug/recover_cleanup.h', 'sys/kern/kdebug/recover_diag.h', 'sys/include/pci_dev_contract.h', 'sys/include/pci_bind_contract.h', 'sys/include/platform_display.h', 'sys/platform/pc98/display_private.h', 'sys/platform/pc98/display.c', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py'],
    needs=['make:all'], budget=11.6,
    basis=['KDBG_PROBE_DESIGN §2', '[ABI]'],
)

from kernel_boot_source import kernel_source as read_kernel_source
from kernel_includes import include_flags
import argparse
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
INCLUDES = (['-I' + str(ROOT / p) for p in ('kdebug',)] + include_flags(ROOT))


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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--mutate-running', action='store_true')
    parser.add_argument('--pair-baseline', type=Path,
                        help='saved vmkernel.lz4/kdebug.bin before import migration')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kdebug-init-') as td:
        folder = Path(td)
        meta = gen.generate(gen.load_def(), folder, gen.LAST_STAGE)
        for generated in (ROOT / 'kdebug/generated').iterdir():
            if generated.suffix in ('.c', '.h', '.S', '.json'):
                assert generated.read_bytes() == (folder / generated.name).read_bytes(), generated
        cross = Path(os.environ['CROSS_DIR']) / 'bin'
        inc = [*INCLUDES, '-I' + td, '-I' + str(ROOT/'hwdev/generated')]
        host = ['gcc', *FLAGS, *inc, '-nostdlib', '-static', '-no-pie']
        exe = folder / 'init'
        harness = ROOT / 'tools/tests/kdebug_init_host.c'
        source = ROOT / 'sys/platform/pc98/kdebug/kdebug_main.c'
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
            trace_source = (ROOT / 'sys/platform/pc98/kdebug/nic_trace.c').read_text()
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
                harness_copy.write_text(trace_harness.replace('../../sys/platform/pc98/kdebug/nic_trace.c', str(mutant)))
                command([*host, '-I' + str(ROOT / 'kdebug'), harness_copy, '-o', trace_exe])
                for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                    assert host32.run([str(trace_exe)], runner=runner).returncode == 1, label
                print('MUTATE', label, 'RED', flush=True)
        # Build the real module independently of pre-existing product artifacts.
        objects = []
        for src in (ROOT / 'sys/platform/pc98/kdebug/crt_diag.c', ROOT / 'sys/platform/pc98/kdebug/kselftest.c',
                    ROOT / 'sys/kern/kdebug/selftest_kbd_inject.c',
                    ROOT / 'sys/arch/x86/kdebug/selftest_lease.c',
                    ROOT / 'sys/arch/x86/kdebug/selftest_exec.c',
                    ROOT / 'sys/platform/pc98/kdebug/nic_trace.c', ROOT / 'sys/dev/video/trident/diag/trident_command_test.c',
                    ROOT / 'sys/platform/pc98/kdebug/v86_gcap.c',
                    ROOT / 'sys/platform/pc98/kdebug/v86_gcap_math.c', ROOT / 'sys/dev/char/kdebug/probe.c', ROOT / 'sys/kern/kdebug/probe_index.c', ROOT / 'sys/kern/kdebug/hwb_access.c', ROOT / 'sys/platform/pc98/kdebug/kpeek.c', ROOT / 'sys/arch/x86/kdebug/kdebug_window.c',
                    ROOT / ROOT / 'sys/platform/pc98/kdebug/recover.c',
                    ROOT / 'sys/platform/pc98/kdebug/recover_profile.c', ROOT / 'sys/kern/kdebug/recover_virtual.c',
                    ROOT / 'sys/dev/video/geforce/diag/recover_gf_locate.c', ROOT / 'sys/dev/pci/pci_locate.c',
                    ROOT / 'sys/dev/hwdev/generated/hwdev_core.c', source,
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
        kernel_source = read_kernel_source(ROOT)
        (folder / 'kernel_selftest_dispatch.h').write_text(boot_dispatch(kernel_source))
        def build_loader(module_source, call_source=ROOT / 'sys/kern/module/kdebug_call.c'):
            command([*host, ROOT / 'tools/tests/kdebug_loader_host.c', call_source,
                     module_source, ROOT / 'sys/kern/module/module_image.c', ROOT / 'sys/kern/module/module_runtime.c',
                     ROOT / 'sys/kern/kselftest_report.c', folder / 'loader_exports.c', '-o', exe])
        build_loader(ROOT / 'sys/kern/kdebug/kdebug_module.c')
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
        bad = bytearray(image)
        struct.pack_into('<I', bad, 10 * 4, mkmod.POLICIES[1]['mem_max'] + 4096)
        cases.append(('core-body-over-limit', 1, 0, 3, seal(bad)))
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
        if args.pair_baseline:
            # Use unmodified product images, not a synthetic CRC-bit flip.
            # The current real loader must reject the old contract at the ABI
            # gate, before importing slots or executing init.
            current = ROOT / 'build/out'
            old_pair = mkmod.pair_crc((args.pair_baseline / 'vmkernel.lz4').read_bytes())
            new_pair = mkmod.pair_crc((current / 'vmkernel.lz4').read_bytes())
            old_image = (args.pair_baseline / 'kdebug.bin').read_bytes()
            new_image = (current / 'kdebug.bin').read_bytes()
            assert old_pair != new_pair
            assert struct.unpack_from('<I', old_image, 31 * 4)[0] == old_pair
            assert struct.unpack_from('<I', new_image, 31 * 4)[0] == new_pair
            for label, data, boot_pair, state in (
                    ('product-current', new_image, new_pair, 1),
                    ('product-old-module-abi', old_image, new_pair, 6),
                    ('product-old-boot-crc', new_image, old_pair, 4)):
                payload = struct.pack('<5I', 1, 0, state, len(data), boot_pair) + data
                for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                    result = host32.run([str(exe)], input=payload, runner=runner)
                    assert result.returncode == 0, (label, result.returncode)
                print('PASS loader', label, f'boot pair={boot_pair:08x}', flush=True)
        if '--mutate' in sys.argv or '--mutate-running' in sys.argv:
            module_source = (ROOT / 'sys/kern/kdebug/kdebug_module.c').read_text()
            assignment = '    kdebug.state = KDEBUG_RUNNING;'
            assert module_source.count(assignment) == 1
            without_gate = kernel_source
            for op in ('selftest_run', 'selftest_post_exec', 'selftest_post_unicode', 'selftest_post_probe'):
                old = 'if (kdebug.state == KDEBUG_RUNNING) kdebug.ops.' + op
                assert without_gate.count(old) == 1
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
            call_source = (ROOT / 'sys/kern/module/kdebug_call.c').read_text()
            gate = 'return kdebug.ops.dbg_call ?'
            assert call_source.count(gate) == 1
            mutant = folder / 'kdebug-call-always-nosys.c'
            mutant.write_text(call_source.replace(gate, 'return 0 ?'))
            build_loader(ROOT / 'sys/kern/kdebug/kdebug_module.c', mutant)
            payload = struct.pack('<5I', 0, 0, 0, len(image), pair) + image
            for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
                assert result.returncode == 1 and b'FAIL ' in result.stdout, ('kdebug-call', result)
            print('MUTATE kdebug-call-always-nosys RED (runtime assertion)', flush=True)
        print('PASS init: candidate only, imports=0; loader cases=' + str(len(cases)))
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kdebug_init.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
