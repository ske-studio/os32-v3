#!/usr/bin/env python3
"""Exercise the existing product gates with an external tool spy."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ENTRIES = ('boot sdk programs libs module modules deploy deploy-kernel kernel-r1 '
           'sqlite-module-proto kdebug hwdev kernel-link geforce-module deploy-test').split()


def invoke(root, target):
    return subprocess.run(['make', '-j1', '--no-print-directory', 'ARCH=ppc',
        'PLATFORM=powermac', 'BUILD_OUT=output', 'CC=./spy', 'AS=./spy', 'LD=./spy',
        'OBJCOPY=./spy', 'AR=./spy', target], cwd=root,
        capture_output=True, text=True, timeout=30)


def reject(root, target):
    before = {p.relative_to(root) for p in root.rglob('*') if p.is_file()}
    result = invoke(root, target)
    # Observe I/O before checking the message: gate mutants must fail here.
    assert not (root / 'calls').exists(), 'tool invoked: ' + target
    assert not (root / 'product').exists(), 'output created: ' + target
    after = {p.relative_to(root) for p in root.rglob('*') if p.is_file()}
    assert after == before, ('unexpected outputs', target, after - before)
    assert result.returncode != 0 and 'unsupported' in result.stderr, (target, result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    # Fix the x86 source and recipe contract against the agreed base.
    for name in ['build/boot.mk', 'build/targets/x86-pc98.mk'] + [
            str(p.relative_to(ROOT)) for p in (ROOT / 'boot').glob('*') if p.is_file()
            and p.suffix in ('.c', '.h', '.asm', '.inc', '.ld')]:
        baseline = subprocess.check_output(['git', 'show', '3ebbd208:' + name], cwd=ROOT)
        assert (ROOT / name).read_bytes() == baseline, name
    with tempfile.TemporaryDirectory(prefix='sep5-boot-') as directory:
        root = Path(directory)
        build_dir = Path('build/config.mk').parent
        shutil.copytree(ROOT / build_dir, root / build_dir,
                        ignore=shutil.ignore_patterns('out', 'sdk', '__pycache__'))
        # build/sdk-target.mk は config の読み込みで SDK の profile を引く (第 5 段 S1)。
        (root / 'sdk').mkdir(exist_ok=True)
        for name in ('sdk/target_profile.py', 'sdk/target_profile_cli.py'):
            shutil.copy2(ROOT / name, root / name)
        shutil.copy2(ROOT / 'Makefile', root / 'Makefile')
        # Existing make parse-time ownership stamp is fixture metadata.
        stamp = root / 'output/config/target'
        stamp.parent.mkdir(parents=True)
        stamp.write_text('ppc-powermac\n')
        (root / 'spy').write_text('#!/bin/sh\necho called >> calls\ntouch product\nexit 0\n')
        (root / 'spy').chmod(0o755)
        # Config may run host commands; compiler discovery must also be observed.
        env_path = root / 'cross/bin'
        env_path.mkdir(parents=True)
        for name in ('powerpc-elf-gcc', 'powerpc-elf-ld', 'powerpc-elf-ar',
                     'powerpc-elf-objcopy', 'nasm'):
            (env_path / name).symlink_to(root / 'spy')
        os.environ['CROSS_DIR'] = str(root / 'cross')
        for target in ENTRIES:
            reject(root, target)
        print('GREEN unsupported, zero calls/outputs: ' + ', '.join(ENTRIES))
        if args.mutate:
            gate = root / 'build/targets/ppc-powermac.mk'
            original = gate.read_text()
            for target in ENTRIES:
                # Override precisely one entry in the disposable product graph.
                gate.write_text(original + '\n' + target + ':\n\t@$(CC) -o product\n')
                try:
                    reject(root, target)
                except AssertionError as error:
                    assert str(error) == 'tool invoked: ' + target, error
                    assert (root / 'product').exists()
                    print('RED tool spy: ' + target)
                else:
                    raise AssertionError('surviving gate: ' + target)
                (root / 'calls').unlink()
                (root / 'product').unlink()
                gate.write_text(original)
                reject(root, target)
    print('PASS sep5 boot')


if __name__ == '__main__':
    main()
