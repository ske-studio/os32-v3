"""Query the ownership declarations of an indexed worktree or a specified Git tree."""
import argparse
from collections import defaultdict
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


class OwnershipError(ValueError):
    pass


def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root)


class Inventory:
    def __init__(self, root, revision=None):
        self.images = {}
        self.root = root
        self.revision = revision
        if revision:
            self.paths = set(git(root, "ls-tree", "-rz", "--name-only", revision).decode().split("\0")[:-1])
        elif (root / ".git").exists():
            self.paths = set(git(root, "ls-files", "-z").decode().split("\0")[:-1])
        else:
            self.paths = set(json.loads((root / ".check-index.json").read_text()))
        self.tables = defaultdict(dict)
        path = "build/sources/ownership.mk"
        source = git(root, "show", f"{revision}:{path}").decode() if revision else (root / path).read_text()
        for number, line in enumerate(source.splitlines(), 1):
            if not line.strip() or line.startswith("#"):
                continue
            key, payload = line.split(" += ")
            name, *value = payload.split("|")
            table = self.tables[key]
            if name in table and table[name] != value:
                raise OwnershipError(f"{path}:{number} conflicting {key} {name}: {table[name]} / {value}")
            table[name] = value
        sdk = (git(root, "show", f"{revision}:build/sdk.mk").decode() if revision
               else (root / "build/sdk.mk").read_text())
        self.public = {"sdk/kapi.json"}
        header_dirs = re.search(r"^SDK_LIB_HEADER_DIRS\s*=\s*(.*)$", sdk, re.M)
        patterns = [f"userland/lib/{d}/*.h" for d in header_dirs[1].split()] if header_dirs else []
        for line in sdk.splitlines():
            if line.startswith("\tcp "):
                inputs = line.split()[1:-1]
                if "-r" in inputs:
                    for directory in inputs[1:]:
                        self.public.update(p for p in self.paths if p.startswith(directory + "/"))
                patterns.extend(word for word in inputs if "$" not in word and word != "-r")
        self.public.update(p for p in self.paths if not p.endswith("_internal.h") and
                           any(PurePosixPath(p).match(pattern) for pattern in patterns))
        for key in ("OWN_FILE", "OWN_HEADER", "OWN_BUILD_ROLE", "OWN_CONTRACT"):
            for name in self.tables[key].keys() - self.paths:
                raise OwnershipError(f"{path} {key} names untracked path {name}")

    def nearest(self, key, name):
        for directory in Path(name).parents:
            if str(directory) in self.tables[key]:
                return self.tables[key][str(directory)]
        raise OwnershipError(f"build/sources/ownership.mk: missing {key} for {name}")

    def lookup(self, name):
        if name not in self.paths:
            raise OwnershipError(f"untracked path {name}")
        kind, target = self.tables["OWN_FILE"].get(name) or self.nearest("OWN_DIRECTORY", name)
        permitted = self.nearest("OWN_PLACEMENT", name)
        if kind not in permitted:
            raise OwnershipError(f"misplaced {name}: {kind}, permitted {permitted}")
        for parent in Path(name).parents:
            placement = self.tables["OWN_LOCATION_TARGET"].get(str(parent))
            if placement and placement[0] == kind and placement[1] != target:
                raise OwnershipError(f"misplaced {name}: {kind} {target}, expected {placement[1]}")
        role = self.tables["OWN_HEADER"].get(name, ["ordinary" if name.endswith(".h") else None])[0]
        return dict(path=name, kind=kind, target=None if target == "-" else target,
                    header_role=role, public=name in self.public,
                    images=[dict(config=config, output=output)
                            for config, output in sorted(self.images.get(name, ()))],
                    images_complete=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision")
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args()
    inventory = Inventory(Path.cwd(), args.revision)
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from tools.ownership_images import image_dependencies
    inventory.images = image_dependencies(inventory)
    for path in args.paths or sorted(inventory.paths):
        print(json.dumps(inventory.lookup(path), ensure_ascii=False))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
