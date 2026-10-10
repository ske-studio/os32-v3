#!/usr/bin/env python3
"""T2h/E1-a: 配備元の名札・期待集合・hsync が配る余剰を照合する。"""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import zlib

import deploy_protect as protect
from gen_deploy_set import ROOT, valid_path

# hsync.c の ls_cb が配送対象から外す予約名 (host 試験で一致を検査)。
HS_TEMP_PREFIX = ".hs~"


def source_extras(root, names, deploy_set):
    """全体 + 明示 sys 同期の範囲。根直下・data も hsync が配る。

    名札自身と任意存在欄、hsync_protect.inc と同じ設定保護は除外。
    sys は第2段で配るので除外しない。host_only は hsync の除外ではない。
    """
    root = Path(root).resolve()
    patterns = []
    for item in deploy_set["allow_list"]:
        guest = item["guest"]
        path = PurePosixPath(guest)
        if (not valid_path(guest, absolute=True) or guest.startswith("//") or
                any(ch in guest for ch in "?[]\x00") or "**" in guest or
                "*" in str(path.parent) or item["check"] != "optional"):
            raise ValueError("invalid allow-list entry: " + str(guest))
        patterns.append(path)
    protect.check_tree(str(root))
    extras = []

    def walk_error(exc):
        raise exc

    for parent, dirs, files in os.walk(root, onerror=walk_error, followlinks=False):
        # 保護されたディレクトリ経路へは hsync も入らない。
        dirs[:] = sorted(name for name in dirs if not name.startswith(HS_TEMP_PREFIX) and not
                         protect.protected_ancestor(str(root), os.path.join(parent, name)))
        for name in sorted(files):
            if name.startswith(HS_TEMP_PREFIX):
                continue
            path = Path(parent) / name
            guest = "/" + path.relative_to(root).as_posix()
            pp = PurePosixPath(guest)
            if (guest.lstrip("/") in names or guest == "/.deploy/manifest.txt" or
                    any(pp.parent == pattern.parent and
                        fnmatch.fnmatchcase(pp.name, pattern.name) for pattern in patterns) or
                    protect.protected_ancestor(str(root), str(path)) or
                    protect.is_protected(str(root), str(path))):
                continue
            extras.append(guest)
    return sorted(extras)


