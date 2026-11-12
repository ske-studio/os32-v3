#!/usr/bin/env python3
"""Exercise root images through the ISO9660 directory records."""
CHECK = dict(name='mkroot-image-host', tier=1, needs=[], budget=10,
    covers=['tools/mkroot_image.py', 'tools/tests/test_mkroot_image.py'])

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/mkroot_image.py'
BLOCK = 2048


def entries(image, record):
    extent = int.from_bytes(record[2:6], 'little')
    size = int.from_bytes(record[10:14], 'little')
    data = image[extent * BLOCK:extent * BLOCK + size]
    result = {}
    pos = 0
    while pos < len(data):
        length = data[pos]
        if not length:
            pos = (pos // BLOCK + 1) * BLOCK
            continue
        item = data[pos:pos + length]
        name = item[33:33 + item[32]].decode('ascii').split(';')[0].rstrip('.')
        result[name] = item
        pos += length
    return result


def verify(image, motd):
    pvd = image[16 * BLOCK:17 * BLOCK]
    assert pvd[:7] == b'\x01CD001\x01'
    assert len(image) == int.from_bytes(pvd[80:84], 'little') * BLOCK, 'padding detected'
    root = entries(image, pvd[156:])
    assert root['TMP'][25] & 2
    assert set(entries(image, root['TMP'])) == {'\x00', '\x01'}
    etc = entries(image, root['ETC'])
    rec = etc['MOTD']
    offset = int.from_bytes(rec[2:6], 'little') * BLOCK
    size = int.from_bytes(rec[10:14], 'little')
    assert image[offset:offset + size] == motd
    assert len(image) == offset + ((size + BLOCK - 1) // BLOCK) * BLOCK, 'padding detected'


def exercise(script, directory):
    motd = b'OS32 root\n'
    (directory / 'motd').write_bytes(motd)
    staging = directory / 'staging'
    (staging / 'etc').mkdir(parents=True)
    (staging / 'tmp').mkdir()
    (staging / 'etc' / 'motd').write_bytes(motd)
    for option, source in (('--motd', directory / 'motd'), ('--staging', staging)):
        out = directory / 'root.iso'
        proc = subprocess.run([sys.executable, '-B', str(script), option, str(source),
                               '--out', str(out)], capture_output=True, text=True)
        assert proc.returncode == 0, proc.stderr
        image = out.read_bytes()
        assert proc.stdout.strip() == str(len(image))
        verify(image, motd)

    app = directory / 'hello.bin'
    payload = b'OS32X app\x00'
    app.write_bytes(payload)
    out = directory / 'apps.iso'
    subprocess.run([sys.executable, '-B', str(script), '--motd', str(directory / 'motd'),
                    '--bin', str(app), '--out', str(out)], check=True, capture_output=True)
    image = out.read_bytes()
    root = entries(image, image[16 * BLOCK + 156:])
    rec = entries(image, root['BIN'])['HELLO.BIN']
    start = int.from_bytes(rec[2:6], 'little') * BLOCK
    size = int.from_bytes(rec[10:14], 'little')
    assert image[start:start + size] == payload


def main():
    if not (shutil.which('genisoimage') or shutil.which('xorriso')):
        print('SKIP: genisoimage or xorriso required')
        return 3
    with tempfile.TemporaryDirectory(prefix='test-mkroot-') as tmp:
        directory = Path(tmp)
        exercise(SOURCE, directory)
        if '--mutate' in sys.argv:
            source = SOURCE.read_text()
            anchor = "'-no-pad'"
            assert source.count(anchor) == 1
            mutant = directory / 'mutant.py'
            mutant.write_text(source.replace(anchor, "'-pad'"))
            try:
                verify_mutant(mutant, directory)
            except AssertionError as exc:
                assert str(exc) == 'padding detected', str(exc)
                print('RED runtime: padding detected (1)')
            else:
                raise AssertionError('padding mutant survived')
    print('PASS mkroot image')
    return 0


def verify_mutant(script, directory):
    out = directory / 'mutant.iso'
    proc = subprocess.run([sys.executable, '-B', str(script), '--motd', str(directory / 'motd'),
                           '--out', str(out)], capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    verify(out.read_bytes(), b'OS32 root\n')


def run(ctx):
    argv = [sys.executable, '-B', 'tools/tests/test_mkroot_image.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
