"""Compare the projected inventory with an unrestricted dry-run on a miniature tree."""
import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent


def read_rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def check(root):
    out = root / 'out'
    def run(*cmd):
        return subprocess.check_output(cmd, cwd=root)
    run('git', 'init', '-q')
    for name, data in {
        'mm/unit.c': 'const char *where(void) {return __FILE__;}\n',
        'arch/unit.c': 'int other(void) {return 2;}\n',
        'kernel/use.h': '#include "../mm/unit.c"\n',
        'tools/probe.py': "CHECK = dict(covers=['mm/unit.c', 'arch/*.c'])\n"
                          "src = ROOT / 'arch' / 'unit.c'\n"
                          "if '--mutate' in args: src.replace(anchor, '')\n",
        'docs/tasks/restructure/P3.md': 'Move mm/unit.c next.\n',
        'sys/hal/gfx/v2/format.c': 'int format(void) {return 1;}\n',
        'docs/guide.md': 'See mm/unit.c or prefix/mm/unit.c\n',
        'build/list.mk': 'objs := $(OUT)/obj/mm/unit.o arch/unit.d\n',
    }.items():
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(data)
        run('git', 'add', name)
    run(sys.executable, '-B', 'tools/restructure/path_refs.py', str(out))
    reference = json.loads(run(sys.executable, '-B', 'tools/move_files.py', '--dry-run', str(out / 'map.json')))
    projected = [json.loads(s) for s in (out / 'dry-run.jsonl').read_text().splitlines()]
    plan = 'docs/tasks/restructure/P3.md'
    assert any(r['holder'] == plan and r['replacements'] for r in projected), 'current plan'
    for field in ('replacements', 'split_paths', 'unresolved_paths'):
        combined = [item for report in projected for item in report[field]]
        assert combined == reference[field], field
    refs = read_rows(out / 'refs.tsv')
    assert any(r['kind'] == 'covers' and r['token'] == 'mm/unit.c' for r in refs), 'covers evidence'
    assert any(r['kind'] == 'mutation operand' and '未確定' in r['evidence'] for r in refs), 'symbolic operand'
    assert any(r['holder'] == plan and r['token'] == 'mm/unit.c' for r in refs), 'plan reference'
    warnings = read_rows(out / 'unresolved.tsv')
    assert any(r['spelling'] == 'arch/*.c' for r in warnings), 'glob on mixed line'
    build = root / 'build/out'
    build.mkdir(parents=True)
    (build / 'deploy-set.json').write_text('{"files":[{"host":"build/out/unit.o"}]}')
    command = ['cc', '-g', '-c', 'mm/unit.c', '-o', 'build/out/unit.o']
    run(*command)
    gfx_command = ['cc', '-c', 'sys/hal/gfx/v2/format.c', '-o', 'sys/hal/gfx/v2/format.o']
    run(*gfx_command)
    (out / 'build.log').write_text(' '.join(command) + '\n' + ' '.join(gfx_command))
    run(sys.executable, '-B', 'tools/restructure/artifact_paths.py', str(out))
    rows = read_rows(out / 'artifact_paths.tsv')
    assert any(r['product'] == 'sys/hal/gfx/v2/format.o' and r['kind'] == 'STT_FILE'
               and r['source'] == 'sys/hal/gfx/v2/format.c' for r in rows), 'gfx object'
    assert any(r['kind'] == 'STT_FILE' and r['source'] == 'mm/unit.c' for r in rows), 'object attribution'
    assert any(r['kind'].startswith('runtime string') and r['spelling'] == 'mm/unit.c' for r in rows), 'runtime path'
    assert any(r['kind'] == 'DW_AT_name' for r in rows), 'debug info'
    (out / 'build.log').write_text('')
    run(sys.executable, '-B', 'tools/restructure/artifact_paths.py', str(out))
    assert any(r['kind'] == 'STT_FILE' and r['source'] == '未確定'
               for r in read_rows(out / 'artifact_paths.tsv')), 'ambiguous basename'


def exercise(mutation=None):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        (root / 'tools/restructure').mkdir(parents=True)
        shutil.copy(TOOLS / 'move_files.py', root / 'tools')
        for p in ('path_refs.py', 'artifact_paths.py'):
            shutil.copy(HERE / p, root / 'tools/restructure')
        if mutation:
            filename, anchor, replacement, reason = mutation
            p = root / 'tools/restructure' / filename
            code = p.read_text()
            assert code.count(anchor) == 1
            p.write_text(code.replace(anchor, replacement))
        try:
            check(root)
        except AssertionError as error:
            if mutation and str(error) == reason:
                print('runtime RED', reason)
                return
            raise
        if mutation:
            raise AssertionError('surviving mutant: ' + reason)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    from artifact_paths import sections
    assert any(name == '.text' and size > 0 for name, _, size in sections(Path('build/out/kernel.elf')))
    exercise()
    if args.mutate:
        for mutant in [
            ('path_refs.py', "name.startswith('docs/archive/')",
             "name.startswith(('docs/archive/', 'docs/tasks/restructure/'))", 'current plan'),
            ('artifact_paths.py', "if word.endswith(('.o', '.elf', '.a'))",
             "if word.startswith('build/out/') and word.endswith(('.o', '.elf', '.a'))", 'gfx object'),
            ('path_refs.py', "tags[child.lineno, child.value] = 'covers'",
             "tags[child.lineno, child.value] = 'tools'", 'covers evidence'),
            ('artifact_paths.py', "return UNKNOWN, 'no unique compile input'",
             "return value, 'basename only'", 'ambiguous basename'),
        ]:
            exercise(mutant)
    print('PASS path reference inventory')
