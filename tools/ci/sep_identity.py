#!/usr/bin/env python3
"""機種依存の分離の枝で「生成物が基点と同じ」を確かめる (docs/design/tools/ci/sep_identity.md)。

移すだけの段は x86 の生成物も PowerPC の像の .o も基点と同じはずなので、違えば落とす。
変わる段は、基点から HEAD までのコミットのどれかに `Sep-Changes: x86` / `Sep-Changes: ppc`
(両方なら `Sep-Changes: x86 ppc`) の行を書いて宣言する。
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TARGETS = ('x86', 'ppc')
# PowerPC の像 (build/ppc-kboot.mk の旗) — CI の 4 像と、常駐 CLI・NEGATIVE の像。
PPC_IMAGES = (['KBOOT_LINK=0'], [], ['KBOOT_SH=1', 'KBOOT_LINK=0'], ['KBOOT_SH=1'],
              ['KBOOT_SHELL=1', 'KBOOT_LINK=0'], ['NEGATIVE=1', 'KBOOT_LINK=0'])
# RAM root の ISO は同じ入力でも作るたびに変わる (時刻)。カーネルのコードではない。
VOLATILE = ('root_image_blob.o',)


def git(root, *args):
    return subprocess.check_output(['git', *args], cwd=root, text=True)


def declared(messages):
    """コミットの本文の `Sep-Changes:` の行が宣言した対象の集合。未知の語は誤り。"""
    found = set()
    for line in messages.splitlines():
        match = re.fullmatch(r'Sep-Changes:\s*(.*)', line.strip())
        if not match:
            continue
        for word in match[1].split():
            if word not in TARGETS:
                raise ValueError('unknown Sep-Changes target: ' + word)
            found.add(word)
    return found


def renames(status):
    """`git diff --name-status -M` の出力から {新しいパス: 古いパス} (ソースの移動)。"""
    moved = {}
    for line in status.splitlines():
        parts = line.split('\t')
        if len(parts) == 3 and parts[0].startswith('R'):
            moved[parts[2]] = parts[1]
    return moved


def object_key(path):
    """build/out/ppc-kboot/<像>/<ソースのパス>.o -> (像, ソースのパスから拡張子を除いた物)。"""
    parts = Path(path).parts
    index = parts.index('ppc-kboot')
    rest = parts[index + 1:]
    image = ''
    if rest[0] in ('link', 'link-sh', 'sh', 'shell') or rest[0].startswith('neg'):
        image, rest = rest[0], rest[1:]
    return image, '/'.join(rest)[:-2]


def compare_objects(base, head, moved):
    """(違う, 基点にだけ在る, HEAD にだけ在る)。moved は {新しいソース: 古いソース}。"""
    stems = {new.rsplit('.', 1)[0]: old.rsplit('.', 1)[0] for new, old in moved.items()}
    mapped = {}
    for (image, stem), digest in head.items():
        mapped[(image, stems.get(stem, stem))] = ((image, stem), digest)
    different = sorted('/'.join(k for k in mapped[key][0] if k) for key in mapped
                       if key in base and base[key] != mapped[key][1])
    only_base = sorted('/'.join(k for k in key if k) for key in base if key not in mapped)
    only_head = sorted('/'.join(k for k in mapped[key][0] if k) for key in mapped if key not in base)
    return different, only_base, only_head


def moved_object_pairs(diff_paths, moved):
    """build_compare が違うと言ったパスのうち、移したソースの .o を (基点の .o, HEAD の .o) の対にする。

    戻り値は (対の一覧, 対にならなかったパス)。build_compare は .o をパスで比べるので、置き場を
    移した .o は基点と HEAD の両方で「片方にしか無い」と出る。中身を比べるのは呼び手。
    """
    stems = {new.rsplit('.', 1)[0]: old.rsplit('.', 1)[0] for new, old in moved.items()}
    paths = set(diff_paths)
    pairs, used = [], set()
    for path in sorted(paths):
        if not path.endswith('.o'):
            continue
        for new, old in stems.items():
            tail = '/' + new + '.o'
            if path.endswith(tail):
                before = path[:-len(tail)] + '/' + old + '.o'
                if before in paths:
                    pairs.append((before, path))
                    used.update((before, path))
    return pairs, sorted(paths - used)


def hash_objects(tree):
    result = {}
    for path in sorted((tree / 'build/out/ppc-kboot').rglob('*.o')):
        if path.name in VOLATILE:
            continue
        result[object_key(path.relative_to(tree))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def build_ppc(tree, jobs):
    env = dict(os.environ)
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS', 'BUILD_OUT'):
        env.pop(name, None)
    for flags in PPC_IMAGES:
        subprocess.run(['make', '-j' + str(jobs), '-f', 'build/ppc-kboot.mk', *flags], cwd=tree, env=env,
                       check=True, stdout=subprocess.DEVNULL)
    return hash_objects(tree)


def snapshot(root, revision, destination):
    destination.mkdir(parents=True)
    archive = subprocess.Popen(['git', 'archive', revision], cwd=root, stdout=subprocess.PIPE)
    subprocess.run(['tar', '-x', '-C', str(destination)], stdin=archive.stdout, check=True)
    if archive.wait():
        raise RuntimeError('git archive failed: ' + revision)
    fonts = root / 'assets/fonts'  # 取得済みのフォントは追跡外。像の RAM root が要る。
    if fonts.is_dir():
        shutil.copytree(fonts, destination / 'assets/fonts', dirs_exist_ok=True)
    return destination


def verdict(name, identical, changes, detail):
    """1 つの対象の結論を印字して、落とすべきなら True。"""
    if identical:
        note = ' (Sep-Changes で変わると宣言されているが、同じだった)' if name in changes else ''
        print('PASS ' + name + ': 基点と同じ' + note)
        return False
    if name in changes:
        print('PASS ' + name + ': 基点と違う (Sep-Changes: ' + name + ' の宣言あり)')
    else:
        print('FAIL ' + name + ': 基点と違うのに Sep-Changes: ' + name + ' の宣言が無い')
    for line in detail[:40]:
        print('  ' + line)
    return name not in changes


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', help='基点 (既定は origin/main との merge-base)')
    parser.add_argument('--work', type=Path, required=True, help='作業用の空の置き場')
    parser.add_argument('--only', choices=TARGETS, action='append')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    args = parser.parse_args(argv)
    base = args.base or git(ROOT, 'merge-base', 'origin/main', 'HEAD').strip()
    head = git(ROOT, 'rev-parse', 'HEAD').strip()
    changes = declared(git(ROOT, 'log', '--format=%B', base + '..' + head))
    print('基点 ' + base[:9] + ' → HEAD ' + head[:9] + '、Sep-Changes: ' + (' '.join(sorted(changes)) or '(無し)'))
    if base == head:
        print('PASS: 基点と HEAD が同じ')
        return 0
    failed = False
    targets = args.only or TARGETS
    if 'x86' in targets:
        result = subprocess.run([sys.executable, '-B', 'tools/build_compare.py', '--output', str(args.work / 'x86'),
                                 base, head], cwd=ROOT, capture_output=True, text=True)
        lines = [line for line in (result.stdout + result.stderr).splitlines() if not line.startswith('MATCH ')]
        if result.returncode not in (0, 1):
            print('\n'.join(lines[-40:]))
            raise RuntimeError('tools/build_compare.py failed to run')
        different = [line.split(None, 1)[1].strip() for line in lines if line.startswith('DIFF')]
        moved = renames(git(ROOT, 'diff', '--name-status', '-M90', base, head))
        pairs, different = moved_object_pairs(different, moved)
        for before, after in pairs:
            # 置き場を移した .o: 基点の古い置き場の物と、HEAD の新しい置き場の物の中身を比べる。
            if (args.work / 'x86/base' / before).read_bytes() != (args.work / 'x86/target' / after).read_bytes():
                different.append(after + ' (移す前の ' + before + ' と中身が違う)')
        if pairs:
            print('x86: 置き場を移した .o を ' + str(len(pairs)) + ' 組、中身で比べた')
        failed |= verdict('x86', not different, changes, ['違う: ' + name for name in different])
    if 'ppc' in targets:
        moved = renames(git(ROOT, 'diff', '--name-status', '-M90', base, head))
        hashes = [build_ppc(snapshot(ROOT, revision, args.work / 'ppc' / name), args.jobs)
                  for name, revision in (('base', base), ('head', head))]
        different, only_base, only_head = compare_objects(hashes[0], hashes[1], moved)
        detail = (['違う: ' + name for name in different] + ['基点にだけ在る: ' + name for name in only_base]
                  + ['HEAD にだけ在る: ' + name for name in only_head])
        print('ppc: 基点 ' + str(len(hashes[0])) + ' 本、HEAD ' + str(len(hashes[1])) + ' 本の .o を比べた')
        (args.work / 'ppc-objects.json').write_text(json.dumps(
            dict(different=different, only_base=only_base, only_head=only_head), indent=1))
        failed |= verdict('ppc', not detail, changes, detail)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
