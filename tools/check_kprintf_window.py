#!/usr/bin/env python3
"""CPL3 kprintf の C / Rust 引数窓を、非活性分岐も含めて検査する。"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ALLOW = 'tools/check_kprintf_window_allow.txt'
STRING = re.compile(r'(?:u8|u|U|L)?"(?:\\.|[^"\\\n])*"', re.S)
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|' + STRING.pattern + '|'
                   r"'(?:\\.|[^'\\\n])*'|[A-Za-z_]\w*|->|[^\s]", re.S)
IDENT = re.compile(r'[A-Za-z_]\w*\Z')
RUST_RAW = re.compile(r'(?:br|r)(#*)"')
RUST_STRING = re.compile(r'b?"(?:\\.|[^"\\])*"', re.S)
RUST_CHAR = re.compile(r"b?'(?:\\(?:x[0-9a-fA-F]{2}|u\{[0-9a-fA-F_]+\}|[^\r\n])|[^'\\\r\n])'")
RUST_IDENT = re.compile(r"'[A-Za-z_]\w*|[A-Za-z_]\w*")


@dataclass
class Call:
    file: str
    line: int
    function: str
    format: str | None
    supplied: int
    kind: str = 'C'

    @property
    def location(self):
        return f'{self.file}:{self.line} ({self.function})'


def tokens(source):
    # C translation phase 2 precedes comments/tokenization. Keep physical lines.
    positions = []
    chunks = []
    start = 0
    for match in re.finditer(r'\\\r?\n', source):
        chunks.append(source[start:match.start()])
        positions.extend(range(start, match.start()))
        start = match.end()
    chunks.append(source[start:])
    positions.extend(range(start, len(source)))
    logical = ''.join(chunks)
    return [(m.group(), positions[m.start()]) for m in TOKEN.finditer(logical)
            if not m.group().startswith(('//', '/*'))]


def string_value(token):
    """Decode C escapes (Python escapes differ, notably octal/hex and NUL)."""
    escapes = {'a': '\a', 'b': '\b', 'f': '\f', 'n': '\n', 'r': '\r',
               't': '\t', 'v': '\v', '\\': '\\', '"': '"', "'": "'", '?': '?'}
    text = token[token.index('"') + 1:-1]
    out, i = [], 0
    while i < len(text):
        if text[i] != '\\':
            out.append(text[i])
            i += 1
            continue
        i += 1
        if text[i] in '01234567':
            match = re.match(r'[0-7]{1,3}', text[i:])
            out.append(chr(int(match.group(), 8)))
            i += len(match.group())
        elif text[i] == 'x':
            match = re.match(r'[0-9a-fA-F]+', text[i + 1:])
            if not match:
                raise ValueError('invalid C hex escape')
            out.append(chr(int(match.group(), 16)))
            i += 1 + len(match.group())
        else:
            out.append(escapes.get(text[i], text[i]))
            i += 1
    return ''.join(out)


def literal_format(argument):
    values = [t[0] for t in argument]
    # Parentheses preserve the literal value; macros and other expressions do not.
    while values and values[0] == '(' and values[-1] == ')':
        depth = 0
        for i, value in enumerate(values):
            if value == '(':
                depth += 1
            elif value == ')':
                depth -= 1
                if depth == 0:
                    break
        if i != len(values) - 1:
            break
        values = values[1:-1]
    if values and all(STRING.fullmatch(value) for value in values):
        return ''.join(string_value(value) for value in values)
    return None


def count_arguments(fmt):
    """Count conversions and each star in width/precision; %% consumes none."""
    fmt = fmt.split('\0', 1)[0]
    total, i = 0, 0
    while i < len(fmt):
        if fmt[i] != '%':
            i += 1
            continue
        if fmt.startswith('%%', i):
            # %% consumes no argument.
            i += 2
            continue
        match = re.match(r"%[-+ #0']*(\*|[0-9]*)(?:\.(\*|[0-9]*))?"
                         r'(?:hh|ll|[hljztL])?([^%])', fmt[i:])
        if not match:
            raise ValueError('cannot parse format near ' + repr(fmt[i:]))
        total += 1 + (match[1] == '*') + (match[2] == '*')
        i += len(match.group())
    return total


def calls_in(source, file):
    ts = tokens(source)
    pairs, stack = {}, []
    for i, (value, _) in enumerate(ts):
        if value in ('(', '[', '{'):
            stack.append(i)
        elif value in (')', ']', '}'):
            if stack and ts[stack[-1]][0] == {')': '(', ']': '[', '}': '{'}[value]:
                opening = stack.pop()
                pairs[opening] = i
                pairs[i] = opening
    # Function scopes need no compiler/preprocessing, so inactive calls remain.
    scopes = []
    for i, (value, _) in enumerate(ts):
        if value == '{' and i and ts[i - 1][0] == ')':
            opening = pairs.get(i - 1, 0)
            name = ts[opening - 1][0] if opening else ''
            if IDENT.fullmatch(name) and name not in ('if', 'for', 'while', 'switch'):
                scopes.append((i, pairs.get(i, len(ts)), name))
    calls = []
    for i in range(len(ts) - 2):
        if ts[i][0] not in ('->', '.') or [t[0] for t in ts[i + 1:i + 3]] != ['kprintf', '(']:
            continue
        end = pairs.get(i + 2)
        if end is None:
            raise ValueError(f'{file}: unclosed kprintf call')
        args, start, cursor = [], i + 3, i + 3
        while cursor < end:
            value = ts[cursor][0]
            if value in ('(', '[', '{'):
                if cursor not in pairs:
                    raise ValueError(f'{file}: unclosed argument')
                cursor = pairs[cursor] + 1
            elif value == ',':
                args.append(ts[start:cursor])
                cursor += 1
                start = cursor
            else:
                cursor += 1
        args.append(ts[start:end])
        if len(args) < 2 or any(not arg for arg in args):
            raise ValueError(f'{file}: kprintf requires attr and fmt')
        fmt = literal_format(args[1])
        function = next((name for left, right, name in scopes if left < i < right), '<file>')
        calls.append(Call(file, source.count('\n', 0, ts[i][1]) + 1,
                          function, fmt, len(args) - 2))
    return calls


def rust_tokens(source):
    """Preserve literal tokens/positions; comments nest and lifetimes aren't chars."""
    result, i = [], 0
    while i < len(source):
        if source[i].isspace():
            i += 1
            continue
        if source.startswith('//', i):
            end = source.find('\n', i)
            i = len(source) if end < 0 else end
            continue
        if source.startswith('/*', i):
            depth, i = 1, i + 2
            while depth and i < len(source):
                if source.startswith('/*', i):
                    depth, i = depth + 1, i + 2
                elif source.startswith('*/', i):
                    depth, i = depth - 1, i + 2
                else:
                    i += 1
            if depth:
                raise ValueError('unclosed Rust block comment')
            continue
        raw = RUST_RAW.match(source, i)
        if raw:
            closing = '"' + raw[1]
            end = source.find(closing, raw.end())
            if end < 0:
                raise ValueError('unclosed Rust raw string')
            end += len(closing)
        else:
            match = (RUST_STRING.match(source, i) or RUST_CHAR.match(source, i)
                     or RUST_IDENT.match(source, i))
            if not match and (source[i] == '"' or source.startswith('b"', i)):
                raise ValueError('unclosed Rust string')
            end = match.end() if match else i + 1
        result.append((source[i:end], i))
        i = end
    return result


