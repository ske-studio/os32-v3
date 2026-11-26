"""Read the compiler and final link invocations selected by the product Makefiles."""
from pathlib import Path
import shlex
import subprocess

from tests.support.media import PPC, PPC_MAKE, make


def flags(config):
    return [] if config == "pc98" else [*PPC_MAKE, *PPC[config][0]]


def build(config):
    make(*flags(config), "kernel")


def sqlite_directory(config, root):
    if config == "pc98":
        return None
    values = make_values(config, ["KB_OUT", "KB_BODY_OUT"], root)
    if not values["KB_BODY_OUT"] or values["KB_BODY_OUT"] == values["KB_OUT"]:
        return None
    cache, output = (Path(next(iter(values[key]))) for key in ("KB_BODY_OUT", "KB_OUT"))
    if not (root / cache).resolve().is_relative_to((root / "build/out").resolve()):
        raise OSError(f"{config}: unexpected SQLite body directory {cache}")
    # GNU Make executes the recursive flock even with -n. It needs the
    # directory that the preceding, printed mkdir recipe would create.
    (root / cache).mkdir(parents=True, exist_ok=True)
    return cache, output


def commands(config, goals=("kernel",)):
    sqlite_directory(config, Path.cwd())
    proc = subprocess.run(["make", "-j1", "-Bn", *flags(config), *goals], capture_output=True, text=True)
    if proc.returncode:
        raise OSError(f"{config} Make expansion: {proc.stderr}")
    for line in proc.stdout.replace("\\\n", " ").splitlines():
        if " -o " not in line:
            continue
        args = shlex.split(line)
        if "-o" not in args:
            continue
        output = args[args.index("-o") + 1]
        yield output, args


def recipes(config, goals=("kernel",)):
    objects, links, preprocessed = {}, {}, {}
    for output, args in commands(config, goals):
        compiler = next((i for i, arg in enumerate(args) if arg.endswith("-elf-gcc")), None)
        if "-traditional-cpp" in args:
            inputs = [a for a in args if a.endswith(".asm") and a != output]
            if len(inputs) == 1:
                preprocessed[output] = inputs[0], args[compiler:]
        if compiler is not None and "-c" in args:
            sources = [x for x in args if x.endswith((".c", ".S", ".s"))]
            if len(sources) == 1:
                objects[output] = (sources[0], args[compiler:])
        elif Path(args[0]).name == "nasm" and "-f" in args:
            sources = [x for x in args if x.endswith(".asm")]
            if sources:
                objects[output] = preprocessed.get(sources[0], (sources[0], []))
        elif output.lower().endswith(".elf") and "-r" not in args:
            links[output] = [x for x in args if x.endswith((".o", ".a"))]
    if not objects or not links:
        raise OSError(f"{config}: missing compiler or link evidence")
    return objects, links


def make_values(config, names, root=None):
    """Query the product's selected owner sets before ordering and linking."""
    if not all(name.isidentifier() and name.isupper() for name in names):
        raise ValueError(f"invalid Make variable {names}")
    recipe = "ownership-query:\n\t@" + "".join("$(info OWN " + name + " $(" + name + "))"
                                               for name in names) + "true\n"
    proc = subprocess.run(["make", "--no-print-directory", "-s", *flags(config),
                           "-f", "Makefile", "-f", "-", "ownership-query"],
                          input=recipe, cwd=root, capture_output=True, text=True)
    if proc.returncode:
        raise OSError(f"{config}: cannot expand owner sets: {proc.stderr}")
    values = {}
    for line in proc.stdout.splitlines():
        if line.startswith("OWN "):
            _, name, *items = line.split()
            values[name] = set(items)
    if set(values) != set(names):
        raise OSError(f"{config}: incomplete owner sets")
    return values


def declared_sources(inventory, config):
    explicit = {name for name, configs in inventory.tables["OWN_COMPILE"].items() if config in configs}
    groups = [name for name, configs in inventory.tables["OWN_COMPILE_SET"].items() if config in configs]
    for entries in make_values(config, groups).values():
        explicit.update(entries)
    return explicit


def source_key(source, cache):
    # A cache hit changes the physical directory of generated sources only.
    path = Path(source)
    if cache and path.is_relative_to(cache[0]):
        return str(cache[1] / path.relative_to(cache[0]))
    return source
