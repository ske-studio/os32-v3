"""Check the P0-5 inventory on tracked source files and compiled objects."""
import subprocess

CHECK = dict(name='restructure-p0-5', tier=1, needs=['make:all'], budget=12,
             covers=['tools/move_files.py', 'tools/restructure/path_refs.py',
                     'tools/restructure/artifact_paths.py', 'tools/restructure/test_path_refs.py',
                     'docs/tasks/restructure/P3.md', 'sys/hal/gfx/v2/format.c',
                     'build/out/kernel.elf', 'build/os32.ld', 'sys/kapi/kapi_generated.c',
                     'sdk/link/*.ld', 'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs'],
             basis=['docs/tasks/restructure/P0.md P0-5'])


def run(ctx):
    cmd = ['python3', '-B', 'tools/restructure/test_path_refs.py']
    if ctx.mutate:
        cmd.append('--mutate')
    return subprocess.call(cmd, cwd=ctx.root)