def delimiter_pairs(ts):
    pairs, stack = {}, []
    for i, (value, _) in enumerate(ts):
        if value in ('(', '[', '{'):
            stack.append(i)
        elif value in (')', ']', '}'):
            if not stack or ts[stack[-1]][0] != {')': '(', ']': '[', '}': '{'}[value]:
                raise ValueError('unmatched Rust delimiter')
            opening = stack.pop()
            pairs[opening] = i
            pairs[i] = opening
    if stack:
        raise ValueError('unclosed Rust delimiter')
    return pairs


def rust_literal_format(argument):
    values = [t[0] for t in argument]

    def unwrap(values):
        while values and values[0] == '(' and values[-1] == ')':
            depth = 0
            for i, value in enumerate(values):
                depth += (value == '(') - (value == ')')
                if depth == 0:
                    break
            if i != len(values) - 1:
                break
            values = values[1:-1]
        return values

    values = unwrap(values)
    if values[-4:] == ['.', 'as_ptr', '(', ')']:
        values = unwrap(values[:-4])
    if len(values) != 1:
        return None
    token = values[0]
    raw = RUST_RAW.match(token)
    if raw and token.startswith('br'):
        return token[raw.end():-1 - len(raw[1])]
    if not token.startswith('b"') or not RUST_STRING.fullmatch(token):
        return None
    # Rust byte strings have two-digit hex escapes and whitespace continuation.
    escapes = {'0': '\0', 'n': '\n', 'r': '\r', 't': '\t',
               '\\': '\\', '"': '"', "'": "'"}
    text, out, i = token[2:-1], [], 0
    while i < len(text):
        if text[i] != '\\':
            out.append(text[i])
            i += 1
            continue
        i += 1
        if text[i] == 'x':
            digits = text[i + 1:i + 3]
            if not re.fullmatch(r'[0-9a-fA-F]{2}', digits):
                raise ValueError('invalid Rust hex escape')
            out.append(chr(int(digits, 16)))
            i += 3
        elif text[i] in '\r\n':
            while i < len(text) and text[i].isspace():
                i += 1
        elif text[i] in escapes:
            out.append(escapes[text[i]])
            i += 1
        else:
            raise ValueError('invalid Rust byte string escape')
    return ''.join(out)


