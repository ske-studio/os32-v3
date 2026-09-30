#!/usr/bin/env python3
"""配備マニフェスト (層別 deploy.yaml) の一元定義とマージ

配備定義は所有する層ごとに分かれている。カーネル層が boot: と
ディレクトリ構造を持ち、ユーザーランド・標準アプリ・ゲームは自層の
files: だけを持つ。

このリストを二重に持つと必ず食い違う (実際 tools/deploy.yaml が層分割で
削除されたあと hostdrv_deploy.py だけが古い単一ファイルを見続け、
`make deploy` が無言の空振りになっていた)。参照側は必ずここを使うこと。
"""

import glob
import os
import sys

import yaml

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 読み込み順 = マージ順。あとの層が boot: を持てば上書きする。
MANIFEST_RELPATHS = [
    os.path.join('build', 'core.yaml'),
    os.path.join('userland', 'deploy.yaml'),
    # 外部リポジトリ (git submodule)。未取得なら無いので load_merged が読み飛ばす。
    os.path.join('apps', 'deploy.yaml'),
    os.path.join('game', 'deploy.yaml'),
]

DEPLOY_MANIFESTS = [os.path.join(PROJ_DIR, p) for p in MANIFEST_RELPATHS]

# 本体リポジトリが持つ層 (submodule を除く)。`make all` だけで成果物が揃うのは
# ここまでで、CD のパッケージ (tools/mkpkg.py --plan) はこの 2 つから作る。
CORE_MANIFEST_RELPATHS = MANIFEST_RELPATHS[:2]


def load_merged(relpaths=None):
    """層ごとの配備定義をマージして返す。1 つも無ければ None。

    boot: と filesystem.directories: はカーネル層 (build/core.yaml) が持つ。
    filesystem.files: は全マニフェストを読み込み順に連結する。
    """
    merged = {'filesystem': {'directories': [], 'files': []}}
    found = False
    paths = DEPLOY_MANIFESTS if relpaths is None else \
        [os.path.join(PROJ_DIR, p) for p in relpaths]
    for path in paths:
        if not os.path.isfile(path):
            continue
        found = True
        with open(path, 'r', encoding='utf-8') as f:
            d = yaml.safe_load(f) or {}
        if 'boot' in d:
            merged['boot'] = d['boot']
        fs = d.get('filesystem') or {}
        merged['filesystem']['directories'].extend(fs.get('directories') or [])
        rel = os.path.relpath(path, PROJ_DIR)
        for e in fs.get('files') or []:
            e = dict(e)
            e['_manifest'] = rel    # どの層の行か (検査の報告用)
            merged['filesystem']['files'].append(e)
    if not found:
        print("Error: 配備定義が 1 つも見つかりません:\n  {}".format(
            "\n  ".join(paths)), file=sys.stderr)
        return None
    return merged


def is_glob_entry(entry):
    """files: の 1 行が glob か (type: glob、または host に * を含む)"""
    return entry.get('type') == 'glob' or '*' in entry['host']


def resolve_entry(entry, proj_dir=PROJ_DIR):
    """files: の 1 行を [(host のプロジェクト相対パス, ゲストパス), ...] に展開する

    配備側 (nhd_deploy / hostdrv_deploy) と同じ解釈:
      - glob は一致したファイルごと。exclude: はベース名で外す
      - guest が '/' で終わればホストのベース名を足す
      - 実ファイル指定はファイルが無くても 1 件返す (欠損の扱いは呼び手が決める)
    """
    host = entry['host']
    guest = entry['guest']
    exclude = entry.get('exclude') or []
    out = []
    if is_glob_entry(entry):
        for fpath in sorted(glob.glob(os.path.join(proj_dir, host))):
            base = os.path.basename(fpath)
            if base in exclude or not os.path.isfile(fpath):
                continue
            rel = os.path.relpath(fpath, proj_dir).replace(os.sep, '/')
            out.append((rel, guest + base if guest.endswith('/') else guest))
    else:
        base = os.path.basename(host)
        out.append((host, guest + base if guest.endswith('/') else guest))
    return out
