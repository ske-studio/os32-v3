#!/usr/bin/env python3
"""NOTEST: validate real selected-compile observations (never mock execution)."""
import argparse
import json
import posixpath
from pathlib import Path
REQUIRED = ['platform/powermac/' + name + '.c' for name in ('ofw','escc_tree','escc','boot')]
REQUIRED += ['lib/kstring_c.c', 'arch/ppc/entry.S', 'arch/ppc/ofw_note.S']


def forbidden_dependencies(text):
    paths = text.replace('\\\n', ' ').split()
    prefixes = ('arch/x86/', 'platform/pc98/', 'kernel/v86/', 'kernel/ime/',
                'drivers/ata/', 'drivers/block/', 'drivers/char/', 'drivers/dma/',
                'drivers/input/', 'drivers/net/', 'drivers/pci/', 'drivers/sound/',
                'drivers/video/')
    return [path for word in paths if (path := posixpath.normpath(word.rstrip(':'))) and
            any(path.startswith(prefix) for prefix in prefixes)]


def validate(report):
    errors = []
    selected = report.get('selected', [])
    units = report.get('units', [])
    if not selected or [row['source'] for row in units] != selected:
        errors.append('all selected translation units must be attempted in order')
    success = {row['source'] for row in units if row['status'] == 'success'}
    required = REQUIRED
    baseline = Path(__file__).with_name('selected_success.json')
    if baseline.is_file():
        required = list(dict.fromkeys(REQUIRED + json.loads(baseline.read_text())))
    for name in required:
        if name not in success:
            errors.append('required success missing: ' + name)
    if report.get('cc', [''])[0].split('/')[-1] != 'powerpc-elf-gcc':
        errors.append('compiler identity mismatch')
    observed = set()
    for row in units:
        if row['status'] == 'success':
            if row.get('returncode') != 0 or row.get('classification') != 'success':
                errors.append('inconsistent success: ' + row['source'])
            if row.get('elf') != dict(elf_class=1, endian='big', machine=20):
                errors.append('ELF identity mismatch: ' + row['source'])
            if not row.get('dependencies'):
                errors.append('dependency evidence missing: ' + row['source'])
            observed.update((row['source'], header) for header in
                            forbidden_dependencies(row.get('dependencies') or ''))
        elif row.get('returncode') == 0 or row.get('classification') == 'success':
            errors.append('inconsistent failure: ' + row['source'])
    known = json.loads(Path(__file__).with_name('known_header_dependencies.json').read_text())
    expected = {(entry['source'], entry['header']) for entry in known['dependencies']}
    for source, header in sorted(observed - expected):
        errors.append('new forbidden dependency: ' + source + ': ' + header)
    for source, header in sorted(expected - observed):
        errors.append('stale known dependency (remove after confirming success): ' + source + ': ' + header)
    return errors


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    errors = validate(json.loads(args.report.read_text()))
    print('\n'.join(errors) if errors else 'PASS selected PowerPC observations')
    raise SystemExit(bool(errors))
