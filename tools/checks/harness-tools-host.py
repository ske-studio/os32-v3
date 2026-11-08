#!/usr/bin/env python3
CHECK = dict(name='harness-tools-host', tier=1,
    covers=['tools/tests/test_harness_tools.py', 'tools/test_changes.py',
            'tools/check_docs_status.py', 'docs/POLICY_DEV.md',
            'docs/tasks/DEFERRED_TESTS.md'],
    needs=[], budget=2.9)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(
        ['python3', '-B', 'tools/tests/test_harness_tools.py'] +
        (['--mutate'] if ctx.mutate else []), cwd=ctx.root, close_fds=False)
