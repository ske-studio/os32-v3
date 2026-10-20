#!/usr/bin/env python3
"""TASK_FONTPERF F1: the boot preload list of the .g16 bundle (design §4).

  python3 tools/g16/gen_preload16.py --ttf assets/fonts/JF-Dot-Shinonome16.ttf \\
      -o build/out/preload16.txt

Order (duplicates dropped, only code points the bundle holds, up to --max):
  1. JIS X 0208 rows 1-8 (non-kanji): EUC-JP 0xA1A1..0xA8FE decoded by
     Python's euc_jp codec, in code order;
  2. the GUI's own characters, tools/g16/ui_chars.txt (scan_ui_chars.py);
  3. kanji of IPADIC 2.7.0 (assets/ipadic/*.csv, the FEP dictionary; THIRD_PARTY.md)
     ordered by the lowest word cost of any surface form containing them
     (lower cost = more frequent), ties by code point.
Output: one hex code point per line, `XXXX  c` (gen_g16.py reads the first field).
Deterministic: inputs are the repository's files and the fetched TTF only.
"""
import argparse
import csv
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from gen_g16 import full_path  # noqa: E402  (the same Full-path rule as the bundle)

PRELOAD_MAX = 1600            # design §4: half of the gshell 3,200-slot cache
UI_CHARS = HERE / 'ui_chars.txt'
IPADIC = ROOT / 'assets/ipadic'
IPADIC_COST_FIELD = 3         # surface, left id, right id, cost, ...


def jis_rows_1_8():
    out = []
    for hi in range(0xA1, 0xA9):
        for lo in range(0xA1, 0xFF):
            try:
                s = bytes([hi, lo]).decode('euc_jp')
            except UnicodeDecodeError:
                continue
            if len(s) == 1:
                out.append(ord(s))
    return out


def ui_chars(path=UI_CHARS):
    out = []
    for line in path.read_text(encoding='utf-8').splitlines():
        fields = line.split()
        if fields and not fields[0].startswith('#'):
            out.append(int(fields[0], 16))
    return out


def is_kanji(cp):
    return 0x4E00 <= cp <= 0x9FFF or 0x3400 <= cp <= 0x4DBF or 0xF900 <= cp <= 0xFAFF


def ipadic_cost_order(directory=IPADIC):
    best = {}
    files = sorted(directory.glob('*.csv'))
    if not files:
        raise SystemExit('gen_preload16: no IPADIC CSV under %s' % directory)
    for path in files:
        with open(path, encoding='euc_jp', newline='') as f:
            for row in csv.reader(f):
                if len(row) <= IPADIC_COST_FIELD:
                    continue
                cost = int(row[IPADIC_COST_FIELD])
                for ch in row[0]:
                    cp = ord(ch)
                    if is_kanji(cp) and (cp not in best or cost < best[cp]):
                        best[cp] = cost
    return sorted(best, key=lambda cp: (best[cp], cp))


def bundle_cps(ttf):
    from fontTools.ttLib import TTFont
    font = TTFont(ttf)
    order = font.getGlyphOrder()
    return {cp for cp, name in font.getBestCmap().items() if full_path(cp) and order.index(name) != 0}


def make_list(present, limit=PRELOAD_MAX, ui=UI_CHARS, ipadic=IPADIC):
    out, seen, counts = [], set(), {}
    for label, source in (('jis1-8', jis_rows_1_8()), ('ui', ui_chars(ui)), ('ipadic', ipadic_cost_order(ipadic))):
        n = 0
        for cp in source:
            if len(out) >= limit:
                break
            if cp in present and cp not in seen:
                seen.add(cp)
                out.append(cp)
                n += 1
        counts[label] = n
    return out, counts


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--ttf', required=True)
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--max', type=int, default=PRELOAD_MAX)
    ap.add_argument('--ui-chars', type=Path, default=UI_CHARS)
    ap.add_argument('--ipadic', type=Path, default=IPADIC, help='directory of the IPADIC CSV')
    args = ap.parse_args(argv)
    try:
        present = bundle_cps(args.ttf)
    except ImportError:
        sys.stderr.write('gen_preload16: fontTools is not importable: pip install fonttools\n')
        return 4
    cps, counts = make_list(present, args.max, args.ui_chars, args.ipadic)
    text = ('# TASK_FONTPERF preload list (tools/g16/gen_preload16.py): %s\n'
            % ' '.join('%s=%d' % kv for kv in counts.items()))
    text += ''.join('%04X  %s\n' % (cp, chr(cp)) for cp in cps)
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    tmp = args.output + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        f.write(text)
    os.replace(tmp, args.output)
    print('gen_preload16: %s %d cps (%s)' % (args.output, len(cps),
                                              ', '.join('%s %d' % kv for kv in counts.items())))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
