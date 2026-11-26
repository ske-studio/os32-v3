import json
from pathlib import Path
import re
import subprocess
import sys
import shutil
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.product import flags, make_values
from tests.support.tracked import copy_inputs, paths
from tests.tools.kapi_abi import comparison_base, public_definition
from tests.support.kapi_layout import layouts, rust_layout
from tests.support.media import Failure, main

OUT = Path("build/out/check-kapi")


def build(config):
    out = OUT / config
    root = Path.cwd()
    with tempfile.TemporaryDirectory() as tmp:
        tree = Path(tmp)
        copy_inputs(root, tree, paths(root))
        outputs = make_values("pc98", ["KAPI_GENERATED", "RUST_KAPI_RS"], tree)
        rust = next(iter(outputs["RUST_KAPI_RS"]))
        subprocess.run(["make", "-B", "sys/include/kapi_call_generated.h", rust], cwd=tree, check=True)
        products = {name: tree / name for name in outputs["KAPI_GENERATED"] | {rust}}
        if config != "pc98":
            gen = next(iter(make_values(config, ["KB_GEN"], tree)["KB_GEN"]))
            subprocess.run(["make", "-B", *flags(config), gen + "/stamp"], cwd=tree, check=True)
            products = {name: path for name, path in products.items() if name.startswith("sdk/")}
            for path in (tree / gen / "sdk").rglob("*"):
                if path.is_file():
                    products[str(path.relative_to(tree / gen))] = path
        for name, path in products.items():
            destination = out / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)


def verify(config):
    out = OUT / config
    for expected in sorted(out.rglob("*")):
        if expected.is_file():
            public = expected.relative_to(out)
            if config != "pc98" and public.parts[0] != "sdk":
                continue
            if not public.is_file() or public.read_bytes() != expected.read_bytes():
                raise Failure(f"generated definition differs: {public}")
    data = json.loads(Path("sdk/kapi.json").read_text())
    base, _, definition = comparison_base()
    previous = definition(base)
    if public_definition(previous) != public_definition(data) and data["version"] <= previous["version"]:
        raise Failure(f"{config} public definition changed without version increase from {base}")
    document = Path("docs/KAPI_SPEC.md").read_text()
    rows = re.findall(r"^\| (0x[0-9a-fA-F]+) \| (\w+) \| `([^`]+)` \|", document, re.M)
    rows = [(int(off, 16), name, sig) for off, name, sig in rows]
    if not re.search(r"^\| 現在のバージョン \| \*\*" + str(data["version"]) + r"\*\* \|", document, re.M):
        raise Failure("public specification version differs")
    data_names = {row["name"] for row in data["data_fields"]}
    data_rows = [row for row in rows if row[1] in data_names]
    rows = [row for row in rows if row[1] not in data_names]
    if len(rows) != len(data["api"]):
        raise Failure("public specification slot count differs")
    layout = layouts(config, Path("sdk/include/os32"))
    if data_rows != [(layout["KernelAPI"][row["name"]], row["name"], row["type"])
                     for row in data["data_fields"]]:
        raise Failure("public specification data layout differs")
    rust_layout(config, layout)
    for index, fn in enumerate(data["api"]):
        signature = fn["ret"] + "(" + (", ".join(fn["args"]) or "void") + ")"
        if rows[index] != (layout["KernelAPI"][fn["name"]], fn["name"], signature):
            raise Failure(f"public specification differs: slot {index} {fn['name']} {signature}")


if __name__ == "__main__":
    main(build, verify)
