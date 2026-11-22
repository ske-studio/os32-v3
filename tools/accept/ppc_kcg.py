#!/usr/bin/env python3
"""Exercise the five KCG KAPI slots over link-sh RSH on mac99.
AC-GLYPH-1 defines blank cells for unavailable glyphs. KCG1 absence also
disables the font-backed console and leaves the resident provider unready.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]

EXPECTED = {
    'normal': (b'K\n\n', b'0\n'),
    'missing': (b'K\n\n', b'0\n'),
    'ready': (b'K\n\n', b'0\n'),
    'offset': (b'G\n', b'1\n'),
    'absent': (b'G\n', b'1\n'),
}


def observe(media, cpu, evidence, case):
    from link_sh_run import Session
    from run import require
    from usb_run import rsh_command

    session = Session(media, cpu, evidence, 240)
    try:
        session.start()
        session.wait(session.ready, 'KCG cui READY', 60)
        require(session.agent.operator.ready['mode'] == 'cui', 'KCG RSH mode')
        missing = case in ('missing', 'ready')
        body = rsh_command(session, '/bin/kcgprob.bin' + (' missing' if missing else ''))
        status = rsh_command(session, 'echo $?')
        require((body, status) == EXPECTED[case], repr((case, body, status)))
        if missing:
            log = (evidence / 'serial-a.log').read_bytes()
            require(b'FB off step=font' in log, 'KCG1 missing at font load')
            readings = [line.strip() for line in log.splitlines() if b'KCG ready=' in line]
            wanted = b'KCG ready=' + (b'1' if case == 'ready' else b'0')
            require(len(readings) == 1 and readings[0].endswith(wanted), repr(readings))
        if case == 'normal':
            require(session.word('kcg_scale') == 3, 'scale after NOSYS')
            rsh_command(session, '/bin/kcgprob.bin reset', b'I\n\n')
            require(session.word('kcg_scale') == 1, 'scale after init')
        session.q.call('quit')
        session.p.wait(timeout=5)
        require(session.p.returncode == 0, 'KCG QEMU exit')
        print(('GREEN ' if case in ('normal', 'missing') else 'RED ') + case + ' ' + cpu, flush=True)
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--case', choices=EXPECTED)
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if any(not os.access(cross / 'bin' / ('powerpc-elf-' + tool), os.X_OK)
           for tool in ('gcc', 'ld', 'ar', 'nm', 'objcopy', 'objdump')):
        print(f'SKIP: PowerPC toolchain unavailable at {cross}')
        return 3

    from kboot_accept import build_media, mutant_settings
    from ppc_shm import guest_available

    if not args.build_only and not guest_available():
        print('SKIP: mac99 QEMU or loopback sockets unavailable')
        return 3

    work = Path(tempfile.mkdtemp(prefix='ppc-kcg-', dir=os.environ.get('TMPDIR') or None)).resolve()
    print('evidence=' + str(work), flush=True)
    user = work / 'user'
    bins = [str(user / 'kcgprob.bin')]
    options = ['IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
               'KB_ROOT_BINS=' + ' '.join(bins)]
    images = {}
    os.environ.setdefault('SOURCE_DATE_EPOCH', '946684800')
    cases = EXPECTED if args.mutate else ('normal', 'missing')
    for case in ((args.case,) if args.case else cases):
        folder = work / case
        folder.mkdir()
        kernel = folder / 'kernel'
        mutation = []
        if case in ('missing', 'ready'):
            manifest = json.loads((ROOT / 'build/ppc-ram-root.json').read_text())
            del manifest['/fonts/shino16.kcg']
            path = folder / 'root.json'
            path.write_text(json.dumps(manifest))
            hook = '    kcg_file_unload();'
            mutation = mutant_settings(folder, 'observe', 'platform/powermac/console_text.c',
                                       hook, hook + '\n    extern int kcg_module_ready(void);'
                                       '\n    kprintf(KBOOT_ATTR, "KCG ready=%d\\r\\n", kcg_module_ready());')
            mutation.append('ROOT_MANIFEST=' + str(path))
            if case == 'ready':
                mutation.extend(item.replace('KB_MUT_', 'KB_MUT_EXTRA_') for item in
                                mutant_settings(folder, case, 'platform/powermac/kcg.c',
                                                'return kcg_file_ready();', 'return 1;'))
        elif case == 'offset':
            # Inject the wrong offset byte order into the font consumer only.
            # The same decoder must first expand the root containing the font.
            core = (ROOT / 'lib/lz4_decode_core.inc').read_text()
            offset = 'offset = (int)ip[0] | ((int)ip[1] << 8);'
            if core.count(offset) != 1:
                raise RuntimeError('KCG offset mutation anchor')
            (folder / 'kcg_offset.inc').write_text(core.replace(offset,
                'extern const u8 __root_image_start[];\n'
                '        if (src == __root_image_start) ' + offset +
                ' else offset = (int)ip[1] | ((int)ip[0] << 8);'))
            mutation = mutant_settings(folder, case, 'lib/lz4.c',
                                       '#include "lz4_decode_core.inc"',
                                       '#include "kcg_offset.inc"')
        elif case == 'absent':
            from test_kapi_binding import generate
            kernel.mkdir()
            (generate(kernel, 'kcg-absent') / 'stamp').touch()
        media = build_media(kernel, *options, *mutation, 'kb-user', 'kernel')
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        if digest in images:
            raise RuntimeError(f'{case} ELF is identical to {images[digest]}: {media}')
        images[digest] = case
        print(f'{case} sha256={digest} {media}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(media, cpu, folder / cpu, case)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
