"""Shared make-query fixture inputs; no compilers or generated products."""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
CONFIG_INPUTS = ('sdk/target_profile.py', 'sdk/target_profile_cli.py')


def copy_build_tree(root):
    shutil.copytree(ROOT / 'build', root / 'build',
                    ignore=shutil.ignore_patterns('out', 'sdk', '__pycache__'))
    # Kernel source lists use wildcards for these directories.
    for name in ('gfx/v2', 'gfx/sync'):
        shutil.copytree(ROOT / name, root / name)
    for name in CONFIG_INPUTS:
        source = ROOT / name
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
