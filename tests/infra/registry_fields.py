from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.registry import load, validate
from tests.support.media import Failure, main


def build(config):
    load(Path.cwd())


def verify(config):
    root = Path.cwd()
    row = load(root)[0]
    for field in row:
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
