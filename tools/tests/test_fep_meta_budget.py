#!/usr/bin/env python3
"""Measure two live FEP connections and the third resident probe, without libc."""
import argparse
from pathlib import Path
import re
import tempfile
import host32
from fep_host_build import ROOT, prepare, build, probe_source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='fep-meta-budget-') as directory:
        work = Path(directory)
        prepare(work, probe_source(), observe=True)
        body = (ROOT / 'tools/tests/fep_meta_budget_host.c').read_text()
        vfs = (work / 'vfs.c').read_text()
        cases = [('normal', vfs)]
        if args.mutate:
            for label, old, new in [
                ('third-group', 'end = kind == OS32_SQLITE_EXEC ? OS32_SQLITE_EXEC_GROUPS : OS32_SQLITE_GROUPS;',
                 'end = kind == OS32_SQLITE_EXEC ? OS32_SQLITE_EXEC_GROUPS : OS32_SQLITE_GROUPS - 1;'),
                ('app-gate', 'budget->context == BUDGET_APP &&', '0 &&')]:
                assert vfs.count(old) == 1, label
                cases.append((label, vfs.replace(old, new)))
        for label, source in cases:
            (work / 'vfs.c').write_text(source)
            text = body
            exe = build(work, text, label, flags=('-DFIXTURE_BYTES=8388608',))
            result = host32.run([str(exe)], capture_output=True, text=True, timeout=120)
            print(result.stdout + result.stderr, end='', flush=True)
            if label == 'normal':
                assert result.returncode == 0
                assert len(re.findall(r'^META_BUDGET variant=', result.stdout, re.M)) == 3
                assert 'META_BUDGET_MAX bytes=' in result.stdout
            else:
                assert result.returncode == 1 and 'fep_meta_probe' in result.stderr, (label, result)
                print('RED runtime ' + label, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
