"""Read exec's moved fragments at their include sites for existing source tests.

Keep the existing extraction and mutation checks unchanged. The optional reader
uses the fixture's copied tree or mutated sources, never an unmutated fallback.
"""
import re

EXEC_PARTS = (
    'exec/exec_tramp_init.inc', 'exec/exec_tramp_strings.inc',
    'exec/exec_abort.inc', 'exec/exec_image_map.inc', 'exec/exec_finish.inc',
    'exec/exec_syscall.inc', 'exec/exec_stdin.inc', 'exec/exec_fault.inc', 'exec/exec_launch.inc',
    'exec/exec_schedule.inc',
)


def exec_source(root, read=None):
    if read is None:
        read = lambda path: (root / path).read_text()
    source = read('exec/exec.c')
    return re.sub(r'^#include "(exec_[^"\n]+\.inc)"\n',
                  lambda m: read('exec/' + m[1]) if 'exec/' + m[1] in EXEC_PARTS else m[0],
                  source, flags=re.M)


def read_text(path, **kwargs):
    """Resolve split exec sources when a fixture iterates over mixed inputs."""
    if path.as_posix().endswith('/exec/exec.c'):
        return exec_source(path.parent.parent)
    return path.read_text(**kwargs)
