"""Check the PowerMac version report through its public KAPI on mac99."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from kboot_accept import build, mutant_settings
from test_kapi_binding import generate
from ppc_shm import guest_available


def observe(elf, cpu, folder, case):
    from link_sh_run import Session
    from usb_run import rsh_command

    s = Session(elf, cpu, folder, 240)
    try:
        s.start()
        s.wait(s.ready, 'version probe READY', 60)
        lines = rsh_command(s, '/bin/verprob.bin').decode().strip().splitlines()
        status = rsh_command(s, 'echo $?').strip()
        expected = ['HEAD 800 600']
        if case == 'absent':
            expected.append('VER NOSYS')
            assert status == b'2', status
        else:
            assert status == b'0', status
            expected.append(r'0 PowerMac OS32 v\S+')
            if case != 'drop-pvr':
                # mac99 QEMU aliases 7450 to v2.1 and 7455 to v3.2.
                pvr = {'7450': '80000201', '7455': '80010302'}[cpu]
                expected.append('257 CPU: PowerPC PVR=' + pvr)
            expected.extend([r'257 IRQ: MPIC sources=[1-9]\d*',
                             r'257 DEC: tick=1000Hz \(configured\)',
                             '257 SER: ESCC baud=9600',
                             '257 GFX: OF-fb 800x600x8 bpp; (active|GUI target)',
                             r'258 API: v[1-9]\d*', '258 Build: .+', '258 Commit: .+'])
            iso = (elf.parent / 'root.iso').read_bytes()
            size, crc = len(iso), zlib.crc32(iso)
            if case == 'ignore-crc':
                expected.append(r'258 Root image CRC: 00000000 \(0 bytes\)')
            elif case != 'invalid-crc':
                expected.append(rf'258 Root image CRC: {crc:08x} \({size} bytes\)')
        assert len(lines) == len(expected) and all(re.fullmatch(p, line) for p, line in
                                                    zip(expected, lines)), (case, lines)
        assert not re.search(r'8259|8254|8251|OPN|PC-98|x86|loader|FPU|calib', '\n'.join(lines))
        s.q.call('quit')
        s.p.wait(timeout=5)
        assert s.p.returncode == 0
        print(('GREEN' if case in ('normal', 'invalid-crc') else 'RED') + f' {case} {cpu}')
    finally:
        s.close()


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
    work = Path(tempfile.mkdtemp(prefix='ppc-ver-', dir=os.environ.get('TMPDIR'))).resolve()
    user = work / 'user'
    bins = ' '.join(str(user / (name + '.bin')) for name in ('hello', 'fault', 'verprob'))
    seen = set()
    cases = ('normal', 'invalid-crc', 'ignore-crc', 'drop-pvr', 'absent')
    for case in cases if args.mutate else ('normal',):
        folder = work / case
        kernel = folder / 'kernel'
        kernel.mkdir(parents=True)
        settings = []
        if case in ('invalid-crc', 'ignore-crc'):
            settings = mutant_settings(folder, 'invalid-crc', 'platform/powermac/ver_backend.c',
                                       's->pvr = saved_pvr;',
                                       's->boot = (BootImageInfo){0}; s->pvr = saved_pvr;')
            if case == 'ignore-crc':
                source = next(Path(arg.split('=', 1)[1]) for arg in settings
                              if arg.startswith('KB_MUT_FILE='))
                body = source.read_text()
                anchor = 'if (!s->boot.crc_valid) return;'
                assert body.count(anchor) == 1
                source.write_text(body.replace(anchor, ''))
        elif case == 'drop-pvr':
            settings = mutant_settings(folder, case, 'platform/powermac/kernel_boot.c',
                                       '    pm_ver_note_pvr(pvr);', '')
        elif case == 'absent':
            (generate(kernel, 'ver-absent') / 'stamp').touch()
        elf = build(kernel, 'IMAGE=kboot-link-sh', 'KB_SH_OUT=' + str(user),
                    'KB_ROOT_BINS=' + bins, *settings, 'kb-user', 'kernel')
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert digest not in seen, (case, digest)
        seen.add(digest)
        print(f'{case} sha256={digest} {elf}', flush=True)
        if not args.build_only:
            for cpu in ('7450', '7455'):
                observe(elf, cpu, folder / cpu, case)
    print(f'{len(seen)} distinct version probe images')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
