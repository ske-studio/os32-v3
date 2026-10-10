#!/usr/bin/env python3
"""T2h/E1-a: HostDrv/SerialFS 配備元の名札の全行を存在・size・CRC で照合。"""
import argparse
import json
from pathlib import Path
import re
import sys
import zlib

from gen_deploy_set import ROOT, valid_path


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
    return errors, sorted(guests)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="HostDrv/SerialFS の配備元ルート")
    parser.add_argument("--deploy-set", type=Path, default=ROOT / "build/out/deploy-set.json")
    parser.add_argument("--guest-paths", type=Path, help="成功時、管理対象一覧をこのファイルにも保存")
    args = parser.parse_args()
    try:
        text = (args.root / ".deploy/manifest.txt").read_text(encoding="utf-8")
        deploy_set = json.loads(args.deploy_set.read_text())
        errors, guests = check_source(args.root, text, deploy_set)
        for error in errors:
            print("[NG] " + error, file=sys.stderr)
        if errors:
            return 1
        listing = "".join(guest + "\n" for guest in guests)
        if args.guest_paths:
            args.guest_paths.write_text(listing)
        sys.stdout.write(listing)
        print("deploy-source: 全行一致、管理対象 {} 件".format(len(guests)), file=sys.stderr)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print("deploy-source: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
