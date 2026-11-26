from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.ownership import Inventory, OwnershipError
from tests.support.media import Failure, main


def verify(config):
    try:
        inventory = Inventory(Path.cwd())
        for name in sorted(inventory.paths):
            inventory.lookup(name)
    except OwnershipError as exc:
        raise Failure(str(exc)) from exc


if __name__ == "__main__":
    main(lambda config: None, verify)
