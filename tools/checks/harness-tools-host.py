#!/usr/bin/env python3
CHECK = dict(name='harness-tools-host', tier=1,
    covers=['tools/docs_status_exceptions.json', 'tools/tests/test_harness_tools.py', 'tools/test_changes.py',
            'tools/check_docs_status.py', 'docs/POLICY_DEV.md',
            'docs/tasks/DEFERRED_TESTS.md'],
    needs=[], budget=2.9,
    basis=['POLICY_DEV §8'],
    full_only=True,
)


def run(ctx) -> int:
    import subprocess
    return subprocess.call(
        ['python3', '-B', 'tools/tests/test_harness_tools.py'] +
        (['--mutate'] if ctx.mutate else []), cwd=ctx.root, close_fds=False)
