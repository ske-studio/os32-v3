"""P3 typed absent/present bindings and PPC provider dependencies."""
CHECK = dict(name='ppc-kapi-binding', tier=1, needs=[], budget=10,
    covers=['tools/checks/ppc-kapi-binding.py', 'tools/ppc/test_kapi_binding.py',
            'sdk/kapi_target_ppc.py', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py',
            'sdk/target_profile.py', 'sdk/kapi.json', 'include/**', 'kapi/*.h',
            'kernel/**/*.h', 'exec/*.h', 'gfx/**/*.h', 'drivers/**/*.h',
            'fs/**/*.h', 'sdk/include/os32/*.h', 'kapi/kapi_generated.c',
            'sdk/crt/generations.inc', 'sdk/link/app.ld', 'sdk/link/app_sys.ld',
            'sdk/link/generations.ld', 'sdk/link/shlib.ld', 'sdk/os32_generations.py',
            'sdk/rust/os32api/src/generations.rs', 'sdk/rust/os32api/src/kapi_generated.rs',
            'sdk/kapi_rust_gen.py'], basis=['AC-NET-3', 'AC-CTX-1'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ppc/test_kapi_binding.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
