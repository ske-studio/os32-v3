"""Verify byte provenance, planned function ranges and the selected SF closure."""
CHECK = dict(name='nofpu-upstream', tier=1, needs=[], budget=10,
    covers=['tools/tests/test_nofpu_upstream.py', 'lib/softfloat/**',
            'arch/x86/kernel/x87_emu_vendor/**', 'arch/x86/kernel/x87_emu*.c'], kind='contract')

from pathlib import Path
import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SF = ROOT / 'lib/softfloat'
X87 = ROOT / 'arch/x86/kernel/x87_emu_vendor'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_root(manifest):
    # Override only the parent NP21 checkout, so CI can exercise missing-source
    # SKIP without rewriting the pinned provenance manifest.
    override = os.environ.get('NOFPU_UPSTREAM_ROOT')
    if override is None:
        return Path(manifest['source_root'])
    suffix = Path(manifest['source_root']).relative_to('/home/hight/np21w-src')
    return Path(override) / suffix


def slice_bytes(data, entry):
    lines = data.splitlines(keepends=True)
    first, last = entry['line_start'], entry['line_end']
    assert 1 <= first <= last <= len(lines), entry
    return b''.join(lines[first-1:last])


def verify_softfloat(manifest):
    source = source_root(manifest)
    available = source.is_dir()
    assert len(manifest['files']) == len(set(e['source_file'] for e in manifest['files']))
    selected = set(manifest['selected_sources'])
    assert selected.isdisjoint(manifest['excluded_sources'])
    assert {e['source_file'] for e in manifest['files']} == (
        selected | set(manifest['headers']) | {'COPYING.txt'})
    for entry in manifest['files']:
        data = (ROOT / entry['destination']).read_bytes()
        assert digest(data) == entry['sha256'], entry['destination']
        assert len(data) == entry['bytes'], entry['destination']
        if available:
            original = (source / entry['source_file']).read_bytes()
            assert digest(original) == entry['raw_sha256'], entry['source_file']
            for patch in entry['patches']:
                before, after = patch['before'].encode(), patch['after'].encode()
                assert original.count(before) == 1, entry['source_file']
                original = original.replace(before, after)
            assert original == data, entry['source_file']
    if available:
        assert selected | set(manifest['excluded_sources']) == {p.name for p in source.glob('*.c')}
    print('SoftFloat installed hashes/port: GREEN; original hashes: ' +
          ('GREEN' if available else f'SKIP (source absent: {source})'))


def verify_x87(manifest):
    source = source_root(manifest)
    available = source.is_dir()
    for key in ('license_file', 'notice'):
        entry = manifest[key]
        data = (ROOT / entry['destination']).read_bytes()
        assert digest(data) == entry['raw_sha256'], key
        if available:
            original = (source / entry['source_file']).read_bytes()
            if 'line_start' in entry:
                original = slice_bytes(original, entry)
            assert original == data, key
    if not available:
        print(f'x87 original file/function hashes: SKIP (source absent: {source})')
        return
    raw = (source / manifest['source_file']).read_bytes()
    assert digest(raw) == manifest['raw_sha256']
    names = set()
    for entry in manifest['functions'] + manifest['supporting_data']:
        piece = slice_bytes(raw, entry)
        assert digest(piece) == entry['raw_sha256'], entry
        if 'function' in entry:
            assert entry['function'] not in names
            names.add(entry['function'])
            assert (entry['function'] + '(').encode() in piece
            if entry['destination']:
                assert entry['status'] in ('planned B/C port', 'ported ESC0-3', 'ported ESC4-7', 'partial port')
                if entry['status'] != 'planned B/C port':
                    installed = ROOT / entry['destination']
                    assert installed.is_file(), installed
                    assert 'Redistribution and use in source and binary forms' in installed.read_text()
            else:
                assert entry['status'] == 'excluded'
    assert {f'SF_ESC{i}' for i in range(8)} <= names
    print(f'x87 file/license and {len(names)} function hashes: GREEN')


def closure(manifest):
    cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin'
    cc = str(cross / 'i386-elf-gcc') if (cross / 'i386-elf-gcc').is_file() else 'gcc'
    nm = str(cross / 'i386-elf-nm') if (cross / 'i386-elf-nm').is_file() else 'nm'
    defined, undefined = set(), set()
    with tempfile.TemporaryDirectory(prefix='nofpu-closure-') as directory:
        for name in manifest['selected_sources']:
            obj = Path(directory) / (name + '.o')
            subprocess.run([cc, '-m32', *manifest['closure']['flags'],
                            '-I'+str(SF), '-c', str(SF / name), '-o', str(obj)], check=True)
            symbols = subprocess.check_output([nm, '-g', str(obj)], text=True)
            for line in symbols.splitlines():
                fields = line.split()
                if len(fields) == 2 and fields[0] == 'U':
                    undefined.add(fields[1])
                elif len(fields) == 3:
                    defined.add(fields[2])
    assert undefined - defined == set(manifest['closure']['external_integer_helpers'])
    assert set(manifest['roots']) <= defined
    assert defined == set(manifest['closure']['defined_symbols'])
    print(f'SoftFloat {len(manifest["selected_sources"])} source object closure: GREEN')


def main():
    sf = json.loads((SF / 'UPSTREAM.json').read_text())
    x87 = json.loads((X87 / 'UPSTREAM.json').read_text())
    verify_softfloat(sf)
    verify_x87(x87)
    closure(sf)
    if '--mutate' in sys.argv:
        broken = copy.deepcopy(sf)
        broken['files'][0]['sha256'] = '0' * 64
        try:
            verify_softfloat(broken)
        except AssertionError:
            print('corrupt-installed-hash: integrity RED')
        else:
            raise AssertionError('corrupt hash survived')
        if source_root(x87).is_dir():
            broken = copy.deepcopy(x87)
            broken['functions'][0]['line_end'] -= 1
            try:
                verify_x87(broken)
            except AssertionError:
                print('truncated-function-range: integrity RED')
            else:
                raise AssertionError('truncated range survived')
        else:
            print('SKIP truncated-function-range: source absent')


def run(ctx):
    import os
    args = ['python3', '-B', 'tools/tests/test_nofpu_upstream.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    main()
