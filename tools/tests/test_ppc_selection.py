#!/usr/bin/env python3
"""Offline selection/argv/dependency checks; never stand in for real compilation."""
import argparse
import json
import importlib.util
import copy
from pathlib import Path
import shutil
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_profile import query
from build_fixture import copy_build_tree
from ppc.check_compile import forbidden_dependencies
from ppc.check_accept import REQUIRED, validate
ROOT = Path(__file__).resolve().parents[2]


def check(root):
    assignments = ['ARCH=ppc', 'PLATFORM=powermac']
    p = query(root, assignments, config_only=True)
    for source in REQUIRED:
        assert source in p['C_KERNEL'] + p['GAS_KERNEL'], source
    for source in p['C_KERNEL'] + p['GAS_KERNEL']:
        path = root / source
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    commands = query(root, assignments, commands=True)['argv']
    assert len(commands) == len(p['C_KERNEL']) + len(p['GAS_KERNEL'])
    for argv in commands:
        assert p['RAW_CC'][0].endswith('/powerpc-elf-gcc')
        assert argv[:len(p['CC'])] == p['CC']
        assert '-march=i386' not in argv
        if argv[argv.index('-c') + 1].endswith('.S'):
            assert '-include' not in argv
            assert '-mbig-endian' in argv
    assert forbidden_dependencies('out.o: lib/kstring_c.c arch/x86/arch_io.h\n') == ['arch/x86/arch_io.h']
    assert forbidden_dependencies('out.o: platform/pc98/legacy/v86_io.h\n') == ['platform/pc98/legacy/v86_io.h']
    assert forbidden_dependencies('out.o: arch/x86/legacy/v86.h\n') == ['arch/x86/legacy/v86.h']
    assert not forbidden_dependencies('out.o: fs/ext2/ext2.c drivers/base/dev.h\n')
    assert validate(dict(cc=['powerpc-elf-gcc'], selected=[], units=[]))


def dependency_checks(root, mutate):
    directory = root / 'audit'
    directory.mkdir()
    for name in ('check_accept.py', 'selected_success.json', 'known_header_dependencies.json'):
        shutil.copy2(ROOT / 'tools/ppc' / name, directory / name)
    spec = importlib.util.spec_from_file_location('isolated_accept', directory / 'check_accept.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    listing = directory / 'known_header_dependencies.json'
    known = json.loads(listing.read_text())
    sources = list(dict.fromkeys(REQUIRED + json.loads((directory / 'selected_success.json').read_text())))
    report = dict(cc=['powerpc-elf-gcc'], selected=sources, units=[])
    for source in sources:
        headers = [entry['header'] for entry in known['dependencies'] if entry['source'] == source]
        # Duplicate and parent-directory spellings must normalize to the same pair.
        deps = [header for h in headers for header in (h, './unused/../' + h)]
        report['units'].append(dict(source=source, status='success', returncode=0,
            classification='success', elf=dict(elf_class=1, endian='big', machine=20),
            dependencies='out.o: ' + source + ' ' + ' '.join(deps)))
    assert not audit.validate(report), audit.validate(report)
    reduced = copy.deepcopy(report)
    source = known['dependencies'][0]['source']
    header = known['dependencies'][0]['header']
    row = next(row for row in reduced['units'] if row['source'] == source)
    # Drop only the first known pair; the same source may own several pairs.
    kept = [entry['header'] for entry in known['dependencies'][1:] if entry['source'] == source]
    row['dependencies'] = 'out.o: ' + ' '.join([source] + kept)
    assert any('stale known dependency' in error for error in audit.validate(reduced))
    trimmed = copy.deepcopy(known)
    trimmed['dependencies'].pop(0)
    listing.write_text(json.dumps(trimmed))
    assert not audit.validate(reduced), audit.validate(reduced)
    listing.write_text(json.dumps(known))
    missing = copy.deepcopy(report)
    next(row for row in missing['units'] if row['source'] == source)['dependencies'] = None
    assert any('dependency evidence missing' in error for error in audit.validate(missing))
    if mutate:
        listing.write_text(json.dumps(trimmed))
        assert any('new forbidden dependency' in error for error in audit.validate(report))
        print('RED: deleting one known pair rejected at runtime')
        listing.write_text(json.dumps(known))
        assert not audit.validate(report)
        extra = copy.deepcopy(report)
        extra['units'][0]['dependencies'] += ' arch/x86/new_dependency.h'
        assert any('new forbidden dependency' in error for error in audit.validate(extra))
        print('RED: adding a new dependency rejected at runtime')
        assert not audit.validate(report)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        copy_build_tree(root)
        (root / 'Makefile').write_text('include build/config.mk\ninclude build/kernel-objects.mk\n')
        check(root)
        dependency_checks(root, args.mutate)
        if args.mutate:
            profile = root / 'build/arch/ppc-sources.mk'
            source = profile.read_text()
            anchor = 'arch/ppc/ofw_note.S'
            assert source.count(anchor) == 1
            profile.write_text(source.replace(anchor, ''))
            try:
                check(root)
            except AssertionError:
                print('RED: missing required GAS TU rejected at runtime')
            else:
                raise SystemExit('mutation survived')
    print('PASS PPC selection')
