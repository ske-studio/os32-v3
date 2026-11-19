"""Prepare KB_MUT_FILE for the internal P2 boot probe (KB_MUT_SRC=kernel/sqlite_static.c)."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def probe_source():
    source = (ROOT / 'kernel/sqlite_static.c').read_text()
    anchor = '            services_public = 1;'
    if source.count(anchor) != 1:
        raise ValueError('SQLite boot publication point changed')
    return ('static void sqlite_static_probe(void);\n' +
            source.replace(anchor, anchor + '\n            sqlite_static_probe();') +
            (ROOT / 'tools/tests/ppc_sqlite_static_probe.inc').read_text())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(probe_source())
