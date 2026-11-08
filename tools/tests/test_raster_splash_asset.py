"""S5D: local asset generation and optional HDD/hsync/package sets."""
CHECK = dict(name='raster-splash-asset-host', tier=1,
    covers=['.gitignore', 'tools/raster_splash_asset.py', 'build/core.yaml',
            'build/packages.yaml', 'tools/img2osri.py', 'tools/osri_*.py',
            'tools/osri_enc2_core.c', 'gfx/osri_profile.h',
            'tools/deploy_manifests.py', 'tools/mkpkg.py', 'tools/hostdrv_deploy.py',
            'tools/nhd_deploy.py', 'tools/nhd_profile.py', 'tools/np21w_ctl.py',
            'tools/pc98pt.py', 'tools/gen_deploy_set.py',
            'tools/deploy_protect.py', 'userland/deploy.yaml',
            'tools/tests/test_raster_splash_asset.py'],
    needs=['make:all', 'make:osri-enc2-core'], budget=3.3)

import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch
import zlib
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import deploy_manifests as dm
import mkpkg

TOOL = ROOT / 'tools/raster_splash_asset.py'


def module(text):
    spec = importlib.util.spec_from_file_location('asset_under_test', TOOL)
    result = importlib.util.module_from_spec(spec)
    exec(compile(text, str(TOOL), 'exec'), result.__dict__)
    return result


def rejected(fn):
    try:
        fn()
    except ValueError:
        return
    raise AssertionError('unsafe publication accepted')


def crc_input(data):
    copy = bytearray(data)
    copy[48:52] = bytes(4)
    return copy


def fixture():
    event = struct.pack('<HBBHH', 4, 1, 0, 0x345, 0)
    header = struct.pack('<4sHHHHBBHIIIIIIHHHHIIII',
        b'OSRI', 2, 64, 640, 400, 4, 1, 0, 128104, 64, 96,
        128000, 128096, 1, 8, 0, 1, 0, 0, 0, 0, 0)
    data = bytearray(header + bytes(32 + 128000) + event)
    struct.pack_into('<I', data, 48, zlib.crc32(data))
    return bytes(data)


def publication(m):
    data = fixture()
    expected = dict(sha256=m.sha(data), crc32=f'{zlib.crc32(crc_input(data)):08x}',
                    bytes=128104, events=1, max_outs=3, total_outs=3)
    assert m.statistics(data) == expected
    corrupt = bytearray(data); corrupt[96] ^= 1
    rejected(lambda: m.statistics(corrupt))
    # Valid but different pixels: the comparison must cover more than metadata.
    different = bytearray(corrupt); different[48:52] = bytes(4)
    struct.pack_into('<I', different, 48, zlib.crc32(different))
    with tempfile.TemporaryDirectory(prefix='asset-test-') as td:
        root = Path(td); source = root / 'source.png'; source.write_bytes(b'original')
        reference = root / 'baseline.osri'; reference.write_bytes(data)
        output = root / 'splash.osri'
        emitted = data
        real_run = subprocess.run

        def encode(command, **kwargs):
            if command[0] == 'git':
                return real_run(command, **kwargs)
            assert command[command.index('--outs')+1] == '31'
            assert command[command.index('--threads')+1] == '2'
            Path(command[command.index('-o')+1]).write_bytes(emitted)

        with patch.object(m.subprocess, 'run', side_effect=encode):
            m.generate(source, output, reference, threads=2)
            assert output.read_bytes() == data and source.read_bytes() == b'original'
            record = json.loads(output.with_suffix('.manifest.json').read_text())
            assert record['output'] == expected
            assert record['source_sha256'] == m.sha(b'original')
            assert record['threads'] == 2
            for rel, digest in record['generator_inputs'].items():
                assert m.sha((ROOT / rel).read_bytes()) == digest
            subprocess.run(['git', 'cat-file', '-e', record['generator_commit']+'^{commit}'], check=True)
            sidecar = output.with_suffix('.manifest.json')
            old_sidecar = sidecar.read_bytes()
            rejected(lambda: m.generate(source, output, reference, threads=2))
            emitted = bytes(different)
            rejected(lambda: m.generate(source, output, reference, threads=2, replace=True))
            assert output.read_bytes() == data and sidecar.read_bytes() == old_sidecar
            emitted = bytes(corrupt)
            rejected(lambda: m.generate(source, output, reference, threads=2, replace=True))
            assert output.read_bytes() == data and sidecar.read_bytes() == old_sidecar
            emitted = data
            output.write_bytes(b'old distribution')
            m.generate(source, output, reference, threads=2, replace=True)
            assert output.read_bytes() == data and source.read_bytes() == b'original'
            rejected(lambda: m.generate(source, source, reference, threads=2, replace=True))
            rejected(lambda: m.generate(source, reference, reference, threads=2, replace=True))


