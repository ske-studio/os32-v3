from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.registry import FIELDS, load, validate
from tests.support.media import Failure, main


def build(config):
    load(Path.cwd())


def verify_configs(root, row):
    for config in ("..", ".", "nested/config", "nested\\config", ""):
        candidate = dict(row, arch={"host": [config]}, mutations=[
            dict(mutation, config=config) for mutation in row["mutations"]])
        candidate.pop("ppc_seconds", None)
        try:
            validate([candidate], root)
        except ValueError as exc:
            if str(exc) == "invalid configs":
                continue
        raise Failure("unsafe configuration accepted")


def verify(config):
    root = Path.cwd()
    row = load(root)[0]
    verify_configs(root, row)
    validate([dict(row, ppc_seconds={"core": 900})], root)
    for weights in ([], {"unknown": 1}, {"core": 0}, {"core": True}, {"core": 901}):
        try:
            validate([dict(row, ppc_seconds=weights)], root)
        except ValueError as exc:
            if str(exc) == "invalid ppc_seconds":
                continue
        raise Failure("invalid PPC weight accepted")
    for field in FIELDS:
        incomplete = row.copy()
        del incomplete[field]
        try:
            validate([incomplete], root)
        except ValueError as exc:
            if str(exc) == f"missing field {field}":
                continue
        raise Failure(f"missing field not rejected: {field}")


if __name__ == "__main__":
    main(build, verify)
