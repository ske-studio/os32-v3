"""Exercise the common database KAPI across two PowerMac shell processes."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
import ppc_resource_owner
from kboot_accept import build_media
from ppc_shm import guest_available
from run import require

DB = 'kapi/kapi_db.c'
V50 = 'kapi/kapi_db_v50.inc'
BIND = 'text_copy_buf, length,\n                                             SQLITE_TRANSIENT'
MUTATIONS = {
    'no-vfs': [('fs/ramfs/ramfs.c', None, '.sqlite = &ramfs_sqlite_ops', '.sqlite = 0 ? &ramfs_sqlite_ops : 0')],
    'static-bind': [(DB, V50, BIND, BIND.replace('TRANSIENT', 'STATIC'))],
    'fp-owner': [(DB, V50, 'if (handle == -1) {',
                  'extern volatile u32 services_boot_fault; if (handle == -2) services_boot_fault = 99;\n    if (handle == -1) {'),
                 ('kernel/sqlite_engine.c', None, 'arch_kernel_fp_end(&guard);',
                  'if (services_boot_fault == 99) services_boot_fault = 0;\n'
                  '    else arch_kernel_fp_end(&guard);')],
    'empty-only': [(DB, V50, 'if (st.st_size == 0)', 'if (st.st_size != 0)')],
    'lost-code': [(DB, 'kapi/kapi_db_helpers.inc', 'db_open_fail[owner] = code;', '(void)code;')],
    'leak': [('exec/exec.c', 'exec/exec_finish.inc', 'exec_db_cleanup_owned(id);', ';')],
}
FIRST = 'OPEN CREATE INSERT BIND-RC BIND-VAL STEP CONV-POS CONV-NEG CONV-MOD-POS CONV-MOD-NEG CONV-SUM FP-PRE FPSCR NEXT ERR-RC ERR-CODE NAME CLOSE USED PENDING'.split()
SECOND = ['STAT', 'REOPEN', 'RECLAIM']
FAILURE = {'no-vfs': 'OPEN', 'static-bind': 'BIND-VAL', 'fp-owner': 'FPSCR',
           'empty-only': 'REOPEN', 'lost-code': 'ERR-CODE', 'leak': 'RECLAIM'}


def mutations(folder, edits):
    settings = []
    for index, (unit, include, before, after) in enumerate(edits):
        source = ROOT / (include or unit)
        text = source.read_text()
        if text.count(before) != 1:
            raise ValueError(f'{source}: expected one {before!r}')
        dest = folder / source.name
        dest.write_text(text.replace(before, after))
        if include:
            # Quoted includes search beside the copied translation unit first.
            parent = ROOT / unit
            if parent.read_text().count(f'#include "{source.name}"') != 1:
                raise ValueError(f'{parent}: missing unique include for {source}')
            dest = folder / parent.name
            dest.write_bytes(parent.read_bytes())
        prefix = 'KB_MUT_EXTRA_' if index else 'KB_MUT_'
        settings.extend([prefix + 'SRC=' + unit, prefix + 'FILE=' + str(dest)])
    return settings


def observe(media, cpu, folder, case):
    from link_sh_run import Session
    from usb_run import rsh_command

    session = Session(media, cpu, folder, 240)
    try:
        session.start()
        session.wait(session.ready, 'db cui READY', 90)
        serial = (folder / 'serial-a.log').read_text(errors='replace')
        require('[sqlite] static rc=0' in serial and session.agent.operator.ready['mode'] == 'cui', 'db SQLite startup and CUI mode')
        command = '/bin/dbprob.bin 1'
        failure = FAILURE.get(case)
        for phase, markers in enumerate((FIRST, SECOND), 1):
            body = rsh_command(session, command)
            status = rsh_command(session, 'echo $?')
            lines = body.decode().strip().splitlines()
            match = re.fullmatch(r'DB-BASE (\d+)', lines[0]) if lines else None
            require(match, repr((case, lines)))
            base = int(match[1])
            if phase == 1:
                first = base
                command = f'/bin/dbprob.bin 2 {first}'
            elif case == 'leak':
                require(base > first, repr((case, first, base)))
                markers = ['STAT', 'RECLAIM']
            else:
                require(base == first, repr((case, first, base)))
            failed = failure in markers
            green = markers[:markers.index(failure)] if failed else markers
            expected = ['DB-' + item + ' OK' for item in green]
            require(lines[1:1 + len(green)] == expected, repr((case, phase, lines)))
            if failed:
                red = ['DB-' + failure + ' FAIL']
                if case == 'fp-owner':
                    # The REAL step re-enters SQLite to materialize its row.
                    # The lost guard also rejects the following sqlite3_step.
                    red.append('DB-NEXT FAIL')
                require(lines[1 + len(green):] == red, repr((case, lines)))
                require(status == b'1\n', repr((case, status)))
                break
            require(lines[1:] == expected and status == b'0\n', repr((case, phase, lines, status)))
        if case == 'normal':
            ppc_resource_owner.verify(session)
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'db QEMU exit')
        print('RED' if failure else 'GREEN', case, cpu, flush=True)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross / 'bin/powerpc-elf-gcc').is_file() or (not args.build_only and not guest_available()):
        print('SKIP: PowerPC toolchain, mac99 QEMU or loopback unavailable')
        return 3
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    work = Path(tempfile.mkdtemp(prefix='ppc-db-')).resolve()
    cases = {'normal': []}
    if args.mutate:
        cases.update(MUTATIONS)
    settings = {}
    for case, edits in cases.items():
        folder = work / case
        folder.mkdir()
        settings[case] = mutations(folder, edits)
    user = work / 'user'
    bins = ' '.join(str(user / (app + '.bin')) for app in ('dbprob',))
    settings['normal'] = ppc_resource_owner.instrument(
        work / 'normal', DB, 'db_slots[i].owner = res_owner_get();', 'db_slots[i].owner')
    seen = set()
    for case in cases:
        folder = work / case
        media = build_media(folder / 'kernel', 'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                    'KB_ROOT_BINS=' + bins, *settings[case], 'kb-user', 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        require(digest not in seen, repr((case, digest)))
        seen.add(digest)
        print(f'{case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(media, cpu, folder / cpu, case)
    print(f'{len(seen)} distinct link-sh images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
