"""D4: real FatFs on memory media; no emulator or physical disk access."""
CHECK = dict(name='fatfs-read-cache-host', tier=1,
             covers=['tools/tests/fatfs_read_cache_host.c',
                     'tools/tests/kernel_includes.py',
                     'fs/fat/fatfs_vfs.c', 'fs/fat/fatfs_vfs_read_cache.inc',
                     'fs/fat/fatfs/ff.c', 'fs/fat/fatfs/ff.h',
                     'fs/fat/fatfs/ffconf.h', 'fs/fat/fatfs/diskio.h',
                     'drivers/block/pc98pt.c'],
             needs=[], budget=60)
import os
import pathlib
import subprocess
import sys
import tempfile

from kernel_includes import include_flags

ROOT = pathlib.Path(__file__).resolve().parents[2]
WRAPPED = ('open', 'close', 'read', 'lseek', 'mount')
MUTANTS = (
    ('no-reuse', 'if (rc->slots[i].valid && kstrcmp',
     'if (0 && rc->slots[i].valid && kstrcmp', 'performance'),
    ('no-invalidate', '        rc_drop(rc, &rc->slots[i]);',
     '        (void)rc;', 'mutations'),
    ('rank-holes', '                other->rank--;',
     '                (void)other;', 'lru'),
    ('no-seek', 'if (f_tell(&slot->fil) != offset)',
     'if (0 && f_tell(&slot->fil) != offset)', 'positions'),
    ('no-generation', 'if (gen != rc->media_gen)',
     'if (0 && gen != rc->media_gen)', 'generations'),
    ('ignore-read-error', 'if (fr != FR_OK) return rc_failure(rc, slot, fr);\n    return FR_OK;',
     'if (0 && fr != FR_OK) return rc_failure(rc, slot, fr);\n    return FR_OK;', 'errors'),
)


def main():
    with tempfile.TemporaryDirectory(prefix='fatfs-read-cache-') as tmp:
        tmp = pathlib.Path(tmp)
        source = tmp / 'instrument.c'
        source.write_text('#include "fs/fat/fatfs/ff.c"\n'
                          'void *test_get_fat_address(void) { return (void *)get_fat; }\n')
        flags = ['gcc', '-std=gnu11', '-g', '-O0', '-D__cdecl=',
                 '-I' + str(ROOT), *include_flags(ROOT)]
        subprocess.run([*flags, '-finstrument-functions',
                        *['-Df_' + n + '=real_f_' + n for n in WRAPPED],
                        '-c', str(source), '-o', str(tmp / 'ff.o')], check=True)
        exe = tmp / 'read-cache'
        subprocess.run([*flags, '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-Wno-sign-compare',
                        str(ROOT / 'tools/tests/fatfs_read_cache_host.c'),
                        str(tmp / 'ff.o'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        cache = ROOT / 'fs/fat/fatfs_vfs_read_cache.inc'
        if '--mutate' in sys.argv:
            original = cache.read_text()
            # Copy the whole wrapper and include, rebasing only local includes.
            wrapper = (ROOT / 'fs/fat/fatfs_vfs.c').read_text()
            wrapper = wrapper.replace('"../vfs.h"', '"vfs.h"').replace(
                '"../vfs_name_rules.inc"', '"vfs_name_rules.inc"')
            (tmp / 'fatfs_vfs.c').write_text(wrapper)
            fixture = (ROOT / 'tools/tests/fatfs_read_cache_host.c').read_text()
            fixture = fixture.replace('"../../drivers/block/pc98pt.c"',
                                      '"drivers/block/pc98pt.c"').replace(
                '"../../fs/fat/fatfs_vfs.c"', '"fatfs_vfs.c"')
            (tmp / 'fixture.c').write_text(fixture)
            for name, old, new, case in MUTANTS:
                assert original.count(old) == 1, name
                (tmp / 'fatfs_vfs_read_cache.inc').write_text(original.replace(old, new))
                subprocess.run([*flags, '-Wall', '-Werror',
                                str(tmp / 'fixture.c'), str(tmp / 'ff.o'),
                                '-o', str(tmp / 'mutant')], check=True)
                result = subprocess.run([str(tmp / 'mutant'), case],
                                        text=True, capture_output=True)
                assert result.returncode == 1 and 'FAIL ' in result.stderr, (
                    name, result.stdout, result.stderr)
                print('MUTANT', name, 'runtime RED', flush=True)
        layout = tmp / 'layout.c'
        layout.write_text('#include "fs/fat/fatfs_vfs.c"\n'
                          '_Static_assert(sizeof(FIL) == 40, "FIL");\n'
                          '_Static_assert(sizeof(FatReadSlot) == 304, "slot");\n'
                          '_Static_assert(sizeof(FatReadCache) == 1220, "cache");\n')
        cross = pathlib.Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), '-std=gnu11', '-m32', '-march=i386',
                        '-ffreestanding', '-fsyntax-only', '-I' + str(ROOT),
                        *include_flags(ROOT), str(layout)], check=True)
        print('PASS i386 layout FIL=40 slot=304 cache=1220 bytes', flush=True)
        # Use the actual include, but change only the test configuration.
        if cache.exists():
            guard = tmp / 'guard.c'
            guard.write_text('#include "fs/fat/fatfs/ff.h"\n'
                             '#undef FF_FS_LOCK\n#define FF_FS_LOCK 1\n'
                             '#include "fs/fat/fatfs_vfs_read_cache.inc"\n')
            result = subprocess.run([*flags, '-fsyntax-only', str(guard)],
                                    text=True, capture_output=True)
            assert result.returncode != 0
            assert 'requires FF_FS_LOCK=0' in result.stderr
            print('PASS FF_FS_LOCK guard', flush=True)


def run(ctx):
    return subprocess.call([sys.executable, '-B', str(ROOT / 'tools/tests/test_fatfs_read_cache.py'),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
