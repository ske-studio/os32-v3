"""T5c-3: actual module runtime and core return order, ILP32.
No CPU exception is simulated by a host signal stack. Hardware entry is PM's
NP21/W check; shm-user covers exact PTE/caller/ESP0 restoration separately.
"""
CHECK = dict(name='v86-module-host', tier=1,
    covers=['kernel/module/module_runtime.c', 'kernel/module/module_runtime.h', 'kernel/module/module_image.c', 'kernel/module/module_image.h', 'kernel/module/module_schema.h', 'mm/pgalloc.h', 'include/types.h', 'include/memmap.h', 'fs/vfs.c', 'fs/vfs.h', 'drivers/base/dev.c', 'drivers/base/dev.h', 'drivers/block/disk.h', 'drivers/ata/atapi.h', 'drivers/block/fdc.h', 'include/console.h', 'include/kbd.h', 'drivers/input/kbd.h', 'tools/module_schema.py', 'tools/gen_module_schema.py', 'lib/kstring.h', 'tools/tests/module_runtime_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/module_runtime_tdd.md', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/tests/host32.py', 'kernel/v86/v86_module.c', 'kernel/v86/v86_module.h', 'kernel/v86/v86_body.c', 'include/config.h', 'sdk/include/os32/os32_generations.h', 'include/bootinfo.h', 'tools/prototypes/v86_imports.def', 'tools/gen_sqlite_imports.py', 'tools/tests/v86_module_host.c', 'build/v86-module.ld', 'kernel/v86/v86.c', 'kernel/v86/v86_entry.asm', 'kernel/v86/v86_mem.c', 'kernel/v86/v86_mem.h', 'kernel/v86/v86_snapshot.c', 'kernel/v86/v86_bios.c', 'kernel/v86/v86_bios_*.inc', 'kernel/v86/v86_bios.h', 'kernel/v86/v86_io.c', 'kernel/v86/v86_io.h', 'kernel/v86/v86_pic.c', 'kernel/v86/v86_pic.h', 'kernel/v86/v86_kbd.c', 'kernel/v86/v86_kbd.h', 'kernel/module/module.h', 'arch/x86/kernel/double_fault.c', 'arch/x86/kernel/tss.h', 'mm/paging.h', 'kernel/kselftest.h', 'gfx/gfx.h', 'drivers/block/loop_dev.h', 'include/pc98.h', 'tools/mkmod.py', 'tools/gen_kdebug_imports.py', 'sdk/kapi.json', 'build/out/v86/*', 'build/out/v86.mod', 'build/out/vmkernel.lz4', 'build/out/kernel.elf', 'drivers/video/kcg/kcg_module.h', 'fs/vfs_device.h', 'platform/pc98/boot_names.h', 'platform/pc98/boot_names.c', 'drivers/ata/ide.h', 'kernel/console/display_restore.h', 'include/platform_display.h', 'platform/pc98/display.c', 'tools/tests/kernel_includes.py', 'kernel/module/kdebug_imports.def', 'lib/sqlite3/os32_sqlite_vfs.c', 'tools/prototypes/sqlite_imports.def'],
    needs=['make:all'], budget=3.830264)

import os
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import host32
from test_module_runtime import FLAGS, INCLUDES, image
from test_module_image import build_validator, validate, fnv, crc, word
ROOT = Path(__file__).resolve().parents[2]
CASES = ['normal-return', 'abort-before-PTE-return', 'exception-trusted-landing', 'prefix-failure']

def product_image(folder):
    """Real mkmod product: PAIR/phase and relocation, with write-free rejection."""
    spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
    metadata = json.loads((ROOT / 'build/out/v86/v86_imports.json').read_text())
    exceptional = {e['name'] for e in metadata['imports'] if e['context'] == 'normal,exception'}
    assert exceptional == {e['name'] for e in metadata['imports']
                           if (e['name'].startswith('loop_dev_') and e['name'] not in
                               ('loop_dev_attach', 'loop_dev_detach')) or
                           e['name'] in ('vfs_read_fd', 'vfs_seek', 'kmemcpy',
                               'console_hw_cursor_enable', 'palette_init', 'snd_set_master',
                               'tss_iomap_deny_all', 'v86_last_gp_cs', 'v86_last_gp_ip',
                               'v86_debug_ops', 'v86_cui_display_restore')}
    data = (ROOT / 'build/out/v86.mod').read_bytes()
    kernel = (ROOT / 'build/out/vmkernel.lz4').read_bytes()
    # Use the existing packer for the boot loader's paired image CRC.
    from sys import path
    path.insert(0, str(ROOT / 'tools'))
    import mkmod
    pair = mkmod.pair_crc(kernel)
    rebuilt, stats = mkmod.build((ROOT / 'build/out/v86/v86.elf').read_bytes(), metadata,
                                 kernel, spec['version'], spec['generations']['memory_layout'],
                                 module_id=8)
    assert rebuilt == data
    core = mkmod.Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    body = mkmod.Elf((ROOT / 'build/out/v86/v86.elf').read_bytes())
    for name in ('v86_module_init', 'v86_pic_reset', 'v86_kbd_reset'):
        assert name not in core.symbols and name in body.symbols, name
    for name in ('v86_run_limit', 'v86_enter', 'v86_exit_to_kernel', 'v86_longjmp_out', 'v86_runtime_end', 'v86_session_end', 'paging_v86_restore', 'v86_bios_save_real', 'v86_bios_restore_real', 'bootinfo_capture', 'double_fault_init', 'double_fault_entry'):
        assert name in core.symbols, name
    for name in ('v86_session', 'kernel_tss'):
        assert name in core.symbols and name not in body.symbols, name
    symbols = subprocess.check_output(['nm', '-a', ROOT / 'build/out/kernel.elf'], text=True)
    assert ' df_tss\n' in symbols and ' df_stack\n' in symbols
    exe = build_validator(folder)
    contract = dict(kapi=spec['version'], layout=spec['generations']['memory_layout'],
                    abi=metadata['module_abi_gen'], pair=pair, ops=1, img_valid=1, module_id=8,
                    imports=[(fnv(e['name']), e['type_ver'], e['kind']) for e in metadata['imports']],
                    exports=[(1, fnv('initialize'), 1)])
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        def check(payload, rc):
            result = validate(exe, payload, relocate_base=0x1200000, runner=runner, **contract)
            assert result.returncode == bool(rc), result
            assert result.stdout == (b'GREEN\n' if not rc else f'RED {rc:02}\n'.encode()), result
        check(data, 0)
        for field, value, rc in ((9, 0, 3), (31, pair ^ 1, 4), (8, 1, 3)):
            bad = bytearray(data)
            word(bad, field, value)
            check(crc(bad), rc)
        check(data[:-1], 3)
        bad = bytearray(data)
        bad[-1] ^= 1
        check(bad, 9)
    print('PASS product V86: paired POST_ROOT, relocate, truncated/corrupt rejection, core dependencies', flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='v86-module-') as td:
        folder = Path(td)
        product_image(folder)
        exe = folder / 'v86-module'
        source = (ROOT / 'kernel/v86/v86_mem.c').read_text()
        def build(text):
            (folder / 'v86_mem.c').write_text(text)
            subprocess.run(['gcc', *FLAGS, '-Wno-unused-parameter', '-I' + str(folder),
                            *INCLUDES, '-I' + str(ROOT / 'gfx'),
                            '-I' + str(ROOT / 'build/out/v86'),
                            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                            ROOT / 'tools/tests/v86_module_host.c',
                            ROOT / 'kernel/module/module_runtime.c', ROOT / 'kernel/module/module_image.c',
                            ROOT / 'fs/vfs.c', ROOT / 'drivers/base/dev.c', '-o', exe], check=True)
        def run(modes, expected=0):
            for mode in modes:
                payload = struct.pack('<2I', 10 if mode == 3 else 0, mode) + image()
                for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                    result = host32.run([str(exe)], input=payload, runner=runner, capture_output=True)
                    assert result.returncode == expected, (CASES[mode], runner, result)
                    if expected:
                        assert b'FAIL returned == free_before && !retired' in result.stdout, result
                print(('MUTATE RED ' if expected else 'PASS ') + CASES[mode], flush=True)
        build(source)
        run(range(len(CASES)))
        if '--mutate' in sys.argv:
            # Compile the genuine session end with an extra release before
            # PTE restoration. Actual module_abort then reclaims/retires early.
            anchor = '    v86_module_session_release();'
            assert source.count(anchor) == 2
            edge = '    v86_restore_mismatch += paging_v86_restore(&v86_session);'
            assert source.count(edge) == 1
            mutant = source.replace(edge, anchor + '\n' + edge)
            assert mutant.count(anchor) == source.count(anchor) + 1
            build(mutant)
            run([1], expected=1)
            print('MUTATE owner-retire-before-return RED (runtime assertion; compiled)', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_v86_module.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
