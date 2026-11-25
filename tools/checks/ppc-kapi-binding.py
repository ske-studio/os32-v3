"""P3 typed absent/present bindings and PPC provider dependencies.

期待の出所: PowerPC P3 の確定設計 §3 (host・notice の provider を PPC で実体化) と AC-CTX-1 (halt/yield の endpoint)。
"""
CHECK = dict(env=['ppc-cross'], name='ppc-kapi-binding', tier=1, needs=[], budget=10,
    covers=['tools/checks/ppc-kapi-binding.py', 'tools/ppc/test_kapi_binding.py', 'tools/checks/av_wiring.txt',
            'sdk/kapi_target_ppc.py', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py',
            'sdk/target_profile.py', 'sdk/kapi.json', 'include/**', 'kapi/*.h',
            'kernel/**/*.h', 'mm/*.h', 'exec/*.h', 'gfx/**/*.h', 'drivers/**/*.h',
            'sys/hal/gfx/gfx_present.c', 'sys/platform/powermac/display_provider.c',
            'fs/**/*.h', 'sdk/include/os32/*.h', 'sys/kapi/kapi_generated.c',
            'sdk/crt/generations.inc', 'sdk/link/app.ld', 'sdk/link/app_sys.ld',
            'sdk/link/generations.ld', 'sdk/link/shlib.ld', 'sdk/os32_generations.py',
            'sdk/rust/os32api/src/generations.rs', 'sdk/rust/os32api/src/kapi_generated.rs',
            'sdk/kapi_rust_gen.py'], basis=['AC-CTX-1', 'docs/09_exec.md Ring3 の USER 写像 (PowerMac SHM)'])


def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', 'tools/ppc/test_kapi_binding.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
