from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main
from tests.support.product import build, recipes
from tools.ownership import Inventory


def target_wrong(config, owner):
    targets = ("x86", "pc98") if config == "pc98" else ("ppc", "powermac")
    return owner["kind"] in ("CPU固有実装", "機種固有実装") and owner["target"] not in targets


def rule(config, reader, header):
    origin, dest = reader["kind"], header["kind"]
    if target_wrong(config, reader) or target_wrong(config, header):
        return "R6"
    if header["header_role"] == "fixed_contract":
        return None
    if origin == "共通実装":
        if header["header_role"] == "named":
            return "R4"
        return {"CPU固有実装": "R1", "機種固有実装": "R2", "デバイス固有実装": "R3"}.get(dest)
    if origin == "デバイス固有実装" and dest == "CPU固有実装":
        return "R5"
    if origin == "CPU固有実装" and dest == "機種固有実装":
        return "R7"


def observe(source, args):
    cmd = args.copy()
    out = cmd.index("-o")
    del cmd[out:out+2]
    cmd = [x for x in cmd if x not in ("-MMD", "-MP", "-c")]
    proc = subprocess.run([*cmd, "-H", "-E", "-o", "/dev/null"], capture_output=True, text=True)
    if proc.returncode:
        raise OSError(f"{source}: preprocessing failed: {proc.stderr[-900:]}")
    stack = [Path(source).resolve()]
    for index, arg in enumerate(cmd[:-1]):
        if arg == "-imacros":
            forced = Path(cmd[index + 1]).resolve()
            if not forced.is_file():
                raise OSError(f"{source}: unresolved macro input {cmd[index + 1]}")
            yield stack[0], forced
    for line in proc.stderr.splitlines():
        match = re.fullmatch(r"(\.+) (.+)", line)
        if match:
            depth = len(match[1])
            stack[depth:] = []
            if len(stack) != depth:
                raise OSError(f"{source}: missing include parent {line}")
            child = Path(match[2]).resolve()
            yield stack[-1], child
            stack.append(child)


def verify(config):
    inventory = Inventory(Path.cwd())
    objects, links = recipes(config)
    edges = set()
    errors = set()
    for obj, (source, args) in objects.items():
        if not args:
            continue
        if source in inventory.paths and target_wrong(config, inventory.lookup(source)):
            errors.add(f"{config} R6 {source}")
        edges.update((parent, child, obj) for parent, child in observe(source, args))
    root = Path.cwd()
    def owner(name):
        if name in inventory.paths:
            return inventory.lookup(name)
        entry = inventory.tables["OWN_GENERATED"].get(name)
        if not entry:
            raise OSError(f"{config} unknown generated ownership {name}")
        if name.endswith(".h") and len(entry) < 3:
            raise OSError(f"{config} missing generated header role {name}")
        return dict(kind=entry[0], target=entry[1], header_role=entry[2] if len(entry) > 2 else None)
    for parent, child, obj in sorted(edges):
        if not parent.is_relative_to(root) or not child.is_relative_to(root):
            continue
        p, c = str(parent.relative_to(root)), str(child.relative_to(root))
        reader, header = owner(p), owner(c)
        if header["header_role"] == "module_import" and p == objects[obj][0]:
            module = inventory.tables["OWN_GENERATED"][c][3]
            if obj in links.get(module, ()) and not target_wrong(config, reader):
                continue
        violation = rule(config, reader, header)
        if violation:
            errors.add(f"{config} {violation} {p} -> {c}")
    if errors:
        raise Failure("\n".join(sorted(errors)))


if __name__ == "__main__":
    main(build, verify)