def check_source(root, text, deploy_set):
    """全行の欠け・不一致を列挙。mtime は内容一致の証拠に使わない。"""
    root = Path(root).resolve()
    errors = []
    headers = {}
    rows = []
    body = False
    for lineno, line in enumerate(text.splitlines(), 1):
        if not body:
            if line == "---":
                body = True
                continue
            key, sep, value = line.partition("=")
            if not sep or key in headers:
                errors.append("line {}: 不正/重複ヘッダ".format(lineno))
            else:
                headers[key] = value
        else:
            rows.append((lineno, line))
    if not body:
        errors.append("名札の --- がない")
    if headers.get("format") not in ("1", "2"):
        errors.append("名札の format が未対応")
    for key in ("build", "generated"):
        if not headers.get(key):
            errors.append("名札の {} がない".format(key))
    if headers.get("format") == "2":
        for key in ("kapi", "kapi_version"):
            if not re.fullmatch(r"[0-9]+", headers.get(key, "")):
                errors.append("名札の {} が不正/欠落".format(key))
    count = headers.get("count", "")
    if not re.fullmatch(r"[0-9]+", count) or int(count) != len(rows):
        errors.append("名札の count={} != {} 行".format(count, len(rows)))
    seen = set()
    for lineno, line in rows:
        cols = line.split(" ")
        if (len(cols) != 4 or not valid_path(cols[0]) or
                not re.fullmatch(r"[0-9]+", cols[1]) or
                not re.fullmatch(r"[0-9a-f]{8}", cols[2]) or
                not re.fullmatch(r"[0-9]+", cols[3])):
            errors.append("line {}: 不正なファイル行: {}".format(lineno, line))
            continue
        name, size, crc, _ = cols
        if name in seen:
            errors.append("line {}: 重複: {}".format(lineno, name))
        seen.add(name)
        path = root / name
        try:
            if not path.resolve().is_relative_to(root) or not path.is_file():
                raise ValueError("配備元ファイルなし/範囲外")
            data = path.read_bytes()
        except (OSError, ValueError) as exc:
            errors.append("line {}: {}: {}".format(lineno, name, exc))
            continue
        if len(data) != int(size):
            errors.append("line {}: {}: size {} != {}".format(lineno, name, len(data), size))
        actual_crc = "{:08x}".format(zlib.crc32(data) & 0xffffffff)
        if actual_crc != crc:
            errors.append("line {}: {}: CRC {} != {}".format(lineno, name, actual_crc, crc))
    if deploy_set.get("format") != 1 or not deploy_set.get("files"):
        raise ValueError("deploy-set の format/集合が不正")
    guests = []
    for entry in deploy_set["files"]:
        guest = entry["guest"]
        if not valid_path(guest, absolute=True) or guest in guests:
            raise ValueError("deploy-set の guest が不正/重複: " + str(guest))
        guests.append(guest)
        if guest.lstrip("/") not in seen:
            errors.append("名札に管理対象の行がない: " + guest)
        path = root / guest.lstrip("/")
        if path.is_file() and path.resolve().is_relative_to(root):
            data = path.read_bytes()
            if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
                errors.append("deploy-set と配備元が不一致: " + guest)
    try:
        errors.extend("extra: " + guest for guest in source_extras(root, seen, deploy_set))
    except (OSError, protect.ProtectError) as exc:
        errors.append("配備元を走査できない: " + str(exc))
    return errors, sorted(guests)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="HostDrv/SerialFS の配備元ルート")
    parser.add_argument("--deploy-set", type=Path, default=ROOT / "build/out/deploy-set.json")
    parser.add_argument("--guest-paths", type=Path, help="成功時、管理対象一覧をこのファイルにも保存")
    parser.add_argument("--prune-extra", action="store_true", help="名札外ファイルの掃除候補 (既定は dry-run)")
    parser.add_argument("--delete", action="store_true", help="--prune-extra の候補を削除")
    args = parser.parse_args()
    if args.delete and not args.prune_extra:
        parser.error("--delete requires --prune-extra")
    try:
        text = (args.root / ".deploy/manifest.txt").read_text(encoding="utf-8")
        deploy_set = json.loads(args.deploy_set.read_text())
        errors, guests = check_source(args.root, text, deploy_set)
        if args.prune_extra:
            # 名札の破損/欠損時に、正規ファイルを余剰と見なして消さない。
            invalid = [error for error in errors if not error.startswith("extra: ")]
            if invalid:
                for error in invalid:
                    print("[NG] " + error, file=sys.stderr)
                return 1
            extras = [error.removeprefix("extra: ") for error in errors]
            root = args.root.resolve()
            for guest in extras:
                print(("remove: " if args.delete else "dry-run: ") + guest)
                if args.delete:
                    protect.check_tree(str(root))
                    path = root / guest.lstrip("/")
                    if (protect.protected_ancestor(str(root), str(path)) or
                            protect.is_protected(str(root), str(path))):
                        raise ValueError("削除前に保護対象へ変化: " + guest)
                    if not stat.S_ISREG(path.lstat().st_mode):
                        raise ValueError("削除対象が通常ファイルでない: " + guest)
                    path.unlink()
            print("deploy-source: 余剰 {} 件、{}".format(
                len(extras), "削除" if args.delete else "未削除 (dry-run)"))
            return 0
        for error in errors:
            print("[NG] " + error, file=sys.stderr)
        if errors:
            return 1
        listing = "".join(guest + "\n" for guest in guests)
        if args.guest_paths:
            args.guest_paths.write_text(listing)
        sys.stdout.write(listing)
        print("deploy-source: 全行一致、管理対象 {} 件".format(len(guests)), file=sys.stderr)
    except (OSError, ValueError, KeyError, TypeError, protect.ProtectError) as exc:
        print("deploy-source: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
