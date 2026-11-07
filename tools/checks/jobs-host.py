#!/usr/bin/env python3
CHECK = dict(name='jobs-host', tier=1,
    covers=['tools/tests/test_jobs.py', 'tools/jobs.sh'],
    needs=[], budget=2.0)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_jobs.py'],
                           cwd=ctx.root, close_fds=False)
