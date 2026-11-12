#!/usr/bin/env python3
CHECK = dict(name='usb-portable', tier=1, needs=[], budget=1,
    covers=['drivers/usb/*.[ch]', 'tools/check_usb_portable.py'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/check_usb_portable.py'],
                           cwd=ctx.root, close_fds=False)
