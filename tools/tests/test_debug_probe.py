"""A1 resident probe registry, ownership and synchronous slot publication (ILP32)."""
CHECK = dict(name='debug-probe', tier=1, needs=[], budget=4,
    covers=['include/debug_probe.h', 'kernel/module/debug_probe.c',
            'kernel/module/module_runtime.h', 'kernel/module/module_image.h',
            'kernel/module/module_schema.h', 'include/types.h', 'include/memmap.h',
            'sdk/include/os32/os32_kapi_shared.h', 'lib/kstring.h', 'lib/kstring_c.c',
            'tools/tests/debug_probe_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk', 'build/kernel.mk'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'kernel/module/debug_probe.c'
MUTATIONS = [
    ('slot-range', '!contains(backing, r->pages * MODULE_PAGE_SIZE, slot, size)', '(0 && backing)'),
    ('name-range', 'if (!contains(r->ro, r->ro_size, name, i + 1)) return 0;',
     'if (0 && !contains(r->ro, r->ro_size, name, i + 1)) return 0;'),
    ('duplicate-name', '!kstrcmp(table[i].name, table[j].name)', '0'),
    ('starting-visible', 'if (!e->slot || !r || r->state != MODULE_RUNNING) continue;',
     'if (!e->slot || !r) continue;'),
    ('callback-text', '!module_text_contains(r, (u32)(uptr)a->callback)', '0'),
    ('release-order', '    /* Remove every executable pointer before releasing callback ownership. */',
     '    if (attached) {\n'
     '        module_ref_release(callback_owner, MODULE_REF_REGISTRATION);\n'
     '        attached = 0;\n'
     '    }'),
    ('registration-rollback', 'while (acquired) {', 'while (0 && acquired) {'),
]


def main():
    with tempfile.TemporaryDirectory(prefix='debug-probe-') as td:
        folder = Path(td)
        source = SOURCE.read_text()
        flags = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
                 '-Wextra', '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
                 '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                 *include_flags(ROOT)]
        variants = [('normal', source)]
        if '--mutate' in sys.argv:
            for label, old, new in MUTATIONS:
                assert source.count(old) == 1, label
                variants.append((label, source.replace(old, new)))
        for label, code in variants:
            unit = folder / 'debug_probe.c'
            unit.write_text(code)
            exe = folder / label
            subprocess.run([*flags, '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', ROOT / 'tools/tests/debug_probe_host.c',
                            unit, ROOT / 'lib/kstring_c.c', '-o', exe], check=True)
            for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
                result = host32.run([str(exe)], runner=runner, capture_output=True)
                if label == 'normal':
                    assert result.returncode == 0, (runner, result)
                else:
                    assert result.returncode == 1 and b'FAIL ' in result.stdout, (label, runner, result)
                print(f'{"PASS" if label == "normal" else "MUTATE RED"} {label} ({runner}, rc={result.returncode})', flush=True)
        # Keep an inspectable disabled-path disassembly: one conditional branch,
        # one indirect tail call, no string lookup, import, or I/O.
        hit = folder / 'hit.c'
        hit.write_text('#include "debug_probe.h"\nvoid hit(struct probe_slot *s) { PROBE_HIT(*s); }\n')
        obj = folder / 'hit.o'
        subprocess.run([*flags, '-c', hit, '-o', obj], check=True)
        asm = subprocess.check_output(['objdump', '-dr', obj], text=True)
        import re
        assert len(re.findall(r'\bje\b|\bjne\b', asm)) == 1, asm
        assert len(re.findall(r'\bjmp\s+\*', asm)) == 1, asm
        assert not re.search(r'\b(in|out|call)\b|R_386_', asm), asm
        print(asm, flush=True)
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_debug_probe.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    raise SystemExit(main())
