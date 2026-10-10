"""T2h: real stop hooks and separate build products, no emulator or disk image.

CPU ud2/stop are replaced; allocator and closing exception path are real source.
Only the temporary source copy uses SOURCE_DATE_EPOCH for build comparisons.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import host32
from r1_recovery import recovery, lease_recovery, RECOVERY_MUTANTS, LEASE_MUTANTS

ROOT = Path(__file__).resolve().parents[2]
TARGET_SRCS = ['kernel/r1_fixture.c', 'kernel/r1_fixture.h', 'kernel/isr_handlers.c',
               'kernel/v86_mem.c', 'exec/exec.c', 'kernel/pgalloc.c',
               'tools/tests/r1_fixture_host.c', 'tools/r1_manifest.py',
               'tools/tests/r1_recovery.py', 'tools/tests/r1_recovery_host.c',
               'tools/tests/r1_lease_host.c', 'exec/appslot.c', 'exec/appslot.h',
               'exec/owner_diag.h', 'kernel/paging.c', 'exec/lease.c']


def function(source, signature):
    match = re.search(re.escape(signature) + r'[^;{]*\{', source)
    assert match, signature
    start = match.start()
    brace = match.end() - 1
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def fixture(tmp, mutation=None, runner="qemu"):
    read = lambda p: (ROOT / p).read_text()
    fixture = read('kernel/r1_fixture.c')
    if mutation:
        old, new = mutation
        assert old in fixture
        fixture = fixture.replace(old, new, 1)
    fixture = fixture.replace('__asm__ volatile("ud2")', 'host_raise_ud()')
    fixture = fixture.replace('"../exec/appslot.h"', '"appslot.h"')
    (tmp / 'fixture_source.c').write_text(fixture)
    (tmp / 'pgalloc_source.c').write_text(read('kernel/pgalloc.c').replace('#include "io.h"', ''))
    exe = read('exec/exec.c')
    (tmp / 'exec_stop_source.c').write_text('\n'.join(function(exe, sig) for sig in (
        'static void exec_stop_mark(', 'static void exec_pending_transfer(')))
    isr = read('kernel/isr_handlers.c')
    # Whole dispatch prefix through the real ring3 fault branch, omit screen dumps.
    entry = function(isr, 'void exception_handler(')
    entry = entry[:entry.index('    /* 画面上部クリア */')] + '}\n'
    entry = entry.replace('    const char *name;\n    int row = 0;\n', '')
    entry = entry.replace('    _disable();', '    (void)error_code;\n    _disable();')
    end = function(read('kernel/v86_mem.c'), 'void v86_session_end(')
    end = end[:end.index('    if (!v86_session.open)')] + '    irq_restore(flags);\n}\n'
    (tmp / 'entry_source.c').write_text(entry + end)
    (tmp / 'timer_source.c').write_text(function(isr, 'void timer_handler('))
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
           '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static',
           '-no-pie', '-ffunction-sections', '-Wl,--gc-sections', '-DOS32_R1_FIXTURE',
           '-I' + str(tmp)]
    cmd += ['-I' + str(ROOT / p) for p in ('include', 'kernel', 'lib', 'drivers',
                                         'sdk/include/os32', 'tools/tests', 'exec')]
    cmd += [str(ROOT / 'tools/tests/r1_fixture_host.c'), str(ROOT / 'kernel/physmem.c'),
            '-o', str(tmp / 'probe')]
    result = subprocess.run(cmd, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    result = host32.run([str(tmp / 'probe')], runner=runner, capture_output=True, text=True, timeout=30)
    return result.returncode


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def builds(tmp):
    # Copy source bytes, never link back to the live tree: make also regenerates
    # KAPI files and build IDs. Include untracked sources, but not ignored data.
    root = tmp / 'tree'
    listed = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT)
    for name in sorted(set(os.fsdecode(listed).split('\0')) - {''}):
        src = ROOT / name
        if not src.is_file() or '.env' in src.parts:
            continue
        dest = root / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
    # Reuse only the Rust archive; the C/ASM objects are built in the copy.
    if 'RUST_LZ4_LIB' in os.environ:
        archive = Path(os.environ['RUST_LZ4_LIB'])
    else:
        result = subprocess.run(
            ['make', '--no-print-directory', '-s', '--eval',
             'print-r1-lz4:;@echo $(RUST_LZ4_LIB)', 'print-r1-lz4'],
            cwd=ROOT, capture_output=True, text=True, check=True)
        archive = Path(result.stdout.strip())
    (root / archive).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / archive, root / archive)
    env = dict(os.environ, SOURCE_DATE_EPOCH='1700000000')
    def make(target):
        with (tmp / (target + '.log')).open('w') as log:
            rc = subprocess.run(['make', target], cwd=root, env=env, stdout=log,
                                stderr=subprocess.STDOUT).returncode
        assert rc == 0, (tmp / (target + '.log')).read_text()[-4000:]
    def snapshot():
        return {str(p): (p.stat().st_mtime_ns, digest(p)) for p in
                (root / 'kernel').glob('*.o')}
    make('kernel')
    product = root / 'build/out/kernel.bin'
    original = digest(product)
    original_map = digest(root / 'build/out/kernel.map')
    original_objects = snapshot()
    # P -> R -> P -> R includes both required no-clean orderings.
    make('kernel-r1')
    assert snapshot() == original_objects, 'fixture changed product objects'
    assert digest(product) == original, 'fixture changed product image'
    assert digest(root / 'build/out/kernel.map') == original_map
    make('kernel')
    assert digest(product) == original, 'R -> P changed product image'
    original_objects = snapshot()
    make('kernel-r1')
    assert snapshot() == original_objects and digest(product) == original
    assert 'r1_fixture_' not in (root / 'build/out/kernel.map').read_text()
    trial_map = (root / 'build/out/r1/kernel.map').read_text()
    assert 'r1_fixture_arm' in trial_map and 'r1_fixture_timer' in trial_map
    assert digest(root / 'build/out/vmkernel.lz4') != digest(root / 'build/out/r1/vmkernel.lz4')
    manifest = json.loads((root / 'build/out/r1/manifest.json').read_text())
    assert manifest['files']['kernel.bin']['sha256'] == digest(root / 'build/out/r1/kernel.bin')
    # Exercise the real make target with a forbidden production local name.
    bad_env = dict(env, OS32_NHD_LOCAL=str(root / 'build/nhd/os32.nhd'))
    result = subprocess.run(['make', 'deploy-kernel-r1', 'PROFILE=t2h'], cwd=root,
                            env=bad_env, capture_output=True, text=True)
    assert result.returncode != 0 and 't2h profile path mismatch' in result.stderr
    result = subprocess.run(['make', 'deploy-kernel-r1', 'PROFILE=production'], cwd=root,
                            env=bad_env, capture_output=True, text=True)
    assert result.returncode != 0 and 'requires PROFILE=t2h' in result.stdout
    # Same kselftest source and KAPI generation; no fixture bypass of tests.
    assert 'OS32_R1_FIXTURE' not in (root / 'kernel/kselftest.c').read_text()
    print('r1 build isolation: both orders PASS, product bytes=' + str(product.stat().st_size))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r1-fixture-') as name:
        tmp = Path(name)
        assert fixture(tmp, runner=args.runner) == 0, 'stop hooks'
        print('r1 stop hooks: PASS')
        result = recovery(tmp, function, args.runner)
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout, end='')
        result = lease_recovery(function, args.runner)
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout, end='')
        if args.runner == host32.selected_runner():
            builds(tmp)
        if args.mutate:
            for name, path, old, new, expected in RECOVERY_MUTANTS:
                result = recovery(tmp, function, args.runner, (path, old, new))
                assert result.returncode == 1 and expected in result.stdout, (name, result.returncode, result.stdout, result.stderr)
                print('RED: ' + name)
            for name, path, old, new, expected in LEASE_MUTANTS:
                result = lease_recovery(function, args.runner, (path, old, new))
                assert result.returncode == 1 and expected in result.stdout, (name, result.returncode, result.stdout, result.stderr)
                print('RED: ' + name)
            for old, new in [
                ('r1_fixture_arm[slot] = 0;', '/* leave armed */'),
                ('!= R1_FIXTURE_ARM', '== R1_FIXTURE_ARM'),
                ('pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, 1)', '(u32)0'),
                ('pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, 0, 1)', '(int)0'),
                ('__asm__ volatile("ud2")', '(void)0'),
            ]:
                assert fixture(tmp, (old, new), runner=args.runner) != 0, 'survived: ' + old
                print('RED: ' + old)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
