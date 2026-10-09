#!/usr/bin/env python3
"""CPL3 kprintf の引数窓を、非活性 #if 分岐も含めて検査する。"""
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


@dataclass
class Call:
    file: str
    line: int
    function: str
    format: str | None
    supplied: int

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
        match = re.fullmatch(r'([^\s:]+\.(?:c|inc|h)):([A-Za-z_]\w*)\s+(.+)', line)
        if not match or match[1] + ':' + match[2] in entries:
            raise ValueError(f'{ALLOW}:{no}: invalid/duplicate allowance')
        entries[match[1] + ':' + match[2]] = match[3]
    return entries


def c_files(root, directory):
    # Source trees only. Rust intermediates can contain generated C files.
    return sorted(p for p in (root / directory).rglob('*')
                  if p.suffix in ('.c', '.inc', '.h') and not {'.git', 'target'} & set(p.relative_to(root / directory).parts))


def audit(root):
    window, limit = window_limit(root)
    allow = read_allow(root)
    files = c_files(root, 'userland') + c_files(root, 'sdk')
    calls, errors, used = [], [], set()
    for path in files:
        calls.extend(calls_in(path.read_text(), path.relative_to(root).as_posix()))
    maximum, places = 0, []
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
          f'sdk={len(c_files(root, "sdk"))}); calls={len(calls)}')
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
