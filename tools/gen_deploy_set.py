#!/usr/bin/env python3
"""T2h/E3: make all の配備期待集合を固定する。媒体・配備元には書かない。"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

from deploy_manifests import CORE_MANIFEST_RELPATHS, load_merged, resolve_entry

ROOT = Path(__file__).resolve().parents[1]
ALLOW_LIST = [
    {"guest": "/etc/settings.db", "check": "exists"},
    {"guest": "/etc/system.cfg", "check": "exists"},
    {"guest": "/var/log/*", "check": "exists"},
]


def valid_path(path, absolute=False):
    """名札にも安全に使える、正規化済み POSIX ファイルパス。"""
    return (isinstance(path, str) and bool(path) and
            not any(ch.isspace() for ch in path) and "\\" not in path and
            not any(ord(ch) < 32 for ch in path) and
            path.startswith("/") == absolute and
            ".." not in path.split("/") and str(PurePosixPath(path)) == path and
            path not in (".", "/"))


def generate(root, merged, generation):
    root = Path(root).resolve()
    files = []
    guests = set()
    for entry in merged["filesystem"]["files"]:
        for host, guest in resolve_entry(entry, str(root)):
            if not valid_path(host) or not valid_path(guest, absolute=True):
                raise ValueError("不正な配備パス: {} -> {}".format(host, guest))
            # ゲストが書く設定は存在だけの別欄。初期内容を期待 hash にしない。
            if any(PurePosixPath(guest).match(item["guest"]) for item in ALLOW_LIST):
                continue
            if guest in guests:
                raise ValueError("ゲストパス重複: " + guest)
            guests.add(guest)
            path = root / host
            if not path.resolve().is_relative_to(root) or not path.is_file():
                raise ValueError("配備元ファイルなし/範囲外: " + host)
            data = path.read_bytes()
            files.append({"host": host, "guest": guest, "size": len(data),
                          "sha256": hashlib.sha256(data).hexdigest()})
    if not files:
        raise ValueError("配備集合が空")
    files.sort(key=lambda item: (item["guest"], item["host"]))
    return {"format": 1, "generation_build_id": generation["build_id"],
            "kernel_commit": generation["kernel_commit"],
            "kapi_version": generation["kapi_version"],
            "generations": generation["generations"],
            "files": files, "allow_list": ALLOW_LIST,
            "external": {"apps": "H-7: excluded", "game": "H-7: excluded"}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="ビルド済みのソース木")
    parser.add_argument("--output", type=Path, help="既定: <root>/build/out/deploy-set.json")
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output or root / "build/out/deploy-set.json"
    try:
        for rel in CORE_MANIFEST_RELPATHS:
            if not (root / rel).is_file():
                raise ValueError("配備定義なし: " + rel)
        merged = load_merged([str(root / rel) for rel in CORE_MANIFEST_RELPATHS])
        generation = json.loads((root / "build/out/generations-manifest.json").read_text())
        result = generate(root, merged, generation)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print("deploy-set: " + str(exc), file=sys.stderr)
        return 1
    print("deploy-set: {} files -> {}".format(len(result["files"]), output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
