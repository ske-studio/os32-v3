#!/usr/bin/env python3
"""B4: the display provider keeps the pre-separation backend selection table."""
CHECK = dict(name='sep5-provider-host', tier=1, needs=[], budget=5,
    covers=['platform/pc98/gfx_selector.c', 'platform/pc98/gfx_selector_lifecycle.inc', 'platform/pc98/display_fallback.h',
            'platform/powermac/display_provider.c', 'platform/powermac/display_fallback.h',
            'include/display_provider.h', 'include/gfx_hal.h', 'gfx/gfx_core.c',
            'gfx/gfx_core_framebuffer.inc', 'gfx/gfx_core_surface.inc', 'gfx/gfx_internal.h',
            'tools/tests/sep5_provider_client_host.c', 'tools/tests/sep5_provider_host.c', 'tools/tests/sep5_provider_ppc_host.c',
            'tools/tests/test_sep5_provider.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk', 'include/**',
            'arch/x86/**', 'platform/pc98/**', 'mm/pgalloc.h', 'sdk/include/os32/**'])
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
PROVIDER = 'platform/pc98/gfx_selector.c'
PPC = 'platform/powermac/display_provider.c'
CORE = 'gfx/gfx_core.c'
# (name, unique anchor, replacement, exit code of the first expectation that
# must fail). Any other exit code is a test error, not a kill.
MUTATIONS = (
    ('pref-cirrus-rejected', 'pref != GFX_PREF_CIRRUS && pref != GFX_PREF_GEFORCE &&',
     'pref != GFX_PREF_GEFORCE &&', 11),
    ('pref-unknown-kept', '        pref = GFX_PREF_AUTO;\n', '        (void)0;\n', 12),
    ('ledger-pegc-as-cirrus', 'be == &gfx_backend_pegc ? LEDGER_SF_PEGC :',
     'be == &gfx_backend_pegc ? LEDGER_SF_CIRRUS :', 13),
    ('cirrus-flag-without-init', 'if (backend == &gfx_backend_cirrus && backend->init) cirrus_probed = 1;',
     'if (backend == &gfx_backend_cirrus) cirrus_probed = 1;', 16),
    ('modeless-probed', 'if (!backend->modes || !backend->probe) return 0;',
     'if (!backend->probe) return 0;', 17),
    ('auto-probes-geforce', "if (!g_backend_list[i] || g_backend_list[i] == &gfx_backend_geforce ||",
     'if (!g_backend_list[i] ||', 17),
    ('auto-mode-miss-continues', 'if (!select_mode(g_backend_list[i], legacy200)) break;',
     'if (!select_mode(g_backend_list[i], legacy200)) continue;', 24),
    ('fallback-mode-skipped',
     '    select_mode(&gfx_backend_pc98, legacy200);\n    return &gfx_backend_pc98;   /* フォールバック */',
     '    return &gfx_backend_pc98;   /* フォールバック */', 24),
    ('pc98-pref-probes', '    if (pref == GFX_PREF_PC98) {', '    if (0) {', 26),
    ('forced-legacy-dropped', '                select_mode(g_backend_list[i], legacy200))\n',
     '                select_mode(g_backend_list[i], 0))\n', 31),
    ('forced-probes-others', '            if (g_backend_list[i] != want) continue;\n', '', 15),
    ('cirrus-pref-as-pegc', '                                     ? &gfx_backend_cirrus\n',
     '                                     ? &gfx_backend_pegc\n', 16),
    ('geforce-pref-as-pegc', 'pref == GFX_PREF_GEFORCE ? &gfx_backend_geforce : (pref == GFX_PREF_CIRRUS)',
     'pref == GFX_PREF_GEFORCE ? &gfx_backend_pegc : (pref == GFX_PREF_CIRRUS)', 33),
    ('trident-prepare-dropped',
     'if (want == &gfx_backend_trident && want && want->prepare) want->prepare();', '(void)0;', 35),
    ('trident-reject-unlogged', '        if (pref == GFX_PREF_TRIDENT) {', '        if (0) {', 36),
    ('trident-rc-zero', '            if (!rc) rc = OS32_ERR_INVAL; /* probe/layout rejection */\n', '', 41),
    ('trident-query-rc-lost', 'int rc = want && want->query ? want->query(&info) : OS32_ERR_NOSYS;',
     'int rc = want && want->query ? (want->query(&info), 0) : OS32_ERR_NOSYS;', 42),
    ('cirrus-flag-never', 'cirrus_probed = 1;', '(void)0;', 18),
    ('cirrus-flag-on-success-only', 'if (backend == &gfx_backend_cirrus && backend->init) cirrus_probed = 1;',
     'if (backend == &gfx_backend_cirrus && backend->init && available[C]) cirrus_probed = 1;', 18),
)
# 部品が未リンクの構成 (weak の番地が 0) だけで落ちる変異: (name, anchor, replacement, exit code)。
# NULL の guard を外すと NULL を読むので、SEGV = host32 の実行が signal で終わること。
SEGV = 'SEGV'
UNLINKED_MUTATIONS = (
    ('unlinked-forced-probed', '            if (!g_backend_list[i]) continue;\n', '', SEGV),
    ('unlinked-trident-prepared', 'if (want == &gfx_backend_trident && want && want->prepare) want->prepare();',
     'if (want == &gfx_backend_trident && want->prepare) want->prepare();', SEGV),
    ('unlinked-trident-queried', 'int rc = want && want->query ? want->query(&info) : OS32_ERR_NOSYS;',
     'int rc = want->query ? want->query(&info) : OS32_ERR_NOSYS;', SEGV),
    ('unlinked-trident-unlogged', '        if (pref == GFX_PREF_TRIDENT) {', '        if (pref == GFX_PREF_TRIDENT && want) {', 54),
)
# The PowerMac provider must stay inert: (name, anchor, replacement, exit code).
PPC_MUTATIONS = (
    ('ppc-pref-kept', '    (void)pref;\n    return GFX_PREF_AUTO;', '    return pref;', 3),
    ('ppc-boot-binds', '    (void)pref;\n    (void)bind_client;\n', '    (void)pref;\n    bind_client();\n', 7),
    ('ppc-display-records', '    (void)ledger_backend;\n    return 1;', '    return ledger_backend + 1;', 8),
    ('ppc-mode-unreported', '    select_mode(&gfx_backend_unsupported, legacy200);\n', '    (void)select_mode; (void)legacy200;\n', 4),
)
FLAGS = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin', '-fno-pie',
         '-fno-stack-protector', '-no-pie']


