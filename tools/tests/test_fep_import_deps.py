#!/usr/bin/env python3
"""Rebuild paired FEP metadata after an import or facade declaration changes."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

CHECK = dict(name='fep-import-deps-host', tier=1, needs=['make:kernel'], budget=1,
             kind='characterization',
             covers=['build/fep-common.mk', 'tools/gen_sqlite_imports.py',
                     'tools/gen_kdebug_imports.py', 'tools/module_schema.py', 'sdk/kapi.json',
                     'sys/kern/module/kdebug_imports.def', 'lib/sqlite3/os32_sqlite_vfs.c',
                     'tools/mkmod.py', 'tools/prototypes/fep_imports.def',
                     'tools/prototypes/sqlite_imports.def', 'tools/tests/test_fep_import_deps.py'])
ROOT = Path(__file__).resolve().parents[2]


def exercise(work, mutation):
    for name in ('build/fep-common.mk', 'tools/gen_sqlite_imports.py',
                 'tools/gen_kdebug_imports.py', 'tools/module_schema.py', 'sdk/kapi.json'):
        dest = work / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, dest)
    if mutation:
        filename, anchor, replacement = mutation
        path = work / filename
        source = path.read_text()
        assert source.count(anchor) == 1
        path.write_text(source.replace(anchor, replacement))
    definition = work / 'tools/prototypes/fep_imports.def'
    definition.parent.mkdir()
    definition.write_text(json.dumps(dict(schema=1, contracts=['ops.h'], imports=[
        dict(symbol='sample', context='normal', irq=False, source='provider.h')])))
    (work / 'ops.h').write_text('struct ops { int value; };\n')
    (work / 'provider.h').write_text('int sample(int value);\n')
    metadata = work / 'out/fep_imports.json'

    def build():
        subprocess.run(['make', '-s', '-f', 'build/fep-common.mk', 'FEP_OUT=out',
                        'ARCH=ppc', 'PLATFORM=powermac', 'out/fep_imports.json'],
                       cwd=work, check=True)
        return json.loads(metadata.read_text())

    query = subprocess.check_output(
        ['make', '-s', '-f', 'build/fep-common.mk', 'FEP_OUT=out',
         'ARCH=ppc', 'PLATFORM=powermac',
         '--eval=query: ; @echo -Iinclude', 'query'], cwd=work, text=True)
    assert query == '-Iinclude\n' and not metadata.exists(), 'query ran generator'
    before = build()
    (work / 'provider.h').write_text('long sample(int value);\n')
    after = build()
    assert after['imports'][0]['signature'] == 'long sample(int value);', 'stale signature'
    (work / 'ops.h').write_text('struct ops { long value; };\n')
    assert build()['module_abi_gen'] != after['module_abi_gen'], 'stale facade'
    assert after['module_abi_gen'] != before['module_abi_gen']
    (work / 'out/fep_imports.d').unlink()
    (work / 'provider.h').write_text('short sample(int value);\n')
    assert build()['imports'][0]['signature'] == 'short sample(int value);', 'lost depfile'
    assert (work / 'out/fep_imports.d').is_file()


def run(ctx):
    args = [sys.executable, '-B', __file__]
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        exercise(Path(directory), None)
    if args.mutate:
        generator = 'tools/gen_sqlite_imports.py'
        inputs = "set(includes).union(doc.get('contracts', []))"
        for mutant, reason in [
                ((generator, inputs, "doc['contracts']"), 'stale signature'),
                ((generator, inputs, 'includes'), 'stale facade'),
                (('build/fep-common.mk', '$(FEP_GEN): fep-missing-depfile',
                  ''), 'lost depfile'),
                (('build/fep-common.mk', '-include $(wildcard $(FEP_OUT)/fep_imports.d)',
                  '$(FEP_OUT)/fep_imports.d: $(FEP_GEN)\n-include $(FEP_OUT)/fep_imports.d'),
                 'query ran generator')]:
            with tempfile.TemporaryDirectory() as directory:
                try:
                    exercise(Path(directory), mutant)
                except AssertionError as error:
                    if str(error) != reason:
                        raise
                    print('RED', reason)
                else:
                    raise AssertionError(f'surviving mutant: {mutant}')
