#!/usr/bin/env python3
"""Validate a guest acceptance index and render an uncommitted Markdown table."""
import argparse
from pathlib import Path
import sys
import yaml

ROOT = Path(__file__).resolve().parents[1]
INDEX = ROOT / "tools/tests/guest_acceptance.yaml"
REQUIRED = {"id", "gate", "contract", "mode", "prereq", "entry", "observe", "expect", "evidence"}


def validate(data, root=ROOT):
    if not isinstance(data, list) or not data:
        raise ValueError("index must be a nonempty list")
    ids = set()
    for case in data:
        if not isinstance(case, dict):
            raise ValueError("case must be a mapping")
        deferred = case.get("mode") == "manual" and case.get("status") == "deferred"
        required = {"id", "mode", "status", "contract"} if deferred else REQUIRED
        if required - case.keys():
            raise ValueError("missing fields: %s" % sorted(required - case.keys()))
        if not isinstance(case["id"], str) or not case["id"] or case["id"] in ids:
            raise ValueError("invalid or duplicate id")
        ids.add(case["id"])
        if case["mode"] not in {"batch", "tool", "manual"}:
            raise ValueError("invalid mode")
        contract = case["contract"]
        if not isinstance(contract, str) or not (root / contract.split("#", 1)[0]).is_file():
            raise ValueError("contract file missing")
        if not deferred and any(case[key] is None or case[key] == "" for key in REQUIRED):
            raise ValueError("empty required field")
        if case["mode"] == "batch":
            from guest_tests import parse_list
            entries, errors = parse_list((root / "tools/tests/guest_tests.txt").read_text())
            ref = case["entry"]
            if errors or not isinstance(ref, dict) or set(ref) != {"guest_tests"} or ref["guest_tests"] not in {e.name for e in entries}:
                raise ValueError("batch entry must reference a guest_tests.txt name")
    return data


def render(cases):
    fields = ["id", "gate", "contract", "mode", "prereq", "entry", "observe", "expect", "evidence", "status"]
    def cell(value):
        return str(value).replace("|", "\\|").replace("\n", " ")
    return "\n".join(["| " + " | ".join(fields) + " |", "| " + " | ".join(["---"] * len(fields)) + " |"] +
                     ["| " + " | ".join(cell(case.get(f, "")) for f in fields) + " |" for case in cases]) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", nargs="?", type=Path, default=INDEX)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write", type=Path)
    args = parser.parse_args()
    try:
        cases = validate(yaml.safe_load(args.index.read_text()))
        if args.write:
            out = args.write.expanduser().resolve()
            if not out.is_relative_to(Path.home() / "os32-tmp"):
                raise ValueError("--write must be under ~/os32-tmp")
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(render(cases))
        elif not args.check:
            print(render(cases), end="")
    except (ValueError, OSError, yaml.YAMLError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
