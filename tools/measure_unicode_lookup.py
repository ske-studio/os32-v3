#!/usr/bin/env python3
"""T3d0 disposable normal/R1 Unicode image probe (never deploys).

Run after a reproducible product build:
  SOURCE_DATE_EPOCH=$(git show -s --format=%ct <base>) make all MUTATE=0
  python3 tools/measure_unicode_lookup.py --base <base> --output "$TMPDIR/images"

Only --output is written. Two git-archive trees retain the exact base sources
and the prototype delta for review. Both builds use the base commit timestamp
and identity. No estimated deletion savings are credited: ready/probe/setter,
unicode.bin loading, registration and lease code all remain in the prototype.
The original unicode_to_jis body is actually replaced by a direct call. KAPI
is appended through the real generator in the disposable tree (version + 1,
not a reservation for d1). The product KAPI and sources are never edited.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(cmd, cwd, env, log):
    with log.open('w') as stream:
        result = subprocess.run(cmd, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'{cmd[0]} rc={result.returncode}: {log}')


def snapshot(base, dest):
    dest.mkdir()
    archive = subprocess.check_output(['git', 'archive', base], cwd=ROOT)
    subprocess.run(['tar', '-xf', '-', '-C', str(dest)], input=archive, check=True)
    # This Rust archive has no Unicode dependency. Reuse the product-built
    # archive, keeping its digest in the measurement record.
    rel = pathlib.Path('lib/os32_lz4/target/i686-os32-none/release/libos32_lz4.a')
    (dest / rel).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / rel, dest / rel)


def prototype(tree):
    candidate = (ROOT / 'tools/tests/unicode_lookup_candidate.c').read_text()
    (tree / 'lib/t3d0_unicode.c').write_text(candidate.replace('t3d0_unicode_to_jis', 'unicode_kernel_lookup'))
    path = tree / 'build/kernel.mk'
    body = path.read_text()
    assert body.count('lib/path.c lib/utf8.c') == 1
    path.write_text(body.replace('lib/path.c lib/utf8.c', 'lib/path.c lib/utf8.c lib/t3d0_unicode.c'))
    path = tree / 'lib/utf8.c'
    body = path.read_text()
    start = body.index('u16 unicode_to_jis(u32 cp)\n{')
    end = body.index('\n/* ======================================================================== */', start)
    path.write_text(body[:start] + '''extern u16 unicode_kernel_lookup(u32 cp);
u16 unicode_to_jis(u32 cp)
{
    return unicode_kernel_lookup(cp);
}
''' + body[end:])
    path = tree / 'sdk/kapi.json'
    schema = json.loads(path.read_text())
    schema['version'] += 1
    schema['includes'].append('utf8.h')
    slot = len(schema['api'])
    schema['api'].append({'name': 'unicode_to_jis', 'ret': 'u16', 'args': ['u32 cp']})
    path.write_text(json.dumps(schema, ensure_ascii=False, indent=2) + '\n')
    # The shared contract owns KAPI_VERSION (docs/KAPI_SPEC.md §3-1).
    path = tree / 'sdk/include/os32/os32_kapi_shared.h'
    body, count = re.subn(r'(#define KAPI_VERSION\s+)\d+',
                         lambda m: m[1] + str(schema['version']), path.read_text())
    assert count == 1
    path.write_text(body)
    return schema['version'], slot


def measure(tree, variant, env, logdir):
    out = tree / 'build/out'
    if variant == 'r1':
        out /= 'r1'
    image = out / 'vmkernel.lz4'
    # Re-run the unmodified production writer with its real loader limit.
    cmd = ['python3', 'tools/mkvmkernel.py', '--layout-header',
           str(tree / 'build/out/memmap_layout.h'), '--elf', str(out / 'kernel.elf'),
           '--kernel', str(out / 'kernel.bin'), '--sqlite', str(out / 'sqlite.bin'),
           '-o', str(image)]
    result = subprocess.run(cmd, cwd=tree, env=env, capture_output=True, text=True)
    (logdir / (variant + '-compress.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        # The production writer reports the exact compressed length before
        # refusing oversized output. A wider archival limit only retains the
        # same bytes for inspection; it does not change the measured gate.
        match = re.search(r'は (\d+) バイトで MAX_IMAGE_SIZE \((\d+) バイト', result.stderr)
        if not match:
            raise RuntimeError(result.stdout + result.stderr)
        size, limit = map(int, match.groups())
        archival = logdir / 'archive-only-limit.h'
        archival.write_text('#define MAX_IMAGE_SIZE (2 * 1024 * 1024)\n')
        run(cmd + ['--limit-header', str(archival)], tree, env,
            logdir / (variant + '-archive-compress.log'))
        assert image.stat().st_size == size
    else:
        size = image.stat().st_size
        limit = 508 * 1024
    # The retained ELF must have exactly one pair table of the promised size.
    nm = subprocess.check_output(['nm', '-S', str(out / 'kernel.elf')], text=True)
    pairs = [line.split() for line in nm.splitlines() if line.endswith(' unicode_jis_table')]
    if (tree / 'lib/t3d0_unicode.c').exists():
        assert len(pairs) == 1 and int(pairs[0][1], 16) == 28252, pairs
        assert pairs[0][2].lower() in ('d', 'r')  # base linker folds .rodata into .data
        assert ' wrap_unicode_to_jis' in nm and ' unicode_kernel_lookup' in nm
        objnm = subprocess.check_output(['nm', '-S', str(tree / 'lib/t3d0_unicode.o')], text=True)
        assert re.search(r'\b00006e5c r unicode_jis_table$', objnm, re.MULTILINE), objnm
    return {'bytes': size, 'limit': limit, 'margin': limit - size,
            'fits': size <= limit, 'sha256': sha(image),
            'kernel_raw_bytes': (out / 'kernel.bin').stat().st_size,
            'sqlite_raw_bytes': (out / 'sqlite.bin').stat().st_size,
            'image': str(image), 'elf': str(out / 'kernel.elf')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    base = subprocess.check_output(['git', 'rev-parse', args.base], cwd=ROOT, text=True).strip()
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    assert head == base, (head, base)
    output = args.output.resolve()
    assert not output.is_relative_to(ROOT), 'probe must live outside the product worktree'
    output.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ)
    env['SOURCE_DATE_EPOCH'] = subprocess.check_output(
        ['git', 'show', '-s', '--format=%ct', base], cwd=ROOT, text=True).strip()
    # gen_build_id.py must use the original clean base identity in both copies.
    env['GIT_DIR'] = subprocess.check_output(['git', 'rev-parse', '--absolute-git-dir'],
                                           cwd=ROOT, text=True).strip()
    env['GIT_WORK_TREE'] = str(ROOT)
    product = ROOT / 'build/out/vmkernel.lz4'
    before = sha(product)
    result = {'base': base, 'source_date_epoch': env['SOURCE_DATE_EPOCH'],
              'product_before_sha256': before,
              'candidate_sha256': sha(ROOT / 'tools/tests/unicode_lookup_candidate.c'),
              'rust_archive_sha256': sha(ROOT / 'lib/os32_lz4/target/i686-os32-none/release/libos32_lz4.a'),
              'deletion_credit_estimate': 0, 'images': {}}
    for name in ('baseline', 'prototype'):
        tree = output / name
        snapshot(base, tree)
        logs = output / (name + '-logs')
        logs.mkdir()
        if name == 'prototype':
            version, slot = prototype(tree)
            result['prototype_kapi_version'] = version
            result['prototype_kapi_slot'] = slot
            run(['python3', 'sdk/gen_kapi.py'], tree, env, logs / 'generate.log')
        for variant, target in (('normal', 'kernel'), ('r1', 'kernel-r1')):
            # If the real image gate rejects the prototype, finish building
            # its ELF/bin once and let measure() record the rejection below.
            with (logs / (variant + '-build.log')).open('w') as stream:
                rc = subprocess.run(['make', target, 'MUTATE=0'], cwd=tree, env=env,
                                    stdout=stream, stderr=subprocess.STDOUT).returncode
            if rc and not re.search(r'は \d+ バイトで MAX_IMAGE_SIZE',
                                    (logs / (variant + '-build.log')).read_text()):
                raise RuntimeError(f'{name}/{variant}: build rc={rc}, see {logs}')
            row = measure(tree, variant, env, logs)
            result['images'][name + '/' + variant] = row
            print(f'{name}/{variant}: {row["bytes"]} B, margin={row["margin"]} B', flush=True)
            (output / 'measurements.json').write_text(json.dumps(result, indent=2) + '\n')
    result['product_after_sha256'] = sha(product)
    result['product_matches_base_build'] = (
        before == result['product_after_sha256'] == result['images']['baseline/normal']['sha256'])
    result['d1_budget_gate'] = all(result['images']['prototype/' + v]['fits'] for v in ('normal', 'r1'))
    (output / 'measurements.json').write_text(json.dumps(result, indent=2) + '\n')
    assert result['product_matches_base_build'], 'product differs from isolated base build'
    print('d1 budget gate: ' + ('PASS' if result['d1_budget_gate'] else 'STOP'))


if __name__ == '__main__':
    main()
