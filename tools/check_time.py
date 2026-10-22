#!/usr/bin/env python3
"""Record one make check's elapsed wall time without capturing its output."""
import os
from pathlib import Path
import subprocess
import sys
import time


def reset(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'')


def run(path, name, command):
    start = time.monotonic()
    # Recursive make must inherit the jobserver descriptors.
    rc = subprocess.call(command, close_fds=False)
    elapsed = time.monotonic() - start
    row = f'{name}\t{elapsed:.6f}\t{rc}\n'.encode()
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o666)
        try:
            os.write(fd, row)  # One append per complete line, including under make -j.
        finally:
            os.close(fd)
    except OSError as error:
        print(f'check_time: {error}', file=sys.stderr)
    return rc if rc >= 0 else 128 - rc


if __name__ == '__main__':
    if sys.argv[1] == '--reset':
        reset(Path(sys.argv[2]))
    else:
        sys.exit(run(Path(sys.argv[1]), sys.argv[2], sys.argv[3:]))
