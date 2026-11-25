#!/usr/bin/env python3
CHECK = dict(name='ppc-sh-input', tier=1, budget=60, needs=[],
    covers=['sys/platform/powermac/input_usb.h', 'tools/tests/host32.py', 'sys/platform/powermac/terminal_fifo.inc', 'sys/platform/powermac/terminal_input.c', 'sys/platform/powermac/input_boot.c', 'sys/platform/powermac/input_boot.h', 'sys/platform/powermac/input_decode.c', 'tools/tests/ppc_sh_input.py', 'sys/kern/input_legacy.c', 'sys/kern/input_wait.c', 'sys/hal/input/in2_int.h'],
    basis=['PPC_SHELL_DESIGN §2', 'AC-INPUT-1'],
    env_limit='hostの偽時計で端末入力とESC期限を確認するが、実入力装置とIRQ配送の遅延は再現しない。',
)

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/ppc_sh_input.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
