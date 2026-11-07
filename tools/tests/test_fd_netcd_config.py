#!/usr/bin/env python3
"""I4 raw floppy configuration, preservation, replacement and runtime mutants."""
CHECK = dict(name='fd-netcd-config', tier=1, needs=[], budget=3,
             covers=['tools/fd_netcd_config.py', 'tools/mkfat12.py'])

import argparse
import contextlib
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import mkfat12 as fat


def files(image):
    # BPB-driven oracle, independent of the product's FAT helpers/writer.
    bps, spc, reserved, nfats, roots, total, media, fatsz = struct.unpack_from(
        '<HBHBHHBH', image, 11)
    table = image[reserved * bps:(reserved + fatsz) * bps]
    root = (reserved + nfats * fatsz) * bps
    data = root + ((roots * 32 + bps - 1) // bps) * bps
    size = bps * spc
    assert all(image[root - (i + 1) * len(table):root - i * len(table)] == table
               for i in range(nfats))

    def read(cluster):
        out, seen = b'', set()
        while 2 <= cluster < 0xff8:
            assert cluster not in seen
            seen.add(cluster)
            offset = data + (cluster - 2) * size
            out += image[offset:offset + size]
            value = int.from_bytes(table[cluster * 3 // 2:cluster * 3 // 2 + 2], 'little')
            cluster = value >> 4 if cluster & 1 else value & 0xfff
        return out

    result = {}

    def walk(raw, prefix):
        for i in range(0, len(raw), 32):
            entry = raw[i:i + 32]
            if not entry[0]:
                break
            if entry[0] in (0xe5, ord('.')) or entry[11] == 15:
                continue
            name = entry[:8].decode().rstrip()
            ext = entry[8:11].decode().rstrip()
            path = prefix + '/' + name + ('.' + ext if ext else '')
            cluster, length = struct.unpack_from('<HI', entry, 26)
            content = read(cluster) if cluster else b''
            if entry[11] & 16:
                walk(content, path)
            else:
                assert path not in result, 'duplicate directory entry'
                result[path] = content[:length]
    walk(image[root:root + roots * 32], '')
    return result


def exercise(script, work):
    for geometry in ('2hd', '144'):
        fat.select_geometry(geometry)
        for mode in ('no-etc', 'existing', 'full-dir'):
            builder = fat.Fat12Builder()
            fixture = work / 'payload'
            with contextlib.redirect_stdout(io.StringIO()):
                for path, content in [('/kernel.bin', b'IPL payload' * 300),
                                      ('/etc/system.cfg', b'KEEP=1\n')]:
                    if mode == 'no-etc' and path.startswith('/etc'):
                        continue
                    fixture.write_bytes(content)
                    builder.add_file_tree(path, str(fixture))
                if mode == 'existing':
                    fixture.write_bytes(b'old settings' * 180)
                    builder.add_file_tree('/etc/netcd.cfg', str(fixture))
                    fixture.write_bytes(b'after old config')
                    builder.add_file_tree('/etc/after.cfg', str(fixture))
                if mode == 'full-dir':
                    # Fill one /etc cluster to exercise the existing extension writer.
                    for i in range(fat.BYTES_PER_SECTOR // 32 - 3):
                        fixture.write_bytes(bytes([i]))
                        builder.add_file_tree(f'/etc/f{i}.txt', str(fixture))
            original = bytearray(builder.build())
            original[100:108] = b'IPL KEEP'
            source = work / f'{geometry}-{mode}.img'
            output = work / f'{geometry}-{mode}-out.img'
            source.write_bytes(original)
            argv = [sys.executable, '-B', str(script), str(source), '--output', str(output),
                    '--netcd', 'install', '--peer', '02:AB:00:00:00:01']
            run = subprocess.run(argv, capture_output=True)
            assert run.returncode == 0, run.stderr.decode()
            expected = files(original)
            expected['/ETC/NETCD.CFG'] = (
                b'NETCD=install\nSFS_LAN=1\nSFS_LAN_PEER=02:ab:00:00:00:01\n')
            changed = output.read_bytes()
            assert files(changed) == expected
            assert changed[:fat.BYTES_PER_SECTOR] == original[:fat.BYTES_PER_SECTOR]
            assert len(changed) == len(original)
            assert source.read_bytes() == original, 'input image changed'
            # Same input/output, including hard links, must be rejected.
            for target in (source, work / 'alias.img'):
                if target != source:
                    os.link(source, target)
                bad = argv.copy()
                bad[bad.index('--output') + 1] = str(target)
                assert subprocess.run(bad, capture_output=True).returncode != 0
                assert source.read_bytes() == original
                if target != source:
                    target.unlink()
            # A bad readback must cause failure even after a successful write.
            probe = (
                'import sys; from pathlib import Path; '
                'sys.path.insert(0, sys.argv[1]); import fd_netcd_config as c; '
                "c.Floppy.read_config = lambda self: b'corrupt'; "
                "c.configure(Path(sys.argv[2]), Path(sys.argv[3]), 'install', "
                "'02:ab:00:00:00:01')")
            bad_output = work / 'bad-readback.img'
            assert subprocess.run([sys.executable, '-B', '-c', probe,
                                   str(script.parent), str(source), str(bad_output)],
                                  capture_output=True).returncode != 0
            assert not bad_output.exists(), 'unverified output image remains'
    return 0


MUTATIONS = (
    ('netcd', 'NETCD={alias}', 'NETCD=wrong'),
    ('lan', 'SFS_LAN=1', 'SFS_LAN=0'),
    ('peer', '{peer.lower()}', '00:00:00:00:00:00'),
    ('input', "with output.open('xb') as stream:", "with source.open('wb') as stream:"),
    ('boot', 'result[:len(boot)] = boot', 'result[:len(boot)] = bytes(len(boot))'),
    ('allocation', 'fat.fat12_get(self.fat, cluster) == fat.FAT12_FREE', 'True'),
    ('replace', "info['index'] = index // fat.DIR_ENTRY_SIZE", "info['index'] = index"),
    ('geometry', "for name in fat.GEOMETRIES:", "for name in ('2hd',):"),
    ('readback', 'if Floppy(output.read_bytes()).read_config() != payload:',
     'if False:'),
    ('readback-cleanup', '        output.unlink()', '        pass'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'tools/fd_netcd_config.py').read_text()
    with tempfile.TemporaryDirectory(prefix='fd-netcd-') as tmp:
        work = Path(tmp)
        def check(source):
            case = work / 'case'
            case.mkdir()
            script = case / 'fd_netcd_config.py'
            script.write_text(source)
            shutil.copyfile(ROOT / 'tools/mkfat12.py', case / 'mkfat12.py')
            try:
                exercise(script, case)
            finally:
                shutil.rmtree(case)
        check(original)
        print('GREEN 2HD/144 write/readback/input + boot/files preserved + replace/extend')
        if args.mutate:
            for name, before, after in MUTATIONS:
                assert original.count(before) == 1, name
                try:
                    check(original.replace(before, after))
                except AssertionError:
                    print(f'RED {name}: runtime assertion')
                else:
                    raise RuntimeError(f'mutant survived: {name}')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
