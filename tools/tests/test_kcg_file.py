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
            'fs/**', 'exec/*.h', 'mm/*.h', 'lib/kstring.h', 'lib/lz4.c', 'lib/lz4.h'])
FLAGS = ['-m32', '-std=gnu11', '-ffreestanding', '-fno-builtin', '-fno-pie',
         '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-D__cdecl=',
         '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
MUTATIONS = [
 ('kernel/console/font_source.c', 'path, FONT_RESIDENT_OWNER,', 'path, GUI_SHELL_OWNER,', 'shell owner'),
 ('kernel/console/font_source.c', 'if (s->owner_gen != FONT_RESIDENT_GENERATION)', 'if (0)', 'resident generation'),
 ('drivers/video/kcg/kcg_file.c', 'flags == 0 && compressed != KCG_FILE_PAYLOAD', '0', 'exact length'),
 ('drivers/video/kcg/kcg_file.c', 'flags > 1', '0', 'flags'),
 ('drivers/video/kcg/kcg_file.c', 'font_lease_resident_validate(id))', '0)', 'VFS generation validation'),
 ('drivers/video/kcg/kcg_file.c', 'static void invalidate(void) {', 'static void invalidate(void) { kmemset(resident, 0, KCG_FILE_PAYLOAD);', 'invalidate source'),
 ('drivers/video/kcg/kcg_file.c', 'size < KCG_FILE_HEADER', '0', 'short header'),
 ('drivers/video/kcg/kcg_file.c', 'read_le32(header + 4) != KCG_FILE_PAYLOAD', '0', 'payload size'),
 ('drivers/video/kcg/kcg_file.c', 'read_le32(header + 12) != 0', '0', 'reserved'),
 ('drivers/video/kcg/kcg_file.c', 'compressed == 0 || compressed > KCG_FILE_COMPRESSED_MAX', '0', 'compressed bound'),
 ('drivers/video/kcg/kcg_file.c', 'flags == 1 && lz4_decode', 'flags == 0 && lz4_decode', 'compression branch'),
 ('drivers/video/kcg/kcg_file.c', '!= (int)KCG_FILE_PAYLOAD) goto done;', '< 0) goto done;', 'decoded length'),
 ('drivers/video/kcg/kcg_file.c', 'data[KCG_FILE_KANJI_BYTES + i] != 1', '0', 'kanji fetched'),
 ('drivers/video/kcg/kcg_file.c', 'data[KCG_FILE_ANK_OFFSET + KCG_FILE_ANK_BYTES + i] != 1', '0', 'ank fetched'),
 ('drivers/video/kcg/kcg_file.c', 'if (input) kfree(input);', 'if (input) { }', 'input cleanup'),
 ('drivers/video/kcg/kcg_file.c', 'if (data) kfree(data);', 'if (data) { }', 'output cleanup'),
 ('drivers/video/kcg/kcg_file.c', 'const char *path = arg;', 'const char *path = arg; if (resident) return OS32_ERR_BUSY;', 'reload'),
 ('drivers/video/kcg/kcg_file.c', 'if (rc) { kfree(data); return rc; }', 'if (0) { kfree(data); return rc; }', 'close before publish'),
 ('lib/lz4.c', 'if (lit_len > (int)(ip_end - ip)) return -1;', 'if (0) return -1;', 'literal input bound'),
 ('lib/lz4.c', 'if (ip + LZ4_OFFSET_BYTES > ip_end) return -1;', 'if (0) return -1;', 'truncated offset'),
 ('lib/lz4.c', 'if (match_len > (int)(op_end - op)) return LZ4_ERR_OUTPUT;', 'if (0) return LZ4_ERR_OUTPUT;', 'match output bound'),
 ('lib/lz4.c', 'if (lit_len > (int)(op_end - op)) return LZ4_ERR_OUTPUT;', 'if (0) return LZ4_ERR_OUTPUT;', 'literal output bound'),
 ('lib/lz4.c', '*op++ = match_src[i];', '*op++ = match_src[0];', 'overlap copy'),
 ('lib/lz4.c', 'if (offset == 0) return -1;', 'if (0) return -1;', 'offset zero'),
 ('lib/lz4.c', 'if (op - dst < offset) return -1;', 'if (0) return -1;', 'offset range'),
]

def build_run(root, tmp):
    exe = tmp / 'kcg-host'
    subprocess.run(['gcc', *FLAGS, '-DHOST_GUI_SHELL_OWNER=' + re.search(r'^#define\s+GUI_SHELL_OWNER\s+(\d+)', (root / 'kernel/gui.h').read_text(), re.M)[1], '-I' + str(tmp), *include_flags(root),
                    str(root / 'tools/tests/kcg_file_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], runner='qemu').returncode


def main():
    with tempfile.TemporaryDirectory(prefix='kcg-r5-') as name:
        tmp = pathlib.Path(name)
        try:
            import lz4.block
        except ImportError:
            print('SKIP kcg-file-host: Python lz4.block unavailable (not PASS)')
            return 77
        payload = bytearray(295940)
        payload[282752:291588] = bytes([1])*8836
        payload[-256:] = bytes([1])*256
        payload[0] = 0xa5
        payload[291588+65*16] = 0x5a
        variants = {'good':payload, 'short':payload[:-1], 'long':payload+bytes(1)}
        bad = bytearray(payload); bad[282752]=0; variants['bad_fetched']=bad
        bad = bytearray(payload); bad[-1]=0; variants['bad_ank']=bad
        arrays = {'expected_payload':payload}
        arrays.update({'packed_'+k:lz4.block.compress(bytes(v), store_size=False) for k,v in variants.items()})
        # Literal-only raw block is legal and larger than P.
        n=len(payload)-15
        arrays['packed_expanded']=bytes([0xf0])+bytes([255])*(n//255)+bytes([n%255])+payload
        (tmp/'kcg_fixtures.h').write_text('\n'.join('static const unsigned char '+k+'[]={'+','.join(map(str,v))+'};' for k,v in arrays.items()))
        rc = build_run(ROOT, tmp)
        print('R5 baseline rc=', rc, flush=True)
        if rc: return rc
        import os
        found = shutil.which('powerpc-elf-gcc')
        cross = pathlib.Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        if not cross.exists() and found: cross = pathlib.Path(found)
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
