"""Ordered kernel include paths for host/cross tests and read-only tools.

build/config.mk owns the directory list; fixture overrides go before these flags.
"""
from functools import lru_cache
from pathlib import Path
import os
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]


@lru_cache(maxsize=None)
def include_dirs(config_root=ROOT):
    """Ask make to expand INC_KERNEL; build/config.mk owns order and defaults."""
    env = dict(os.environ)
    # This query is not a recursive build and must not inherit the jobserver,
    # -n/-j flags, or command-line overrides from the invoking test target.
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS'):
        env.pop(name, None)
    result = subprocess.run(
        ['make', '--no-print-directory', '-rRsf', '-', '__host_includes'],
        input='include build/config.mk\n.PHONY: __host_includes\n'
              '__host_includes:\n\t@$(info $(INC_KERNEL)):\n',
        cwd=config_root, env=env, text=True, capture_output=True, check=True)
    flags = shlex.split(result.stdout)
    if not flags or any(not flag.startswith('-I') or len(flag) == 2 for flag in flags):
        raise ValueError('INC_KERNEL must contain ordered -Idirectory flags')
    return tuple(flag[2:] for flag in flags)


def include_flags(root):
    """Rebase the build's paths onto the real tree or a copied fixture tree.

    Pass the caller's tree explicitly: this module may itself be symlinked.
    Put fixture and host_arch overrides before these flags at each call site.
    Do not fall back to ROOT for copied trees: that can bypass a mutation.
    """
    return ['-I' + str(Path(root) / directory) for directory in include_dirs()]