def rust_calls_in(source, file):
    ts = rust_tokens(source)
    pairs = delimiter_pairs(ts)
    # Skip whole macro_rules token trees, including literal example calls.
    definitions = set()
    for i in range(len(ts) - 3):
        if [t[0] for t in ts[i:i + 2]] == ['macro_rules', '!']:
            opening = i + 3
            if ts[opening][0] in ('(', '[', '{'):
                definitions.update(range(i, pairs[opening] + 1))
    scopes = []
    for i in range(len(ts) - 2):
        if i in definitions or ts[i][0] != 'fn' or not IDENT.fullmatch(ts[i + 1][0]):
            continue
        cursor = i + 2
        while cursor < len(ts) and ts[cursor][0] not in ('{', ';'):
            if ts[cursor][0] in ('(', '['):
                cursor = pairs[cursor] + 1
            else:
                cursor += 1
        if cursor < len(ts) and ts[cursor][0] == '{':
            scopes.append((cursor, pairs[cursor], ts[i + 1][0]))
    calls = []
    for i in range(len(ts) - 2):
        if i in definitions:
            continue
        value = ts[i][0]
        if value in ('kprint', 'kprint_attr') and ts[i + 1][0] == '!':
            opening, fixed, kind = i + 2, 1 if value == 'kprint' else 2, 'Rust macro'
            if ts[opening][0] not in ('(', '[', '{'):
                raise ValueError(f'{file}: missing Rust macro arguments')
        elif (value == 'kprintf' and i and ts[i - 1][0] == '.'
              and ts[i + 1][0] == ')' and ts[i + 2][0] == '('):
            opening, fixed, kind = i + 2, 2, 'Rust direct'
        else:
            continue
        end = pairs[opening]
        args, start, cursor = [], opening + 1, opening + 1
        while cursor < end:
            if ts[cursor][0] in ('(', '[', '{'):
                cursor = pairs[cursor] + 1
            elif ts[cursor][0] == ',':
                args.append(ts[start:cursor])
                cursor += 1
                start = cursor
            else:
                cursor += 1
        if start < end:  # Rust accepts a trailing comma.
            args.append(ts[start:end])
        if len(args) < fixed or any(not arg for arg in args):
            raise ValueError(f'{file}: Rust {value} requires format/attribute arguments')
        function = next((name for left, right, name in reversed(scopes) if left < i < right), '<file>')
        calls.append(Call(file, source.count('\n', 0, ts[i][1]) + 1, function,
                          rust_literal_format(args[fixed - 1]), len(args) - fixed, kind))
    return calls


def window_limit(root):
    source = (root / 'exec/exec.c').read_text()
    match = re.search(r'^#define\s+RING3_ARG_WINDOW\s+(0x[0-9a-fA-F]+|[0-9]+)[uUlL]*\s*(?://[^\n]*|/\*[^\n]*\*/)?$',
                      source, re.M)
    if not match:
        raise ValueError('RING3_ARG_WINDOW definition is not an integer literal')
    window = int(match[1], 16 if match[1].lower().startswith('0x') else 10)
    if window < 8 or window % 4:
        raise ValueError('invalid RING3_ARG_WINDOW size')
    # attr and fmt each occupy one i386 32-bit slot, as in test_mem_stat.py.
    return window, (window - 8) // 4


