#!/usr/bin/env python3
"""C1: registry, module hooks, ACTIVE, heap failure, sync publication and caller."""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

CHECK = dict(name='gfx2-backend-reg-host', tier=1, needs=[], budget=20,
    covers=['gfx/v2/backend_reg.c', 'gfx/v2/gfx2_int.h',
            'platform/pc98/gfx_selector.c', 'gfx/gfx_core.c', 'gfx/gfx_core_init.inc',
            'tools/tests/gfx2_backend_reg_host.c', 'tools/tests/test_gfx2_backend_reg.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py'])
ROOT = Path(__file__).resolve().parents[2]
REG = 'gfx/v2/backend_reg.c'
PROVIDER = 'platform/pc98/gfx_selector.c'
MUTATIONS = (
    ('head-count', REG, 'a->heads = a->caps.heads < 2 ? a->caps.heads : 2;', 'a->heads = a->caps.heads;'),
    ('abi', REG, 'be->abi != GFX_HAL2_ABI', '0'),
    ('null-hook', REG, 'if (!be->hook)', 'if (0)'),
    ('module-text', REG, 'mod && !module_text_contains', '0 && mod && !module_text_contains'),
    ('identity', REG, 'gfx2_adapters[i].v1 == v1', 'gfx2_adapters[i].v1 == NULL && v1 == NULL'),
    ('owner-retry', REG, '!gfx2_quar_owner &&', '!gfx2_quar_owner && !quar_warned &&'),
    ('shim', REG, 'v1->v2 ? v1->v2 : &gfx2_v1_shim', 'v1->v2 ? v1->v2 : &gfx2_lost_be'),
    ('active-clear', REG, 'o->flags &= ~GFX2_AF_ACTIVE;', 'o->flags |= GFX2_AF_ACTIVE;'),
    ('detach', REG, 'gfx2_head_detach(o, h);', '(void)h;'),
    ('generation', REG, 'if (++o->gen == 0) o->gen = 1;', 'if (o->gen == 0) o->gen = 1;'),
    ('heap-failure', REG, 'heap_retry[a->index] = rc != 0;', 'heap_retry[a->index] = 0;'),
    ('heap-empty', REG, '(void)gfx2_heap_init(&a->heap, 0, 0);', '(void)gfx2_heap_init(&a->heap, 0, 1);'),
    ('caller-failure', REG, '!c || !caller_access_get_user(&caller)', '!c || (caller_access_get_user(&caller), 0)'),
    ('caller-owner', REG, 'c->owner = caller.owner;', 'c->owner = 0;'),
    ('sync-mode', REG, '*mode = (u32)gfxsync_mode(*ctx);', '*mode = GFXSYNC_DIRECT;'),
    ('sync-publish', REG, 'sync_tab[kind][head] = ctx;', 'if (!sync_tab[kind][head]) sync_tab[kind][head] = ctx;'),
    ('init-register', 'gfx/gfx_core_init.inc', '    gfx2_register_selected();', '    (void)0;'),
    ('init-palette', 'gfx/gfx_core_init.inc', '    gfx2_pal_reset16();', '    (void)0;'),
    ('shutdown-register', 'gfx/gfx_core.c', '        gfx2_register_selected();', '        (void)0;'),
    ('shutdown-detach', 'gfx/gfx_core.c', 'gfx2_head_detach(a, h);', '(void)h;'),
    ('provider-selected', PROVIDER, 'if (be == g_backend) fn(be, arg);', 'if (be != g_backend) fn(be, arg);'),
)


def observe(folder, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new)
    def source(path):
        return edits.get(path, (ROOT / path).read_text())

    def function(path, name):
        body = source(path)
        match = re.search(r'^(?:static )?void ' + name + r'\([^\n]*\)[^\n]*\n\{', body, re.M)
        assert match, name
        pos, depth = match.end(), 1
        while depth:
            depth += (body[pos] == '{') - (body[pos] == '}')
            pos += 1
        return body[match.start():pos] + '\n'

    # Execute the complete functions, preserving every product call and branch.
    (folder / 'gfx2_core_calls.inc').write_text(
        function('gfx/gfx_core.c', 'gfx2_register_other') +
        function('gfx/gfx_core.c', 'gfx2_register_selected') +
        function('gfx/gfx_core.c', 'gfx_shutdown') +
        function('gfx/gfx_core_init.inc', 'gfx_init_mode'))
    exe = folder / 'registry'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
               '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-no-pie',
               '-fno-stack-protector', '-nostdlib', '-static',
               '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-I' + str(folder), '-I' + str(ROOT), *include_flags(ROOT),
               str(ROOT / 'tools/tests/gfx2_backend_reg_host.c'),
               str(ROOT / REG), str(ROOT / PROVIDER), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), (mutation, result)
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print('RED runtime ' + mutation[0] if mutation else 'GREEN C1 registry/provider', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for mutation in (None, *MUTATIONS, None) if args.mutate else (None,):
        with tempfile.TemporaryDirectory(prefix='gfx2-reg-') as directory:
            observe(Path(directory), mutation)


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=env)


if __name__ == '__main__':
    main()