def distribution(core):
    merged = dm.load_merged(dm.CORE_MANIFEST_RELPATHS)
    merged['filesystem']['files'] = core['filesystem']['files'] + [
        e for e in merged['filesystem']['files'] if e['_manifest'] != 'build/core.yaml']
    rows = merged['filesystem']['files']
    splash = [e for e in rows if e.get('guest') == '/boot/splash.osri']
    assert len(splash) == 1 and splash[0]['host'] == 'assets/splash/splash.osri'
    assert not any(e.get('guest') == '/boot/user.osri' for e in rows)
    # Both cases use an isolated source tree, never the private local image.
    with tempfile.TemporaryDirectory(prefix='splash-deploy-') as td:
        os.environ['HOSTDRV_DIR'] = str(Path(td) / 'dest')
        import hostdrv_deploy as hd
        import nhd_deploy as nhd
        root = Path(td) / 'source'; root.mkdir()
        dest = Path(td) / 'dest'; (dest / 'boot').mkdir(parents=True)
        (dest / 'boot/user.osri').write_bytes(b'keep user image')
        asset = root / 'assets/splash/splash.osri'
        asset.parent.mkdir(parents=True)
        plan = mkpkg.load_plan(str(ROOT / 'build/packages.yaml'))
        for present in (False, True):
            if present:
                asset.write_bytes(fixture())
            expected_pairs = [('assets/splash/splash.osri', '/boot/splash.osri')] if present else []
            assert dm.resolve_entry(splash[0], str(root)) == expected_pairs
            # Package assignment and both deploy resolvers must agree.
            packages, problems = mkpkg.plan_packages(plan, str(root), manifest=merged)
            assert not problems, problems
            owners = [name for name, _, files in packages
                      if any(g == '/boot/splash.osri' for g, _ in files)]
            assert owners == (['normal'] if present else []), owners
            fd, problems = mkpkg.fd_plan(plan, str(root), loader='boot/loader_fat_new.bin', manifest=merged)
            assert not problems and not any(h == 'assets/splash/splash.osri' for _, h in fd)
            cfg = dict(filesystem=dict(directories=['/boot'], files=splash))
            with patch.object(hd, 'PROJ_DIR', str(root)), patch.object(nhd, 'PROJ_DIR', str(root)), \
                 patch.object(hd, 'HOSTDRV_DIR', str(dest)), patch.object(hd, 'load_deploy_yaml', return_value=cfg):
                expected_abs = [(str(asset), '/boot/splash.osri')] if present else []
                assert hd.resolve_files_from_entry(splash[0]) == expected_abs
                assert nhd.resolve_files_from_entry(splash[0]) == expected_abs
                assert hd.do_sync(write_manifest=True)
                assert (dest / 'boot/user.osri').read_bytes() == b'keep user image'
                manifest = (dest / '.deploy/manifest.txt').read_text()
                assert ('boot/splash.osri 128104 ' in manifest) == present
                assert 'user.osri' not in manifest
                if present:
                    assert (dest / 'boot/splash.osri').read_bytes() == fixture()
                else:
                    assert not (dest / 'boot/splash.osri').exists()


def ignored():
    paths = ['assets/splash/splash.osri', 'assets/splash/splash.manifest.json']
    result = subprocess.check_output(['git', 'check-ignore', '--no-index', *paths], cwd=ROOT, text=True)
    assert result.splitlines() == paths


def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args = p.parse_args()
    text = TOOL.read_text(); core = yaml.safe_load((ROOT / 'build/core.yaml').read_text())
    ignored(); publication(module(text)); distribution(core)
    print('GREEN generated fixture SHA/CRC/OUT, guarded publication, optional HDD/hsync/NORMAL, FD exclusion, ignored local assets', flush=True)
    if args.mutate:
        for name, old, new in (
            ('overwrite without flag', 'if not replace and (output.exists() or manifest.exists()):',
             'if False and (output.exists() or manifest.exists()):'),
            ('omit reference comparison', 'if data != baseline:', 'if False:'),
            ('omit independent decode', '_, _, _, outs = decode(data, budget=OSRI_DEFAULT_OUT_BUDGET)', 'outs = [0]'),
        ):
            assert text.count(old) == 1
            try:
                publication(module(text.replace(old, new)))
            except AssertionError:
                print('RED '+name+': runtime assertion', flush=True)
            else:
                raise AssertionError('survived: '+name)
        import copy
        for name, tag in [('FD/MINIMAL injection', 'core'), ('deployment removal', None),
                          ('require private image', 'required')]:
            changed = copy.deepcopy(core)
            row = next(e for e in changed['filesystem']['files'] if e.get('guest') == '/boot/splash.osri')
            if tag == 'required':
                row.pop('type', None)
            elif tag:
                row['tags'] = [tag]
            else:
                changed['filesystem']['files'].remove(row)
            try:
                distribution(changed)
            except AssertionError:
                print('RED '+name+': runtime assertion', flush=True)
            else:
                raise AssertionError('survived: '+name)
        publication(module(text)); distribution(core)
        print('GREEN restored asset and distribution', flush=True)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_raster_splash_asset.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