def read_allow(root):
    entries = {}
    for no, line in enumerate((root / ALLOW).read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        match = re.fullmatch(r'([^\s:]+\.(?:c|inc|h|rs)):([A-Za-z_]\w*)\s+(.+)', line)
        if not match or match[1] + ':' + match[2] in entries:
            raise ValueError(f'{ALLOW}:{no}: invalid/duplicate allowance')
        entries[match[1] + ':' + match[2]] = match[3]
    return entries


def c_files(root, directory):
    # Source trees only. Rust intermediates can contain generated C files.
    return sorted(p for p in (root / directory).rglob('*')
                  if p.suffix in ('.c', '.inc', '.h') and not {'.git', 'target'} & set(p.relative_to(root / directory).parts))


def rust_files(root):
    return sorted(p for directory in ('sdk/rust', 'userland')
                  for p in (root / directory).rglob('*.rs')
                  if not {'.git', 'target'} & set(p.relative_to(root / directory).parts))


def audit(root):
    window, limit = window_limit(root)
    allow = read_allow(root)
    files = c_files(root, 'userland') + c_files(root, 'sdk')
    calls, errors, used = [], [], set()
    for path in files:
        calls.extend(calls_in(path.read_text(), path.relative_to(root).as_posix()))
    rs_files = rust_files(root)
    rust_calls = []
    for path in rs_files:
        rust_calls.extend(rust_calls_in(path.read_text(), path.relative_to(root).as_posix()))
    calls.extend(rust_calls)
    maximum, places, rust_counts = 0, [], []
    for call in calls:
        key = call.file + ':' + call.function
        if call.format is None:
            count = call.supplied
            if key not in allow:
                errors.append(f'{call.location}: nonliteral format outside allowlist')
            else:
                used.add(key)
        else:
            try:
                count = count_arguments(call.format)
            except ValueError as error:
                errors.append(f'{call.location}: {error}')
                continue
        if call.kind.startswith('Rust'):
            rust_counts.append((call, max(count, call.supplied)))
        if count > maximum:
            maximum, places = count, [call.location]
        elif count == maximum:
            places.append(call.location)
        if count > limit or call.supplied > limit:
            errors.append(f'{call.location}: window exceeded: format={count}, '
                          f'supplied={call.supplied}, limit={limit}')
    for key in sorted(allow.keys() - used):
        errors.append(f'{key}: stale allowance (no nonliteral call)')
    print(f'RING3_ARG_WINDOW={window}B; variable argument limit={limit}')
    print(f'scanned files={len(files)} (userland={len(c_files(root, "userland"))}, '
          f'sdk={len(c_files(root, "sdk"))}); calls={len(calls) - len(rust_calls)} (C)')
    print(f'Rust scanned files={len(rs_files)}; '
          f'macro calls={sum(c.kind == "Rust macro" for c in rust_calls)}; '
          f'direct calls={sum(c.kind == "Rust direct" for c in rust_calls)}')
    rust_maximum = max((count for _, count in rust_counts), default=0)
    print(f'Rust maximum variable arguments={rust_maximum}')
    for call, count in rust_counts:
        if count == rust_maximum:
            print('  Rust maximum: ' + call.location)
    print(f'nonliteral calls={sum(c.format is None for c in calls)}; '
          f'allowance entries={len(allow)}')
    print(f'maximum variable arguments={maximum}')
    print(f'maximum supplied variable arguments={max((c.supplied for c in calls), default=0)}')
    for place in places:
        print('  maximum: ' + place)
    for key, reason in allow.items():
        print(f'  allow: {key} {reason}')
    for directory in ('apps', 'game'):
        print(f'excluded {directory}: C files={len(c_files(root, directory))} '
              '(submodule; not scanned for calls)')
    for error in errors:
        print('FAIL: ' + error)
    print('FAIL kprintf window' if errors else 'PASS kprintf window')
    return bool(errors)


def mutate(root):
    # Each mutant runs the actual CLI on an isolated source tree, after control.
    with tempfile.TemporaryDirectory(prefix='kprintf-window-') as directory:
        tree = Path(directory)
        for subdir in ('exec', 'tools', 'userland/tests', 'sdk'):
            (tree / subdir).mkdir(parents=True, exist_ok=True)
        (tree / 'exec/exec.c').write_text((root / 'exec/exec.c').read_text())
        (tree / ALLOW).write_text('')
        _, limit = window_limit(root)
        source = tree / 'userland/tests/control.c'
        source.write_text('void control(void) { odd_receiver->kprintf(0, "'
                          + '%d ' * limit + '%%", ' + ', '.join(['1'] * limit) + '); }\n')

        def run(script=Path(__file__)):
            return subprocess.run([sys.executable, '-B', str(script), '--root', str(tree)],
                                  capture_output=True, text=True, timeout=30)

        control = run()
        if control.returncode:
            raise ValueError('mutation control failed: ' + control.stdout + control.stderr)
        print('PASS mutation control (boundary plus %%)')
        rust = tree / 'userland/tests/control.rs'
        rust.write_text('fn control() { kprint!(b"' + '%d ' * limit + '%%\\0", '
                        + ', '.join(['1'] * limit) + '); }\n')
        control = run()
        if control.returncode:
            raise ValueError('Rust mutation control failed: ' + control.stdout + control.stderr)
        print('PASS Rust mutation control (boundary plus %%)')
        rust.write_text('fn overflow() { kprint!(b"' + '%d ' * (limit + 1) + '\\0", '
                        + ', '.join(['1'] * (limit + 1)) + '); }\n')
        result = run()
        if result.returncode != 1 or 'window exceeded' not in result.stdout:
            raise ValueError('Rust mutant survived: ' + result.stdout + result.stderr)
        print('RED: .rs kprint! argument overflow (%d variable arguments)' % (limit + 1))
        print(next(line for line in result.stdout.splitlines() if 'window exceeded' in line))
        rust.unlink()
        included = tree / 'userland/tests/injected.inc'
        included.write_text('void included_overflow(void) { ops->kprintf(0, "'
                            + '%d ' * (limit + 1) + '", ' + ', '.join(['1'] * (limit + 1)) + '); }')
        result = run()
        if result.returncode != 1 or 'window exceeded' not in result.stdout:
            raise ValueError('inc mutant survived: ' + result.stdout + result.stderr)
        print('RED: .inc argument overflow (%d variable arguments)' % (limit + 1))
        print(next(line for line in result.stdout.splitlines() if 'window exceeded' in line))
        included.unlink()
        injected = tree / 'userland/tests/injected.c'
        mutants = (
            ('argument overflow', 'void overflow(void) { other->kprintf(0, "'
             + '%d ' * (limit + 1) + '", ' + ', '.join(['1'] * (limit + 1)) + '); }',
             'window exceeded'),
            ('nonliteral outside allowlist',
             'void dynamic(void) { other->kprintf(0, fmt, 1); }', 'nonliteral format'),
        )
        for name, content, diagnostic in mutants:
            injected.write_text(content)
            result = run()
            if result.returncode != 1 or diagnostic not in result.stdout:
                raise ValueError('mutant survived: ' + name + '\n' + result.stdout + result.stderr)
            print('RED: ' + name)
            print(next(line for line in result.stdout.splitlines() if diagnostic in line))
        injected.unlink()
        checker = tree / 'tools/check_kprintf_window.py'
        original = Path(__file__).read_text()
        needle = '# %% consumes no argument.\n            i += 2'
        if original.count(needle) != 1:
            raise ValueError('percent mutation anchor changed')
        checker.write_text(original.replace(needle, needle.replace('i += 2', 'total += 1\n            i += 2')))
        result = run(checker)
        if result.returncode != 1 or 'window exceeded' not in result.stdout:
            raise ValueError('percent mutant survived: ' + result.stdout + result.stderr)
        print('RED: %% counted as conversion (false positive on passing control)')
        print(next(line for line in result.stdout.splitlines() if 'window exceeded' in line))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    try:
        if audit(args.root):
            return 1
        if args.mutate:
            return mutate(args.root)
    except (OSError, ValueError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
