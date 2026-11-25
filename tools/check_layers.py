#!/usr/bin/env python3
"""LAYER_GUARD_DESIGN: 像の .d から層の違反 (読んだ側 -> 読まれたヘッダ) を数え、基準と照らす。"""
import argparse
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BASELINE = 'tools/layers_baseline.json'
# 像の名前 -> (arch, platform, build/out の下の .d の根)
IMAGES = {
    'x86-pc98': ('x86', 'pc98', ('obj', 'cd', 'cirrus', 'fep', 'fm', 'geforce', 'hwdev', 'kcg',
                                 'kdebug', 'lan', 'pcm', 'sqlite-proto', 'trident', 'v86', 'usb')),
    'ppc-powermac': ('ppc', 'powermac', ('ppc-kboot', 'ppc-powermac')),
}
# 数えない場所: CI (tools/ppc/compile_ci.py の 4 像) が組まない像の置き場 (根の直下の shell・neg*)。
SKIP_FIRST = {'ppc-kboot': ('shell', 'neg')}
# include/ にある機種名のヘッダ -> (軸, 機種)。置き場を移したら行を消す。
NAMED = {
    'pc98.h': ('platform', 'pc98'), 'pegc.h': ('platform', 'pc98'),
    'wab_xe10.h': ('platform', 'pc98'),
    'v86_call.h': ('arch', 'x86'),
}
DEVICE_ROOTS = ('drivers', 'hwdev', 'kdebug')
GENERATED_KAPI = '/gen/kapi/kapi_generated.c'
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*(["<])([^">]+)[">]', re.M)
COMMENT = re.compile(r'/\*.*?\*/', re.S)  # 行頭の # だけ拾うので // の行は元から当たらない


def owner(path):
    """(種類, 軸の値)。種類は arch / platform / device / named / common。"""
    parts = path.split('/')
    if parts[0] in ('arch', 'platform') and len(parts) > 2:
        return parts[0], parts[1]
    if parts[0] in DEVICE_ROOTS:
        return 'device', None
    if parts[0] == 'include' and len(parts) == 2 and parts[1] in NAMED:
        return ('named',) + NAMED[parts[1]]
    return 'common', None


def fixed_name(path):
    """契約の実装の固定名 (arch/<a>/arch_*.h、platform/<p>/platform_*.h)。"""
    parts = path.split('/')
    return (len(parts) == 3 and parts[0] in ('arch', 'platform')
            and parts[2].startswith(parts[0] + '_') and parts[2].endswith('.h'))


def foreign(image, kind):
    """その持ち主が像と違う arch・platform の物か。"""
    arch, platform, _ = IMAGES[image]
    if kind[0] == 'arch':
        return kind[1] != arch
    if kind[0] == 'platform':
        return kind[1] not in (platform, 'none')
    if kind[0] == 'named':
        return kind[2] != (platform if kind[1] == 'platform' else arch)
    return False


def rule(image, reader, header):
    """辺 reader -> header の規則の名前。違反でなければ None。"""
    src, dst = owner(reader), owner(header)
    if foreign(image, dst):
        return 'R6'
    private = dst[0] in ('arch', 'platform') and not fixed_name(header)
    if src[0] == 'common':
        if private:
            return 'R1' if dst[0] == 'arch' else 'R2'
        if dst[0] == 'device':
            return 'R3'
        if dst[0] == 'named':
            return 'R4'
    if src[0] == 'device' and dst[0] == 'arch' and private:
        return 'R5'
    if src[0] == 'arch' and dst[0] == 'platform' and private:
        return 'R7'
    return None


def parse_dep(text):
    """.d の最初の規則の前提の一覧 (先頭がソース)。"""
    text = text.replace('\\\n', ' ')
    match = re.match(r'\s*\S+:[ \t]*([^\n]*)', text)
    return [os.path.normpath(word) for word in match[1].split()] if match else []


def inside(path):
    return not (path.startswith('/') or path == '..' or path.startswith('../'))


def resolve(reader, mark, name, files):
    """#include の名前を、その TU が読んだファイルの集合の中で解決する。

    "name" は読んだ側のディレクトリからの相対が先。<name> は -I で引くので末尾の一致だけ。
    """
    local = os.path.normpath(os.path.join(os.path.dirname(reader), name))
    if mark == '"' and local in files:
        return [local]
    tail = os.path.normpath(name)
    while tail.startswith('../'):
        tail = tail[3:]
    return sorted(path for path in files if path == tail or path.endswith('/' + tail))


class Tree:
    def __init__(self, root):
        self.root = Path(root)
        self.cache = {}

    def includes(self, path):
        if path not in self.cache:
            try:
                text = (self.root / path).read_text(encoding='utf-8', errors='replace')
            except OSError:
                text = ''
            self.cache[path] = INCLUDE.findall(COMMENT.sub(' ', text))
        return self.cache[path]


def measure(root, image):
    """(違反の辺 {(規則, 読んだ側, ヘッダ): TU の集合}, TU の集合, .d が無かった根)。"""
    root = Path(root)
    tree = Tree(root)
    arch, platform, dep_roots = IMAGES[image]
    edges, units, empty = {}, set(), []
    for dep_root in dep_roots:
        base = root / 'build/out' / dep_root
        skip = SKIP_FIRST.get(dep_root, ())
        deps = [dep for dep in sorted(base.rglob('*.d'))
                if not (dep_root == 'ppc-powermac' and dep.relative_to(base).parts[0] == 'user')
                if not dep.relative_to(base).parts[0].startswith(skip)]
        if not deps:
            empty.append(dep_root)
        for dep in deps:
            words = parse_dep(dep.read_text(errors='replace'))
            if not words or not (root / words[0]).is_file():
                continue  # 消した・改名したソースの古い .d は数えない
            real = words[0]
            if real.endswith(GENERATED_KAPI):
                unit = 'sys/kapi/kapi_generated.c'
            elif not inside(real) or real.startswith('build/'):
                continue
            else:
                unit = real
            units.add(unit)
            kind = owner(unit)
            if kind[0] in ('arch', 'platform') and foreign(image, kind):
                edges.setdefault(('R6', unit, '-'), set()).add(unit)
            files = {word for word in words if inside(word)}
            for path in files:
                if path.startswith('build/') and path != real:
                    continue
                reader = unit if path == real else path
                for mark, name in tree.includes(path):
                    for header in resolve(path, mark, name, files):
                        found = rule(image, reader, header)
                        if found:
                            edges.setdefault((found, reader, header), set()).add(unit)
    return edges, units, empty


def compare(observed, expected):
    errors = ['new violation: ' + ' '.join(edge) for edge in sorted(observed - expected)]
    errors += ['stale baseline (remove): ' + ' '.join(edge) for edge in sorted(expected - observed)]
    return errors


def dump(baseline):
    lines = []
    for image in sorted(baseline):
        rows = ',\n'.join('  ' + json.dumps(row) for row in baseline[image])
        lines.append(json.dumps(image) + ': [\n' + rows + ('\n]' if rows else ']'))
    return '{\n' + ',\n'.join(lines) + '\n}\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--image', action='append', choices=sorted(IMAGES))
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--write-baseline', action='store_true')
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args(argv)
    path = args.baseline or args.root / BASELINE
    baseline = json.loads(path.read_text()) if path.is_file() else {}
    failed = False
    for image in args.image or sorted(IMAGES):
        edges, units, empty = measure(args.root, image)
        counts = {}
        for edge in edges:
            counts[edge[0]] = counts.get(edge[0], 0) + 1
        print(image + ': units=' + str(len(units)) + ' edges=' + str(len(edges)) + ' '
              + ' '.join(name + '=' + str(counts[name]) for name in sorted(counts)))
        if args.verbose:
            for edge in sorted(edges):
                print('  ' + ' '.join(edge) + ' units=' + str(len(edges[edge])))
        for dep_root in empty:
            print(image + ': no .d under build/out/' + dep_root + ' (build the image first)')
            failed = True
        if empty:
            continue
        if args.write_baseline:
            baseline[image] = [list(edge) for edge in sorted(edges)]
            continue
        errors = compare(set(edges), {tuple(row) for row in baseline.get(image, [])})
        for error in errors:
            print(image + ': ' + error)
        failed = failed or bool(errors)
    if args.write_baseline and not failed:
        path.write_text(dump(baseline))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
