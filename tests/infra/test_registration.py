from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.registry import load
from tests.support.media import Failure, main


def verify(config):
    try:
        load(Path.cwd())
    except ValueError as exc:
        raise Failure(str(exc)) from exc


if __name__ == "__main__":
    main(lambda config: None, verify)
