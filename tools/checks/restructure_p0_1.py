CHECK = dict(name='restructure-p0-1', tier=1, needs=['make:build/out/kernel.elf'], budget=12,
             covers=['tools/restructure/*.py', 'tools/check_layers.py', 'tools/layers_baseline.json',
                     'fs/ext2/ext2_dir.c', 'userland/shell/main.c', 'kapi/kapi_generated.c', 'build/out/kernel.map', 'build/**/*.mk',
                     'build/*.mk', 'sdk/target_profile*.py', 'kernel/module/kdebug_imports.def'],
             basis=['docs/tasks/restructure/P0.md P0-1'])


def run(ctx):
    import subprocess
    args = ['python3', '-B', 'tools/restructure/test_owners.py']
    return subprocess.call(args + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
