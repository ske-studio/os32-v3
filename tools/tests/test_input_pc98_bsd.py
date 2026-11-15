#!/usr/bin/env python3
"""M0-2 fixture contract only; the M2 product evaluator does not exist yet."""
CHECK = dict(name='input-pc98-bsd-host', tier=1, budget=5, needs=[], kind='contract',
    basis=['INPUT_BIND_DESIGN §1', 'INPUT_BIND_DESIGN §2a', 'INPUT_BIND_DESIGN §6 M0'],
    covers=['tools/tests/test_input_pc98_bsd.py', 'tools/tests/input_pc98_bsd_fixture.kbd',
            'tools/tests/input_pc98_compat_fixture.h', 'tools/tests/input_pc98_compat_host.c',
            'input/hal/kbd_merge.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'docs/decisions/INPUT_BIND_DESIGN.md'])
from pathlib import Path
from types import SimpleNamespace
import html
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
# FreeBSD usr.sbin/kbdcontrol/lex.l:39-111, kbio.h:139-199.
# These are reference symbols, not OS32 constants or product-derived outputs.
PC98_BSD_ASCII = dict(zip(
    'nul soh stx etx eot enq ack bel bs ht nl vt ff cr so si dle dc1 dc2 dc3 '
    'dc4 nak syn etb can em sub esc fs gs rs us'.split(), range(32)), del_=127)
PC98_BSD_ASCII['del'] = PC98_BSD_ASCII.pop('del_')
PC98_BSD_SPECIAL = dict(nop=0x00, lshift=0x02, clock=0x04, slock=0x06,
    lalt=0x07, btab=0x08, lctrl=0x09, nscr=0x0a, meta=0x84,
    boot=0x85, debug=0x86, susp=0x87, saver=0x88)
# kbio.h: FLAG_LOCK_C=1, FLAG_LOCK_N=2; kbdmap.5:256-271.
PC98_BSD_FLAGS = dict(O=0, C=1, N=2, B=3)
# Fixed expected columns, lock order: neither, Caps, Num, Caps+Num.
# B is absent in jp.pc98.kbd: exercise its specified rule separately too.
PC98_BSD_COLUMNS = ((0, 1, 2, 3, 4, 5, 6, 7), (1, 0, 3, 2, 5, 4, 7, 6))
PC98_BSD_LOCK_EXPECTED = dict(O=(0, 0, 0, 0), C=(0, 1, 0, 1),
                              N=(0, 0, 1, 1), B=(0, 1, 1, 1))


def pc98_bsd_tokens(text):
    return re.findall(r"'[^']'|'''|\S+", text)


def pc98_bsd_decode(token):
    if len(token) == 3 and token[0] == token[2] == "'":
        return ord(token[1]), False
    if token in PC98_BSD_ASCII:
        return PC98_BSD_ASCII[token], False
    if token.startswith('fkey'):
        number = int(token[4:])
        assert 1 <= number <= 96, ('fkey range', token)
        return 0x1b + number - 1, True
    if token.startswith('scr'):
        number = int(token[3:])
        assert 1 <= number <= 16, ('screen range', token)
        return 0x0b + number - 1, True
    assert token in PC98_BSD_SPECIAL, ('unknown symbol', token)
    return PC98_BSD_SPECIAL[token], True


def pc98_bsd_column(column, flags, caps, num):
    # Reference-only oracle for M0, not an implementation of the future M2 path.
    if (flags & 1 and caps) or (flags & 2 and num):
        column ^= 1
    return column


def pc98_bsd_verify(root, fixture):
    rows = []
    for line in fixture.splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        fields = pc98_bsd_tokens(line)
        assert len(fields) == 11, ('row width', line)
        sc, tokens, spcl, flgs = int(fields[0]), fields[1:9], int(fields[9], 16), fields[10]
        assert sc == len(rows) and sc < 128, ('row index', sc)
        assert 0 <= spcl <= 255 and flgs in PC98_BSD_FLAGS, ('row metadata', sc)
        cells = [pc98_bsd_decode(token) for token in tokens]
        for col, (value, special) in enumerate(cells):
            assert bool(spcl & (0x80 >> col)) == special, ('spcl', sc, col)
            # kbio.h excludes 01, 7b-7f and a4 onward for special values.
            if special:
                assert value == 0 or 2 <= value <= 0x7a or 0x80 <= value <= 0xa3, ('special range', sc, col)
            else:
                assert 0 <= value <= 127, ('ASCII range', sc, col)
        rows.append((tokens, spcl, flgs))
    assert len(rows) == 128, '128 rows'

    # Full O/C/N/B truth table × all eight input columns, including both locks.
    for flag, expected in PC98_BSD_LOCK_EXPECTED.items():
        for locks, flipped in enumerate(expected):
            actual = tuple(pc98_bsd_column(c, PC98_BSD_FLAGS[flag], locks & 1, locks & 2)
                           for c in range(8))
            assert actual == PC98_BSD_COLUMNS[flipped], ('lock-column', flag, locks, actual)
    for sc, (tokens, spcl, flag) in enumerate(rows):
        for locks, flipped in enumerate(PC98_BSD_LOCK_EXPECTED[flag]):
            for col, expected_col in enumerate(PC98_BSD_COLUMNS[flipped]):
                selected = pc98_bsd_column(col, PC98_BSD_FLAGS[flag], locks & 1, locks & 2)
                expected_value, expected_special = pc98_bsd_decode(tokens[expected_col])
                assert pc98_bsd_decode(tokens[selected])[0] == expected_value, ('lock-value', sc, locks, col)
                assert bool(spcl & (0x80 >> selected)) == expected_special, ('lock-spcl', sc, locks, col)

    old_text = (root/'tools/tests/input_pc98_compat_fixture.h').read_text()
    old_rows = re.findall(r'\{([^{}]+)\}, /\* ([0-9a-f]{2}) \*/', old_text)
    assert len(old_rows) == 128, 'old fixture rows'
    old = []
    for values, sc_hex in old_rows:
        assert int(sc_hex, 16) == len(old), 'old fixture order'
        values = [int(v.strip(), 16) for v in values.split(',')]
        assert len(values) == 4, 'old fixture columns'
        old.append(values * 2)  # M0-1: GRPH has no cooked effect.

    doc = (root/'docs/decisions/INPUT_BIND_DESIGN.md').read_text()
    section = doc.split('## 2a.', 1)[1].split('\n## 3.', 1)[0]
    ledger = set()
    for line in section.splitlines():
        match = re.match(r'\|(\d{3}) / ([0-9a-f]{2})\|', line)
        if not match:
            continue
        sc = int(match[1])
        assert sc == int(match[2], 16) and sc not in ledger, ('ledger index', sc)
        ledger.add(sc)
        fields = line.split('|')
        bsd = pc98_bsd_tokens(html.unescape(fields[3]))
        tokens, _, flag = rows[sc]
        assert len(bsd) == 9 and bsd[:8] == tokens and bsd[8] == '[' + flag + ']', ('ledger BSD', sc)
        if sc == 114:
            assert fields[4] == '文字なし、Kana派生 (表のbyteは全00)', 'ledger Kana'
            assert old[sc] == [0] * 8, 'old Kana bytes'
        else:
            assert [int(v, 16) for v in fields[4].split()] == old[sc], ('ledger old', sc)
    assert len(ledger) == 80, ('ledger row count', len(ledger))

    # Derive actual differing *rows* independently of the ledger's row list.
    # §2a: four modifier/lock rows have equivalent meanings; NOP equals old
    # byte 00 only at the legacy exit, not internally (NUL remains a character).
    equivalent_modifiers = {112: 'lshift', 113: 'clock', 115: 'lalt', 116: 'lctrl'}
    differences = set()
    for sc, (tokens, spcl, flag) in enumerate(rows):
        if sc in equivalent_modifiers:
            assert tokens == [equivalent_modifiers[sc]] * 8 and spcl == 255 and flag == 'O', ('modifier', sc)
            assert old[sc] == [0] * 8, ('old modifier', sc)
            continue
        if any((special and value != 0) or value != old[sc][col]
               for col, (value, special) in enumerate(map(pc98_bsd_decode, tokens))):
            differences.add(sc)
        if sc not in ledger:
            assert flag == 'O', ('unchanged flags', sc)
    assert differences == ledger, ('ledger differences', sorted(differences ^ ledger))



def pc98_bsd_check_legacy_columns(root, tmp, mutate):
    import host32
    from kernel_includes import include_flags
    # Compile the pinned legacy implementation and the current path against the
    # same literal oracle. Mutants change one expected column, never product code.
    source = (root/'input/hal/kbd_merge.c').read_text()
    host = (root/'tools/tests/input_pc98_compat_host.c').read_text()
    old = subprocess.check_output(['git', 'show', '1b0fa106:input/hal/kbd_merge.c'], cwd=root, text=True)
    old = old[old.index('static const u8 scancode_to_ascii['):old.index('void input_emit_legacy_repeat')]
    old = '#include "in2_int.h"\n#include "drivers/input/kbd.h"\n' + old.replace('input_emit_legacy_key(', 'pc98_old_emit(')
    (tmp/'old.c').write_text(old)
    (tmp/'current.c').write_text(source)
    def execute(code):
        (tmp/'host.c').write_text(code)
        cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
               '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
               '-nostdinc', '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
               '-I'+str(root/'tools/tests'), '-I'+str(root/'tools/tests/host_arch'),
               '-I'+str(root/'input/hal'), *include_flags(root)]
        subprocess.run(cmd+[str(tmp/'current.c'), str(tmp/'old.c'), str(tmp/'host.c'),
                            '-o', str(tmp/'legacy-test')], check=True)
        return host32.run([str(tmp/'legacy-test')], runner='qemu', timeout=10).returncode
    assert execute(host) == 0, 'legacy columns baseline'
    if mutate:
        mutants = [
            ('GRPH column', '{0, 1, 2, 3, 0, 1, 2, 3}, /* no lock */',
             '{0, 1, 2, 3, 0, 0, 2, 3}, /* no lock */'),
            ('Kana lock', '{1, 0, 3, 2, 1, 0, 3, 2}, /* Caps+Kana */',
             '{1, 0, 3, 2, 1, 1, 3, 2}, /* Caps+Kana */')]
        # The first line occurs in both matrices; target the plain matrix only.
        for name, anchor, replacement in mutants:
            target = host
            if name == 'GRPH column':
                anchor = 'static const u8 plain_columns[4][8] = {\n        ' + anchor
                replacement = 'static const u8 plain_columns[4][8] = {\n        ' + replacement
            assert target.count(anchor) == 1, ('unique anchor', name)
            assert execute(target.replace(anchor, replacement, 1)) == 1, ('legacy RED', name)
            print('RED runtime:', name, flush=True)
        assert execute(host) == 0, 'legacy columns final baseline'


def run(ctx):
    source = (ctx.root/'tools/tests/test_input_pc98_bsd.py').read_text()
    fixture = (ctx.root/'tools/tests/input_pc98_bsd_fixture.kbd').read_text()
    with tempfile.TemporaryDirectory(prefix='pc98-bsd-') as directory:
        tmp = Path(directory)
        def execute(code, data):
            (tmp/'verify.py').write_text(code)
            (tmp/'fixture.kbd').write_text(data)
            return subprocess.run([sys.executable, '-B', str(tmp/'verify.py'), '--verify',
                                   str(ctx.root), str(tmp/'fixture.kbd')], capture_output=True, text=True)
        result = execute(source, fixture)
        assert result.returncode == 0, result.stderr
        pc98_bsd_check_legacy_columns(ctx.root, tmp, ctx.mutate)
        if ctx.mutate:
            mutants = [
                ('cell', 'fixture', "001 '1'", "001 '2'", 'ledger BSD'),
                ('spcl', 'fixture', 'debug esc 02 O', 'debug esc 01 O', 'spcl'),
                # Two independent flips cancel for B+Caps+Num: must be RED.
                ('flgs-column', 'source', 'column ' + '^= 1',
                 'column ^= int(bool(flags & 1 and caps)) ^ int(bool(flags & 2 and num))', 'lock-column')]
            for name, target, anchor, replacement, diagnostic in mutants:
                original = fixture if target == 'fixture' else source
                assert original.count(anchor) == 1, ('unique anchor', name)
                changed = original.replace(anchor, replacement, 1)
                result = execute(changed if target == 'source' else source,
                                 changed if target == 'fixture' else fixture)
                assert result.returncode == 1 and diagnostic in result.stderr, (name, result.stderr)
                print('RED runtime:', name, flush=True)
            result = execute(source, fixture)
            assert result.returncode == 0, result.stderr
    print('PASS BSD 128x8, spcl/ranges, O/C/N/B locks, 80-row ledger and 48 equivalent rows', flush=True)
    return 0


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == '--verify':
        pc98_bsd_verify(Path(sys.argv[2]), Path(sys.argv[3]).read_text())
    else:
        sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
