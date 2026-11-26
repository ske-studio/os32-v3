from contextlib import chdir
from pathlib import Path
import hashlib
import json
import os
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import (Failure, PPC, PPC_OUT, main, ppc_build, ppc_verify,
                                 require, x86_build, x86_verify)
sys.path.insert(0, str(Path("tools").resolve()))
from build_compare import artifacts


def inventory(config):
    root = Path.cwd()
    if config == "pc98":
        paths = artifacts(root, True)
        paths.update(p for p in Path("packages").iterdir() if p.is_file())
        paths.update(Path(".").rglob("*.o"))
        presence = set()
    else:
        folder = PPC_OUT / PPC[config][1]
        paths = set(Path(".").rglob("*.o")) | {folder / "PPCKERN.ELF", folder / "boot.iso"}
        presence = {folder / "boot.iso"}
        if folder / "root_image_blob.o" in paths:
            presence.add(folder / "root_image_blob.o")
    result = {}
    for path in sorted(paths | presence):
        if path in presence:
            require(path)
            result[str(path)] = "present"
        else:
            if not path.is_file():
                raise Failure(f"missing {path}")
            with path.open("rb") as file:
                result[str(path)] = hashlib.file_digest(file, "sha256").hexdigest()
    return result


def build(config):
    os.environ.pop("OS32_PRODUCT_DIR", None)  # Two independent clean builds.
    build_media, verify_media = (x86_build, x86_verify) if config == "pc98" else (ppc_build, ppc_verify)
    # The caller supplies an isolated candidate with no previous build products.
    with tempfile.TemporaryDirectory() as tmp:
        twin = Path(tmp) / "candidate"
        shutil.copytree(Path.cwd(), twin)
        with chdir(twin):
            build_media(config)
            verify_media(config)
            first = inventory(config)
        build_media(config)
        verify_media(config)
    Path(".first-build.json").write_text(json.dumps(first))


def verify(config):
    first = json.loads(Path(".first-build.json").read_text())
    current = inventory(config)
    differences = sorted(p for p in first.keys() | current.keys() if first.get(p) != current.get(p))
    if differences:
        raise Failure("different artifacts: " + ", ".join(differences))


if __name__ == "__main__":
    main(build, verify)
