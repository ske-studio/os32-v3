CHECK = dict(name='opna-tables', tier=1, needs=[], budget=4,
             covers=['lib/os32_opna/upstream/**', 'lib/os32_opna/src/tables.rs',
                     'lib/os32_opna/tables.manifest', 'tools/gen_opna_tables.*'],
             basis=['docs/decisions/sound/SYNTH_OPNA_DESIGN.md §4'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/gen_opna_tables.py', '--check',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
