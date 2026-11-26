"""Read the candidate index, including entries whose contents are intentionally excluded."""
import json
from pathlib import Path
import subprocess


def paths(root):
    index = root / ".check-index.json"
    if index.is_file() and not (root / ".git").exists():
        return set(json.loads(index.read_text()))
    names = subprocess.check_output(["git", "ls-files", "-z"], cwd=root)
    return set(names.decode().split("\0")[:-1])


def copy_inputs(root, destination, names):
    """Materialize tracked files for read-only product queries without following gitlinks."""
    import shutil
    for name in names:
        if name == ".env" or name.startswith(("docs/archive/", "docs/hw/")):
            continue
        path = root / name
        if path.is_symlink():
            # The target may be outside the candidate. Only copy internal links.
            if not path.resolve().is_relative_to(root.resolve()):
                continue
        elif not path.is_file():
            continue
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target, follow_symlinks=False)
