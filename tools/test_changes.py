#!/usr/bin/env python3
"""Advisory review of test edits against a request pack; --strict gates findings."""
import argparse
import difflib
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TEST_PREFIXES = ('tools/tests/', 'userland/tests/', 'tools/check_map.d/', 'build/checks.d/')
MUTATE_SKIP = re.compile(r'\b(?:if|elif|unless|ifeq|ifneq)\b.*?(?:--mutate|\bmutate\b|\bMUTATE\b)', re.I)
RANDOM = re.compile(r'\bos\.urandom\b|\brandom\b|\brand\s*\(|\bsrand\s*\(\s*time\s*\(')
MAIN = re.compile(r'\bint\s+main\s*\(')
EXPECT = re.compile(r'\b(?:assert|expect|require|check)\w*\b|==|!=', re.I)
NUMBER = re.compile(r'(?<![\w])[+-]?(?:0x[0-9a-fA-F]+|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)(?=[uUlL]*(?![\w]))')


def mentioned(path, pack):
    return re.search(r'(?<![\w.-])' + re.escape(path) + r'(?![\w./-])', pack) is not None


def numeric_expectation(old, new):
    return (bool(EXPECT.search(new)) and old != new and bool(NUMBER.search(new))
            and NUMBER.sub('<N>', old) == NUMBER.sub('<N>', new))


def inspect_changes(changed, pack, diffs, before):
    warnings = []
    for path in sorted(set(changed)):
        if not path.startswith(TEST_PREFIXES):
            continue
        if not mentioned(path, pack):
            warnings.append('列挙外: ' + path)
        delta = diffs.get(path, '')
        added_lines = [line[1:] for line in delta.splitlines()
                       if line.startswith('+') and not line.startswith('+++')]
        removed = [line[1:] for line in delta.splitlines()
                   if line.startswith('-') and not line.startswith('---')]
        added = '\n'.join(added_lines)
        uncommented = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group().count('\n'),
                             added, flags=re.S).splitlines()
        for no, line in enumerate(added_lines, 1):
            reasons = []
            code = re.split(r'#|//|/\*', uncommented[no - 1] if no <= len(uncommented) else '', maxsplit=1)[0]
            mutation_only = re.fullmatch(
                r'\s*if\s+[\"\']--mutate[\"\']\s+in\s+sys\.argv(?:\[1:\])?\s*:\s*rc\s*\+=\s*mutate\(\)\s*', code)
            if MUTATE_SKIP.search(code) and not mutation_only:
                reasons.append('mutate 分岐で正常段を省く可能性')
            if RANDOM.search(code):
                reasons.append('乱数 (固定種の有無を確認)')
            for old in removed:
                if numeric_expectation(old, line):
                    reasons.append('期待数値のみ変更'); break
            if reasons:
                warnings.append('要確認: %s (追加行%d): %s: %s' % (
                    path, no, ' / '.join(reasons), line.strip()[:160]))
        if path.startswith('userland/tests/') and path.endswith('.c') and before.get(path) is None:
            if not MAIN.search(added):
                warnings.append('要確認: %s: 新しいゲスト試験に int main がない' % path)
    return warnings


def git(root, *args, missing_ok=False):
    p = subprocess.run(['git', '-C', str(root), *args], capture_output=True, text=True)
    missing_path = missing_ok and re.search(r'(?:does not exist in|exists on disk, but not in)', p.stderr)
    if p.returncode and not missing_path:
        raise ValueError(p.stderr.strip())
    return None if p.returncode else p.stdout


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', required=True)
    ap.add_argument('--pack', required=True)
    ap.add_argument('--root', type=Path, default=ROOT)
    ap.add_argument('--strict', action='store_true')
    args = ap.parse_args(argv)
    try:
        git(args.root, 'rev-parse', '--verify', args.base + '^{commit}')
        pack = Path(args.pack).read_text(encoding='utf-8')
        # Untracked tests are relevant before landing, too. Disable rename folding.
        changed = git(args.root, 'diff', '--name-only', '--no-renames', args.base).splitlines()
        changed += git(args.root, 'ls-files', '--others', '--exclude-standard').splitlines()
        diffs, before = {}, {}
        for path in sorted(set(changed)):
            if not path.startswith(TEST_PREFIXES):
                continue
            old = git(args.root, 'show', args.base + ':' + path, missing_ok=True)
            before[path] = old
            source = args.root / path
            new = source.read_text(encoding='utf-8', errors='replace') if source.is_file() else ''
            diffs[path] = '\n'.join(difflib.unified_diff(
                (old or '').splitlines(), new.splitlines(), n=0))
        warnings = inspect_changes(changed, pack, diffs, before)
    except (OSError, ValueError) as exc:
        print('検査エラー: ' + str(exc), file=sys.stderr)
        return 2
    for warning in warnings:
        print(warning)
    print('試験変更の補助情報: %d 件 (合否判定なし)' % len(warnings))
    return int(args.strict and bool(warnings))


if __name__ == '__main__':
    sys.exit(main())
