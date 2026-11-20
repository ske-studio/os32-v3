#!/usr/bin/env python3
"""W[2..4]: compiled policy, actual registry/evaluator/held state, LE and BE."""
CHECK = dict(name='input-w34', tier=1, budget=60, needs=[], kind='contract',
    covers=['input/hal/input_policy.c', 'input/hal/input_policy.h',
            'input/hal/keymap*.c', 'input/hal/keymap.h', 'input/hal/kbd_merge.c',
            'input/hal/in2_dev.c', 'input/hal/in2_int.h', 'input/hal/legacy_keymap.c',
            'input/keymaps/*.c', 'platform/pc98/input_keymap.c',
            'include/types.h', 'include/callconv.h', 'include/kbd.h', 'include/legacy_guest.h', 'include/app_ids.h', 'include/io.h',
            'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host32.py',
            'tools/tests/input_w34_host.c', 'tools/tests/test_input_w34.py'],
    basis=['docs/decisions/INPUT_KEYMAP_DESIGN.md §5・§8・W の小段の割り方・IKM1 v2 の列挙の数値'])
from pathlib import Path
from types import SimpleNamespace
import os
import shutil
import subprocess
import sys
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]
PATHS = ['input/hal/'+n+'.c' for n in ('input_policy', 'keymap_registry', 'keymap_eval',
         'keymap_state', 'in2_dev', 'kbd_merge', 'legacy_keymap')]
PATHS += ['input/keymaps/pc98_jp.c', 'input/keymaps/hid_jp.c', 'platform/pc98/input_keymap.c']
MUTATIONS = [
    ('input_policy', 'if (found == key) return OS32_ERR_INVAL;', 'if (0) return OS32_ERR_INVAL;'),
    ('input_policy', 'if (contextual(active, &row, &value)) { /* W2_KEY_ACTION_FIRST */', 'if (0) { /* W2_KEY_ACTION_FIRST */'),
    ('input_policy', 'if (owner && !saved_valid) { /* W3_SAVE_ONCE */', 'if (owner) { /* W3_SAVE_ONCE */'),
    ('input_policy', 'active = saved; /* W3_RESTORE */', 'active = 0; /* W3_RESTORE */'),
    ('input_policy', 'if (busy(owner, gen)) { irq_restore(flags);', 'if (0) { irq_restore(flags);'),
    ('input_policy', 'if (capacity < *needed) return OS32_ERR_FULL;', 'if (capacity == 0) return OS32_ERR_FULL;'),
    ('input_policy', 'write16(p + 4, 2);', 'write16(p + 4, 1);'),
    ('input_policy', 'write32(r + 8, row.value);', 'write32(r + 8, 0);'),
    ('kbd_merge', 'bits &= (u8)~(1u << (KEY_CAPS & 7));', 'bits &= 0xff;'),
    ('kbd_merge', 'bits &= (u8)~(1u << (KEY_KANA & 7));', 'bits &= 0xff;'),
]

def run(ctx):
    sources = {Path(p).stem: (ctx.root/p).read_text() for p in PATHS}
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
    qemu = shutil.which('qemu-ppc')
    arches = ['x86']
    if cc.is_file() and qemu:
        arches.append('ppc')
    else:
        print('SKIP PowerPC: compiler or qemu-ppc unavailable', flush=True)
        if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
    with tempfile.TemporaryDirectory(prefix='input-w34-') as directory:
        tmp = Path(directory)
        (tmp/'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned f) { (void)f; }\n')
        includes = ['-I'+str(tmp), '-I'+str(ctx.root/'input/hal'), '-I'+str(ctx.root),
                    '-I'+str(ctx.root/'include'), '-I'+str(ctx.root/'sdk/include/os32')]
        def build(texts, arch, mac=False):
            files = []
            for name, code in texts.items():
                path = tmp/(name+'.c')
                path.write_text(code.replace('../../input/hal/keymap.h','keymap.h').replace('../../input/hal/in2_int.h','in2_int.h'))
                files.append(str(path))
            cmd = ['gcc','-m32'] if arch == 'x86' else [str(cc),'-DPPC_TEST']
            cmd += ['-std=gnu11','-O2','-Wall','-Wextra','-Werror','-ffreestanding',
                    '-fno-stack-protector','-nostdlib','-static',*includes,*files,
                    str(ctx.root/'tools/tests/input_w34_host.c'),'-o',str(tmp/'test')]
            if mac: cmd.insert(1, '-DMAC_POLICY_TEST')
            if arch == 'x86': cmd[2:2] = ['-fno-pie','-no-pie']
            subprocess.run(cmd, check=True)
            return subprocess.run([qemu,str(tmp/'test')]).returncode if arch == 'ppc' else host32.run([str(tmp/'test')],runner='qemu').returncode
        for arch in arches:
            rc=build(sources,arch); assert rc==0, (arch,rc)
            assert build(sources,arch,mac=True)==0
            print('PASS',arch,'PC98/Mac W[2..4] compilation, defaults, priority, save/restore, BUSY, short get, 8192 rows',flush=True)
        if ctx.mutate:
            for path, anchor, replacement in MUTATIONS:
                assert sources[path].count(anchor)==1, anchor
                changed=dict(sources); changed[path]=changed[path].replace(anchor,replacement)
                for arch in arches:
                    rc=build(changed,arch); assert 0 < rc < 128, (arch,anchor,rc)
                    print('RED runtime',arch,anchor,flush=True)
            for arch in arches: assert build(sources,arch)==0
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT,mutate='--mutate' in sys.argv)))
