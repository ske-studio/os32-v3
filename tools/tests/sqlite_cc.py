"""Compile the SQLite amalgamation with optional ccache, without fixture caching."""
import os
import shutil
import subprocess


def build(argv, **kwargs):
    """Keep compiler inputs intact; OS32_NO_CCACHE=1 forces a direct build.

    Normalize paths under the invocation directory for copied repository trees.
    Keep default hash_dir and sloppiness; debug info still distinguishes CWDs.
    Current SQLite callers do not enable debug info or active __FILE__ uses.
    """
    cache = shutil.which('ccache')
    if os.environ.get('OS32_NO_CCACHE') != '1' and cache:
        base = os.path.abspath(kwargs.get('cwd') or os.getcwd())
        argv = [cache, 'compiler_check=content', 'base_dir=' + base, *argv]
    return subprocess.run(argv, **kwargs)
