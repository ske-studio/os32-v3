#!/usr/bin/env python3
"""Exercise the single-surface and bundle KAPI slots over link-sh RSH on mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

EXPECTED = {
    'normal': (b'SURFACE OK\n\n', b'0\n'),
    'ignore-generation': (b'SURFACE stale ref accepted\n', b'8\n'),
    'bundle-disabled': (b'SURFACE bundle acquisition failed\n', b'15\n'),
    'ignore-count': (b'SURFACE bundle count failed\n', b'14\n'),
    'bundle-absent': (b'SURFACE bundle absent\n', b'13\n'),
    'absent': (b'SURFACE provider absent\n', b'10\n'),
}


def fixture(folder):
    # Only these private roles use the fixture. Normal public calls use the
    # real OF FB publisher, validation, mapping, and teardown.
    from kboot_accept import mutant_settings

    anchor = """__attribute__((section(".text.surface_api")))
int surface_api_query(u32 role, struct surface_query_result *out)
{
"""
    settings = mutant_settings(folder, 'fixture', 'exec/surface_query.c', anchor, '#include "gfx_internal.h"\n' + anchor + """
    extern const void *probe_watch;
    extern int probe_copied;
    if (role == 0x102) { probe_watch = out; probe_copied = 0; return 0; }
    if (role == 0x103) { probe_watch = 0; return probe_copied; }
    static u32 leased_sid = LEDGER_MAX_SURFACES;
    if (role == 0x104 && leased_sid < LEDGER_MAX_SURFACES)
        return ledger_surfaces[leased_sid].lease_count;
    if (role == 0x100 || role == 0x101) {
        struct surface_query_source client;
        int rc = gfx_surface_source(LEDGER_ROLE_CLIENT, &client);
        if (rc) return rc;
        if (role == 0x101) {
            leased_sid = client.refs[0].sid;
            return ledger_surfaces[leased_sid].lease_count;
        }
        gfx_reinit_surface_roles(0, 0);
        gfx_reinit_surface_roles(1, 0);
        return 0;
    }
""")

    copy_entry = """int copy_caller_bytes(const struct caller_access *c, const void *src,
                      void *dst, u32 len)
{
"""
    spy = mutant_settings(folder, 'copy-watch', 'exec/redir_access.c', copy_entry,
                          'const void *probe_watch;\nint probe_copied;\n' + copy_entry +
                          '    if (src == probe_watch) probe_copied = 1;\n')
    return settings + [s.replace('KB_MUT_', 'KB_MUT_THIRD_', 1) for s in spy]


def absent_bindings(folder, out, case):
    source = (ROOT / 'sdk/kapi_target_ppc.py').read_text()
    anchor = re.search(r"^    'surface': .*", source, re.MULTILINE).group()
    if source.count(anchor) != 1:
        raise RuntimeError('absent surface mutation anchor must be unique')
    group = """    'surface': (frozenset({
        'gfx_surface_lease', 'surface_query', 'surface_lease', 'surface_unlease',
    }), frozenset()),
"""
    if case == 'bundle-absent':
        group = "    'surface': (frozenset({'gfx_surface_lease'}), frozenset()),"
    (folder / 'kapi_target_ppc.py').write_text(source.replace(anchor, group.rstrip()))
    launcher = ('import runpy, sys; import kapi_target_ppc; '
                'sys.path.insert(0, "sdk"); runpy.run_path(sys.argv.pop(1), run_name="__main__")')
    subprocess.run([sys.executable, '-B', '-c', launcher, str(ROOT / 'sdk/gen_kapi.py'),
                    '--target', 'ppc-powermac', '--out-dir', str(out)],
                   cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(folder)), check=True)
    (out / 'stamp').touch()


def observe(elf, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(elf, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'surface cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'surface RSH mode')
        body = rsh_command(session, '/bin/surfprob.bin')
        status = rsh_command(session, 'echo $?')
        require((body, status) == EXPECTED[case], repr((case, body, status)))
        if case == 'normal':
            for arg, expected in (
                ('leave', b'SURFACE left lease at exit\n\n'),
                ('reclaimed', b'SURFACE reclaimed\n\n'),
            ):
                body = rsh_command(session, '/bin/surfprob.bin ' + arg)
                status = rsh_command(session, 'echo $?')
                require((body, status) == (expected, b'0\n'), repr((arg, body, status)))
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'surface QEMU exit')
        print(('GREEN' if case == 'normal' else 'RED ' + case) + ' ' + cpu, flush=True)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if any(not os.access(cross / 'bin' / ('powerpc-elf-' + tool), os.X_OK)
           for tool in ('gcc', 'ld', 'ar', 'nm', 'objcopy', 'objdump')):
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3

    from kboot_accept import build, mutant_settings
    from ppc_shm import guest_available

    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-surface-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / 'surfprob.bin')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    for case in EXPECTED if args.mutate else ('normal',):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = [] if case == 'absent' else fixture(folder)
        if case == 'ignore-generation':
            # Feed the current generation to all three validation layers,
            # preserving sid checks and the production mapping path.
            extra = mutant_settings(folder, case, 'exec/lease.c',
                                    '    if (!rc) rc = surface_query_refs(source, &ref, 1, access);',
                                    '    if (!rc) {\n'
                                    '        ref.generation = source->refs[0].generation;\n'
                                    '        rc = surface_query_refs(source, &ref, 1, access);\n'
                                    '    }')
            mutation.extend(setting.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1)
                            for setting in extra)
        elif case in ('bundle-disabled', 'ignore-count'):
            if case == 'bundle-disabled':
                src = 'platform/powermac/display_bundle.c'
                before = 'return role == LEDGER_ROLE_CLIENT && backend == LEDGER_SF_OF_FB;'
                after = '(void)role; (void)backend; return 0;'
            else:
                src = 'exec/lease.c'
                before = ' || count != source->count)'
                after = ')'
            extra = mutant_settings(folder, case, src, before, after)
            mutation.extend(s.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1) for s in extra)
        elif case in ('absent', 'bundle-absent'):
            absent_bindings(folder, kernel / 'gen', case)
        elf = build(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {elf}')
        images[digest] = case
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
