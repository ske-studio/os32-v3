"""Check static SQLite boot and rollback on PowerMac mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build, mutant_settings
from ppc_shm import guest_available


def probe_source():
    source = (ROOT / 'kernel/sqlite_static.c').read_text()
    anchor = '            services_public = 1;'
    if source.count(anchor) != 1:
        raise ValueError('SQLite boot publication point changed')
    return ('static void sqlite_static_probe(void);\n' +
            source.replace(anchor, anchor + '\n            sqlite_static_probe();') +
            (ROOT / 'tools/tests/ppc_sqlite_static_probe.inc').read_text())


def observe(elf, cpu, folder, case):
    from link_sh_run import Session

    session = Session(elf, cpu, folder, 240)
    try:
        session.start()
        session.wait(session.ready, 'SQLite probe READY', 90)
        serial = (session.out / 'serial-a.log').read_text(errors='replace')
        lines = [line[line.index('[sqlite]'):].strip() for line in serial.splitlines()
                 if '[sqlite]' in line]
        assert '[sqlite] static rc=0' in lines, (case, cpu, lines)
        if case != 'normal':
            assert '[sqlite] probe create=0' in lines, lines
            assert '[sqlite] probe journal=1' in lines, lines
            expected = (r'commit=[1-9]\d* reopen=0 rows=0' if case == 'sync-fail'
                        else 'commit=0 reopen=0 rows=1')
            assert any(re.fullmatch(r'\[sqlite\] probe create=0 ' + expected + ' journal=0', line)
                       for line in lines), (case, cpu, lines)
        session.q.call('quit')
        session.p.wait(timeout=5)
        assert session.p.returncode == 0
        print('RED' if case == 'sync-fail' else 'GREEN', case, cpu, *lines)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not all(os.access(cross / 'bin' / ('powerpc-elf-' + tool), os.X_OK)
               for tool in ('gcc', 'ld', 'ar', 'nm', 'objcopy', 'objdump')):
        print('SKIP: PowerPC toolchain unavailable')
        return 3
    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback unavailable')
        return 3
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    work = Path(tempfile.mkdtemp(prefix='ppc-sqlite-static-', dir=os.environ.get('TMPDIR'))).resolve()
    user = work / 'user'
    seen = set()
    cases = ['normal', 'probe']
    if args.mutate:
        cases.append('sync-fail')
    for case in cases:
        folder = work / case
        kernel = folder / 'kernel'
        kernel.mkdir(parents=True)
        settings = []
        if case != 'normal':
            src = folder / 'sqlite_static_probe.c'
            src.write_text(probe_source())
            settings = ['KB_MUT_SRC=kernel/sqlite_static.c', 'KB_MUT_FILE=' + str(src)]
        if case == 'sync-fail':
            anchor = 'static int ramfs_sync(void *ctx)\n{\n    (void)ctx;'
            failure = (anchor + '\n    extern volatile int sqlite_probe_commit;'
                       '\n    if (sqlite_probe_commit) { sqlite_probe_commit = 0; return VFS_ERR_NOSYS; }')
            extra = mutant_settings(folder, case, 'fs/ramfs/ramfs.c', anchor, failure)
            settings.extend(item.replace('KB_MUT_', 'KB_MUT_EXTRA_', 1) for item in extra)
        elf = build(kernel, 'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                    *settings, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert digest not in seen, (case, digest)
        seen.add(digest)
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case)
    print(f'{len(seen)} distinct SQLite images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
