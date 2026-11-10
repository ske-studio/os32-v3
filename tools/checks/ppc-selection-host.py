#!/usr/bin/env python3
CHECK = dict(name='ppc-selection-host', tier=1,
    covers=['sdk/target_profile.py', 'sdk/target_profile_cli.py', 'tools/ppc/check_compile.py', 'tools/ppc/check_accept.py', 'tools/ppc/selected_success.json', 'tools/ppc/known_header_dependencies.json', 'tools/build_compare.py', 'arch/ppc/entry.S', 'arch/ppc/ofw_note.S', 'lib/kstring_c.c', 'tools/build_profile.py', 'tools/gen_build_id.py', 'Makefile', 'build/**/*.mk', 'tools/tests/test_ppc_selection.py'], needs=[], budget=2.9)


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_ppc_selection.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
