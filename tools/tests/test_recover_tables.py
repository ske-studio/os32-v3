"""S2a frozen C oracle, compiler-evaluated extraction and field comparison.

--extract FILE writes the oracle's versioned JSON records for the later table
producer. This command never rewrites the oracle. Each record is
[table, row, field, slot, value]; slot 0 denotes a scalar/reference, array
values use slots 1..count (guard fields use their element ordinal).
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys
import tempfile

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / 'tools/tests'
sys.path.insert(0, str(ROOT / 'tools'))
from recover_oracle import DATA, FLAGS, ORACLE, load_provenance, records, wrapper

COUNTS = {'rule_count': 578, 'target_count': 154, 'layout_count': 1039, 'expr_count': 8}


def preprocess(source):
    return subprocess.check_output(['gcc', *FLAGS, '-E', '-P', str(source)], text=True)


def compare(old, new):
    before = {tuple(row[:4]): row[4] for row in old}
    after = {tuple(row[:4]): row[4] for row in new}
    differences = [f'{key}: oracle={before.get(key)!r}, product={after.get(key)!r}'
                   for key in dict.fromkeys([*before, *after])
                   if key not in before or key not in after or before[key] != after[key]]
    if differences:
        raise ValueError('\n'.join(differences[:20]))


def expression_bodies(text):
    # These functions are retained unchanged by S2. Compare preprocessed bodies
    # as well as symbolic eval bindings, never function addresses across TUs.
    return dict(re.findall(r'^static int (\w+)(\([^\n]+\)\n\{.*?^\})', text, re.M | re.S))


def check_generator_bounds():
    sys.path.insert(0, str(ROOT / 'tools'))
    from gen_recover_tables import narrow
    for bits in (8, 16):
        limit = (1 << bits) - 1
        assert narrow(0xffffffff, bits, True) == f'REC_TABLE_NONE{bits}'
        assert narrow(0, bits, True) == '0x0U'
        narrow(limit - 1, bits, True)
        narrow(limit, bits)
        for value, sentinel in [(limit, True), (limit + 1, False), (-1, False)]:
            try:
                narrow(value, bits, sentinel)
            except ValueError:
                continue
            raise ValueError(f'narrow accepted {value}/{bits}/{sentinel}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract', type=pathlib.Path)
    args = parser.parse_args()
    provenance = load_provenance()
    old_text = ORACLE.read_text()
    source = ROOT / 'sys/dev/video/geforce/diag/recover_gf.c'
    new_text = preprocess(source)
    old_funcs, new_funcs = expression_bodies(old_text), expression_bodies(new_text)
    if not old_funcs or any(new_funcs.get(name) != body for name, body in old_funcs.items()):
        raise ValueError('expression implementation changed')
    with tempfile.TemporaryDirectory(prefix='recover-tables-') as tmp:
        temp = pathlib.Path(tmp)
        units = []
        for name, file, text in [('oracle', ORACLE, old_text), ('product', source, new_text)]:
            unit = temp / (name + '.c')
            unit.write_text(wrapper(file, text, name, TESTS / 'recover_tables_bounds.h' if name == 'product' else None))
            units.append(str(unit))
        exe = temp / 'host'
        subprocess.run(['gcc', *FLAGS, '-nostdlib', '-static', '-no-pie',
                        '-Wl,--gc-sections', *units, str(ROOT / 'sys/platform/pc98/kdebug/recover.c'),
                        str(DATA / 'recover_tables_host.c'), '-o', str(exe)], check=True)
        run = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
        if run.returncode:
            raise RuntimeError(f'host extraction failed rc={run.returncode}: {run.stderr!r}')
        sections = run.stdout.decode().split('END\n')
        if len(sections) != 3 or sections[-1]:
            raise ValueError('incomplete extraction')
        old, new = map(records, sections[:2])
        values = {tuple(row[:4]): row[4] for row in old}
        for field, count in COUNTS.items():
            if values.get(('profile', 0, field, 0)) != count:
                raise ValueError(f'oracle count mismatch: {field}')
        compare(old, new)
        check_generator_bounds()
        if args.extract:
            args.extract.write_text(json.dumps({'schema': 1, 'provenance': provenance,
                                               'records': old}, indent=2) + '\n')
        print(f'PASS recover tables: 578 rules / 154 targets / 1039 layouts; '
              f'{len(old)} fields; CRC={values[("profile", 0, "crc", 0)]:08x}; '
              'references and expression implementations unchanged', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
