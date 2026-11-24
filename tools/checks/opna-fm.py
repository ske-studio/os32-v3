CHECK = dict(name='opna-fm', tier=1, needs=[], budget=10,
             covers=['lib/os32_opna/upstream/**', 'lib/os32_opna/src/operator.rs',
                     'lib/os32_opna/src/channel.rs', 'lib/os32_opna/src/state.rs',
                     'lib/os32_opna/src/tables.rs', 'tools/tests/test_opna_fm.py',
                     'tools/tests/opna_fm/**'],
             basis=['docs/decisions/sound/SYNTH_OPNA_DESIGN.md §9'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/tests/test_opna_fm.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
