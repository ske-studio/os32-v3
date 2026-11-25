"""Read exec's moved fragments at their include sites for existing source tests.

Keep the existing extraction and mutation checks unchanged. The optional reader
uses the fixture's copied tree or mutated sources, never an unmutated fallback.
"""
import re

EXEC_PARTS = (
    'sys/kern/exec/exec_tramp_init.inc', 'sys/kern/exec/exec_tramp_strings.inc',
    'sys/kern/exec/exec_abort.inc', 'sys/kern/exec/exec_image_map.inc', 'sys/kern/exec/exec_finish.inc',
    'sys/kern/exec/exec_syscall.inc', 'sys/kern/exec/exec_stdin.inc', 'sys/kern/exec/exec_fault.inc', 'sys/kern/exec/exec_launch.inc',
    'sys/kern/exec/exec_schedule.inc',
)


def exec_source(root, read=None):
    if read is None:
        read = lambda path: (root / path).read_text()
    source = read('sys/kern/exec/exec.c')
    return re.sub(r'^#include "(exec_[^"\n]+\.inc)"\n',
                  lambda m: read('exec/' + m[1]) if 'exec/' + m[1] in EXEC_PARTS else m[0],
                  source, flags=re.M)


def read_text(path, **kwargs):
    """Resolve split exec sources when a fixture iterates over mixed inputs."""
    if path.as_posix().endswith('/exec/exec.c'):
        return exec_source(path.parent.parent)
    return path.read_text(**kwargs)


def exec_fixture_preamble():
    """Declarations for extracted exec bodies and their diagnostics."""
    return ('#include "exec_shlib.h"\n'
            '#include "exec_serial_diag.h"\n'
            '#include "exec_gfx_poll.h"\n'
            '#include "owner_diag.h"\n'
            # 手本は端末向けの出力 (console_printf) も、自分の kprintf の手本で受ける。
            '#define console_printf kprintf\n')