def execute(text, work, unlinked=False):
    (work / 'gfx_selector_source.inc').write_text(text.replace('#include "gfx_selector_lifecycle.inc"', ''))
    exe = work / 'provider'
    subprocess.run(['gcc', '-m32', '-march=i386', *FLAGS, '-nostdlib', '-static',
        *(['-DNO_OPTIONAL', '-Wno-address', '-Wno-unused-function', '-Wno-unused-const-variable',
           '-Wno-unused-variable'] if unlinked else []),
        '-I' + str(work), *include_flags(ROOT),
        str(ROOT / 'tools/tests/sep5_provider_host.c'), '-o', str(exe)], check=True)
    rc = host32.run([str(exe)]).returncode
    return SEGV if rc in (-11, 139) else rc


def execute_ppc(text, work):
    """The PowerMac provider on the host: platform/powermac must win the include race."""
    (work / 'display_provider_ppc.inc').write_text(text)
    exe = work / 'provider-ppc'
    subprocess.run(['gcc', *FLAGS, '-fhosted', '-I' + str(work), '-I' + str(ROOT / 'platform/powermac'),
        *include_flags(ROOT), str(ROOT / 'tools/tests/sep5_provider_ppc_host.c'),
        '-o', str(exe)], check=True)
    return subprocess.run([str(exe)]).returncode


def execute_client(work, mutation=None):
    kind = (ROOT / 'gfx/gfx_core_surface.inc').read_text()
    start = kind.index('u32 gfx_sf_backend(void)')
    kind = kind[start:kind.index('\n}', start)+2]
    binding = (ROOT / 'gfx/gfx_core_framebuffer.inc').read_text()
    if mutation:
        old, new = mutation
        assert binding.count(old) == 1
        binding = binding.replace(old, new)
    (work / 'core_surface_kind.inc').write_text(kind)
    (work / 'core_framebuffer.inc').write_text(binding)
    exe = work / 'client'
    host32.build(['gcc', '-m32', *FLAGS, '-nostdlib', '-static',
        '-I' + str(work), *include_flags(ROOT),
        str(ROOT / 'tools/tests/sep5_provider_client_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], runner='qemu').returncode


def core_delegates():
    """The common core must not name a platform backend for selection any more."""
    core = (ROOT / CORE).read_text()
    assert 'g_backend = display_provider_select(g_backend_pref, legacy200, gfx_select_mode);' in core
    assert 'GfxBackend *g_backend = DISPLAY_FALLBACK_BACKEND;' in core
    assert 'g_backend_list' not in core and '&gfx_backend_pc98' not in core


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    text, ppc = (ROOT / PROVIDER).read_text(), (ROOT / PPC).read_text()
    core_delegates()
    with tempfile.TemporaryDirectory(prefix='sep5-provider-') as directory:
        work = Path(directory)
        assert execute_client(work) == 0
        assert execute(text, work) == 0
        assert execute(text, work, unlinked=True) == 0
        assert execute_ppc(ppc, work) == 0
        print('PASS host32 provider selection table (auto, forced, fallback, Trident retry/log, '
              'Cirrus probe flag, pref, ledger kind), the same with optional backends unlinked, '
              'and the inert PowerMac provider', flush=True)
        if args.mutate:
            for old, new, code in [
                ('GfxBackend *backend = g_backend;', 'GfxBackend *backend = DISPLAY_FALLBACK_BACKEND;', 2),
                ('gfx_mode_ready && sf && !sf->closing', 'gfx_mode_ready && sf', 4),
                ('base + sf->plane_offset[i] : 0;', 'base : (u8 *)1;', 2),
            ]:
                assert execute_client(work, (old, new)) == code
                print('MUTATE runtime RED OF CLIENT binding (exit %d)' % code, flush=True)
            for name, old, new, code in MUTATIONS:
                assert text.count(old) == 1, (name, text.count(old))
                rc = execute(text.replace(old, new), work)
                assert rc == code, (name, rc, code)
                print('MUTATE runtime RED %s (exit %d)' % (name, code), flush=True)
            for name, old, new, code in UNLINKED_MUTATIONS:
                assert text.count(old) == 1, (name, text.count(old))
                rc = execute(text.replace(old, new), work, unlinked=True)
                assert rc == code, (name, rc, code)
                print('MUTATE runtime RED %s (unlinked, exit %s)' % (name, code), flush=True)
            for name, old, new, code in PPC_MUTATIONS:
                assert ppc.count(old) == 1, (name, ppc.count(old))
                rc = execute_ppc(ppc.replace(old, new), work)
                assert rc == code, (name, rc, code)
                print('MUTATE runtime RED %s (exit %d)' % (name, code), flush=True)
            assert execute(text, work) == 0 and execute_ppc(ppc, work) == 0


def run(ctx):
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_sep5_provider.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    main()
