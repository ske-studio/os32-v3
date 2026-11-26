from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main
from tests.support.product import PPC, declared_sources, recipes, source_key, sqlite_directory
from tools.ownership import Inventory


def verify(config):
    inventory = Inventory(Path.cwd())
    expected = declared_sources(inventory, config)
    absent = {name for name in expected if not name.startswith("build/out/") and (name not in inventory.paths or not Path(name).is_file())}
    if absent:
        raise Failure("\n".join(f"{config} {name} missing input" for name in sorted(absent)))
    objects, _ = recipes(config, ("all", "iso"))
    cache = sqlite_directory(config, Path.cwd())
    actual = {source_key(source, cache) for source, _ in objects.values()}
    errors = [f"{config} {name} missing from compilation" for name in expected - actual]
    errors.extend(f"{config} {name} unexpected compilation" for name in actual - expected)
    roles = inventory.tables["OWN_BUILD_ROLE"]
    roots = inventory.tables["OWN_SOURCE_ROOT"]
    registered = set(expected)
    for other in ("pc98", *PPC):
        if other != config:
            registered.update(declared_sources(inventory, other))
    for name in sorted(inventory.paths):
        path = Path(name)
        if (path.parts[0] in roots and path.suffix in (".S", ".s", ".asm", ".c")
                and name not in registered and name not in roles):
            errors.append(f"{name} source has no compilation or declared purpose")
    if errors:
        raise Failure("\n".join(sorted(errors)))


if __name__ == "__main__":
    main(lambda config: None, verify)
