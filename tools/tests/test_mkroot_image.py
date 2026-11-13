#!/usr/bin/env python3
"""Exercise root images through the ISO9660 directory records."""
CHECK = dict(name='mkroot-image-host', tier=1, needs=['make:all'], budget=10,
    covers=['build/core.yaml', 'userland/deploy.yaml', 'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/mkfat12.py', 'build/packages.yaml', 'tools/mkroot_image.py', 'tools/tests/test_mkroot_image.py',
            'tools/mkpkg.py', 'tools/mkfat12.py', 'build/packages.yaml'])

from pathlib import Path
import shutil
import os
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

    manifest = directory / 'manifest.json'
    for content in ('{"/SYS/A": "unused", "/SYS/A": "unused"}',
                    '{"/SYS/A.": "unused"}'):
        manifest.write_text(content)
        proc = subprocess.run([sys.executable, '-B', str(script), '--motd', str(directory / 'motd'),
                               '--manifest', str(manifest), '--out', str(directory / 'bad.iso')],
                              capture_output=True, text=True)
        assert proc.returncode == 1 and ('duplicate JSON key' in proc.stderr or
                                         'ISO 9660 level 1' in proc.stderr), proc.stderr

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


def plan_exercise(directory):
    import importlib.util
    sys.path.insert(0, str(ROOT / 'tools'))
    spec = importlib.util.spec_from_file_location('root_image', SOURCE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    seen = set()
    mod.check_collision('SYS/SHELL.BIN', seen)
    try:
        mod.check_collision('SYS/SHELL.BIN', seen)
    except ValueError as exc:
        assert 'collision' in str(exc)
    else:
        raise AssertionError('collision survived')
    # A-B.BIN and A_B.BIN pass a plain upper-case check, but the ISO tool would
    # silently rename one of them: names outside the d-characters are refused.
    seen = set()
    mod.check_collision('BIN/A_B.BIN', seen)
    try:
        mod.check_collision('BIN/A-B.BIN', seen)
    except ValueError as exc:
        assert 'ISO 9660 level 1' in str(exc)
    else:
        raise AssertionError('non-ISO name accepted')
    for bad in ('ABCDEFGH.ABC/X.BIN', 'ABCDEFGHI/X.BIN', 'BIN/ABCDEFGHI.BIN', 'BIN/A.BINX'):
        try:
            mod.check_collision(bad, set())
        except ValueError:
            pass
        else:
            raise AssertionError('non-ISO name accepted: ' + bad)
    mod.check_collision('ABCDEFGH/ABCDEFGH.ABC', set())
    assert mod.fd_free_bytes('2hd', 0, 0) > 64 * 1024
    assert mod.fd_free_bytes('144', 0, 0) > 64 * 1024
    out = directory / 'plan.iso'
    proc = subprocess.run([sys.executable, '-B', str(SOURCE), '--plan',
                           'build/packages.yaml', '--fd', '--out', str(out)],
                          cwd=ROOT, capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    assert f"ISO: {out.stat().st_size} bytes; contents:" in proc.stdout
    assert '2048-rounded contents:' in proc.stdout
    image = out.read_bytes()
    root = entries(image, image[16 * BLOCK + 156:])
    assert 'VMKRNL.LZ4' in root and 'LOADER.BIN' not in root
    assert 'SHELL.BIN' in entries(image, root['SYS'])
    assert 'FILETYPE' in entries(image, root['ETC'])
    for geometry, loader in [('2hd', 'boot/loader_fat_new.bin'),
                             ('144', 'boot/loader_fat144.bin')]:
        free = mod.fd_free_bytes(geometry, len(image), (ROOT / loader).stat().st_size)
        print(f'FD {geometry}: estimated free {free} bytes ({free // 1024} KB)')
        assert free >= mod.FD_MIN_FREE_KB * 1024, 'FD estimate below 64 KB'
    print(proc.stdout.strip())
    no_tool = subprocess.run([sys.executable, '-B', str(SOURCE), '--plan',
                              'build/packages.yaml', '--fd', '--out', str(out)],
                             cwd=ROOT, env={'PATH': ''}, capture_output=True, text=True)
    assert no_tool.returncode == 1


def main():
    if not (shutil.which('genisoimage') or shutil.which('xorriso')):
        print('SKIP: genisoimage or xorriso required')
        return 3
    with tempfile.TemporaryDirectory(prefix='test-mkroot-') as tmp:
        directory = Path(tmp)
        exercise(SOURCE, directory)
        plan_exercise(directory)
        if '--mutate' in sys.argv:
            source = SOURCE.read_text()
            for anchor, replacement in ((", object_pairs_hook=unique_object", ""),
                                        ("{1,3})?'", "{0,3})?'")):
                assert source.count(anchor) == 1
                mutant = directory / 'mutant.py'
                mutant.write_text(source.replace(anchor, replacement))
                os.environ['PYTHONPATH'] = str(ROOT / 'tools')
                manifest = directory / 'mutation.json'
                manifest.write_text('{"/SYS/A": "unused", "/SYS/A": "unused"}' if 'object_pairs' in anchor
                                    else '{"/SYS/A.": "unused"}')
                proc = subprocess.run([sys.executable, '-B', str(mutant), '--motd', str(directory / 'motd'),
                                       '--manifest', str(manifest), '--out', str(directory / 'bad.iso')],
                                      capture_output=True, text=True)
                assert 'No such file' in proc.stderr, proc.stderr
                print('RED runtime: manifest validation bypassed')
            anchor = "'-no-pad'"
            assert source.count(anchor) == 1
            mutant = directory / 'mutant.py'
            mutant.write_text(source.replace(anchor, "'-pad'"))
            os.environ['PYTHONPATH'] = str(ROOT / 'tools')
            try:
                verify_mutant(mutant, directory)
            except AssertionError as exc:
                assert str(exc) == 'padding detected', str(exc)
                print('RED runtime: padding detected (1)')
            else:
                raise AssertionError('padding mutant survived')
            anchor = '    if target in seen:'
            assert source.count(anchor) == 1
            mutant.write_text(source.replace(anchor, '    if False:'))
            import importlib.util
            spec = importlib.util.spec_from_file_location('mutant_root', mutant)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            seen = set()
            mod.check_collision('SYS/SHELL.BIN', seen)
            try:
                mod.check_collision('SYS/SHELL.BIN', seen)
            except ValueError:
                raise AssertionError('collision mutant failed to mutate')
            print('RED runtime: collision survived (1)')
            anchor = '    if not _D_NAME.fullmatch(leaf):'
            assert source.count(anchor) == 1
            mutant.write_text(source.replace(anchor, '    if False:'))
            spec = importlib.util.spec_from_file_location('mutant_root2', mutant)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            mod.check_collision('BIN/A-B.BIN', set())
            print('RED runtime: non-ISO name accepted (1)')
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
