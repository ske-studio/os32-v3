"""R5 resident KCG1 provider; runtime mutations use isolated source copies."""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHECK = dict(name='kcg-file-host', tier=1, needs=[], budget=15,
    covers=['tools/tests/test_kcg_file.py', 'tools/tests/kcg_file_host.c',
            'tools/tests/host32.py', 'tools/tests/cc_cache.py', 'tools/tests/kernel_includes.py',
            'build/config.mk', 'include/**', 'sdk/include/**', 'arch/x86/**',
            'kernel/console/font_source*', 'kernel/console/font_guard.h', 'kernel/gui.h',
            'kernel/module/module_runtime.h', 'kernel/module/module_image.h',
            'kernel/module/module_schema.h',
            'drivers/video/kcg/kcg_file*', 'drivers/video/kcg/kcg_module.h',
            'fs/**', 'exec/*.h', 'mm/*.h', 'lib/kstring.h'])
FLAGS = ['-m32', '-std=gnu11', '-ffreestanding', '-fno-builtin', '-fno-pie',
         '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-D__cdecl=',
         '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
MUTATIONS = [
 ('kernel/console/font_source.c', 'path, FONT_RESIDENT_OWNER,', 'path, GUI_SHELL_OWNER,', 'shell owner'),
 ('kernel/console/font_source.c', 'if (s->owner_gen != FONT_RESIDENT_GENERATION)', 'if (0)', 'resident generation'),
 ('drivers/video/kcg/kcg_file.c', 'size != KCG_FILE_HEADER + KCG_FILE_PAYLOAD ||', '0 ||', 'exact length'),
 ('drivers/video/kcg/kcg_file.c', 'read_le32(header + 8) != 0', '0', 'flags'),
 ('drivers/video/kcg/kcg_file.c', 'font_lease_resident_validate(id))', '0)', 'VFS generation validation'),
 ('drivers/video/kcg/kcg_file.c', 'static void invalidate(void) {', 'static void invalidate(void) { kmemset(resident, 0, KCG_FILE_PAYLOAD);', 'invalidate source'),
]

def build_run(root, tmp):
    exe = tmp / 'kcg-host'
    subprocess.run(['gcc', *FLAGS, '-DHOST_GUI_SHELL_OWNER=' + re.search(r'^#define\s+GUI_SHELL_OWNER\s+(\d+)', (root / 'kernel/gui.h').read_text(), re.M)[1], *include_flags(root),
                    str(root / 'tools/tests/kcg_file_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], runner='qemu').returncode


def main():
    with tempfile.TemporaryDirectory(prefix='kcg-r5-') as name:
        tmp = pathlib.Path(name)
        rc = build_run(ROOT, tmp)
        print('R5 baseline rc=', rc, flush=True)
        if rc: return rc
        cross = pathlib.Path('/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc')
        if cross.exists():
            subprocess.run([str(cross), '-std=gnu11', '-ffreestanding', '-Wall', '-Wextra', '-Werror',
                            *include_flags(ROOT), '-c', str(ROOT / 'drivers/video/kcg/kcg_file.c'),
                            '-o', str(tmp / 'kcg-ppc.o')], check=True)
            print('PPC kcg_file -c PASS')
        else: print('SKIP PPC cross compiler unavailable')
        if '--mutate' in sys.argv:
            copy = tmp / 'tree'
            for directory in ('include', 'sdk/include', 'arch', 'kernel', 'drivers', 'fs', 'exec', 'mm', 'lib'):
                shutil.copytree(ROOT / directory, copy / directory, ignore=shutil.ignore_patterns('out', 'sdk', '__pycache__'), dirs_exist_ok=True)
            (copy / 'tools/tests').mkdir(parents=True)
            shutil.copy2(ROOT / 'tools/tests/kcg_file_host.c', copy / 'tools/tests')
            for rel, old, new, label in MUTATIONS:
                p = copy / rel
                original = p.read_text()
                assert original.count(old) == 1, label
                p.write_text(original.replace(old,new))
                rc = build_run(copy, tmp)
                p.write_text(original)
                print('MUTATION', label, 'RED' if rc else 'GREEN', 'rc=', rc, flush=True)
                if rc <= 0: return 1
    return 0

if __name__ == '__main__': sys.exit(main())

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_kcg_file.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
