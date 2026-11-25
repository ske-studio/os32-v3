"""Read and validate the candidate's declarative check registry."""
from pathlib import Path
import re
import tomllib

FIELDS = set("id spec subsystem level sources environment arch needs expect limits gates mutations".split())
OWNERS = set("kern hal arch platform dev gfx sound input fs net kapi boot lib sdk userland tools integration system infra".split())
ENVIRONMENTS = {"python", "linux-x86-cross", "linux-ppc-cross", "linux-cross"}


def demand(condition, message):
    if not condition:
        raise ValueError(message)


def string(value):
    return isinstance(value, str) and bool(value.strip())


def identifier(name):
    return string(name) and re.fullmatch("[a-z0-9_]+", name)


def strings(values, empty=False):
    return isinstance(values, list) and (empty or values) and all(map(string, values)) and len(set(values)) == len(values)


def relative(name):
    return string(name) and not Path(name).is_absolute() and ".." not in Path(name).parts and name == str(Path(name))


def validate(checks, root):
    ids, mutation_ids = set(), set()
    demand(isinstance(checks, list) and checks, "expected [[check]]")
    for row in checks:
        demand(isinstance(row, dict), "invalid check")
        missing = FIELDS - row.keys()
        demand(not missing, "missing field " + ", ".join(sorted(missing)))
        demand(row.keys() == FIELDS, "unexpected field")
        name = row["id"]
        demand(identifier(name) and name not in ids, f"invalid or duplicate id {name}")
        ids.add(name)
        owner = row["subsystem"]
        demand(string(owner) and owner in OWNERS, "invalid subsystem")
        demand((root / "tests" / owner / f"{name}.py").is_file(), f"missing body {name}")
        for field in ("environment", "level", "expect", "limits"):
            demand(string(row[field]), f"invalid {field}")
        demand(row["environment"] in ENVIRONMENTS, "unknown environment")
        demand(row["level"] in ("L0", "L1", "L2", "L3", "L4", "L5", "M"), "invalid level")
        for field in ("sources", "needs", "gates"):
            demand(strings(row[field], empty=field == "needs"), f"invalid {field}")
        demand(set(row["gates"]) <= {"push", "main"}, "unknown gate")
        spec = row["spec"]
        demand(isinstance(spec, dict) and spec.keys() == {"path", "section"}, "invalid spec")
        path = spec["path"]
        demand(relative(path) and path.startswith("docs/") and not path.startswith(("docs/archive/", "docs/hw/")), "invalid spec path")
        document = root / path
        demand(document.is_file() and string(spec["section"]), "missing spec")
        demand(spec["section"] in re.findall(r"^#{1,6} (.+)$", document.read_text(), re.M), "missing spec section")
        arch = row["arch"]
        demand(isinstance(arch, dict) and arch and arch.keys() <= {"x86", "ppc", "host"}, "invalid arch")
        configs = set()
        for values in arch.values():
            demand(strings(values) and configs.isdisjoint(values), "invalid configs")
            configs.update(values)
        demand(isinstance(row["mutations"], list) and row["mutations"], "missing mutations")
        for mutation in row["mutations"]:
            demand(isinstance(mutation, dict), "invalid mutation")
            common = {"id", "config", "when", "fail"}
            operations = mutation.keys() - common
            demand(common <= mutation.keys() and operations in ({"remove"}, {"append"}, {"replace", "old", "new"}), "invalid mutation fields")
            demand(all(string(v) for v in mutation.values()), "invalid mutation value")
            mid = mutation["id"]
            demand(identifier(mid) and mid not in mutation_ids, "invalid or duplicate mutation id")
            mutation_ids.add(mid)
            demand(mutation["config"] in configs and mutation["when"] == "after_build", "invalid mutation reference")
            target = next(mutation[k] for k in ("remove", "append", "replace") if k in mutation)
            demand(relative(target), "invalid mutation target")
    return checks


def load(root):
    with (root / "tests/registry.toml").open("rb") as stream:
        data = tomllib.load(stream)
    demand(data.keys() == {"check"}, "expected check table")
    return validate(data["check"], root)
