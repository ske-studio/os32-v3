# 期待の出所: PPC_SQLITE_STACK_DESIGN §6 / PPC_FP_NEWLIB_DESIGN §8 S1.
CHECK = dict(
    name='ppc-sqlite-stack-guest', tier=2, budget=600,
    needs=['make:sdk/include/os32/os32_generations.h'],
    env=['ppc-cross', 'qemu-ppc'],
    basis=['PPC_SQLITE_STACK_DESIGN §6', 'PPC_SQLITE_STACK_DESIGN 追記 A-2・A-4',
           'PPC_FP_NEWLIB_DESIGN §8 S1'],
    covers=[
        'tools/accept/ppc_sqlite_stack.py',
        'tools/tests/ppc_sqlite_stack_guest.c', 'tools/tests/ppc_sqlite_stack_regs.S',
        'tools/ppc/kboot_vectors.py', 'tools/ppc/run.py',
        # The input audit follows the complete vectors helper module.
        'tools/ppc/kboot_vectors_fixture.[cS]', 'sys/platform/powermac/boot.c',
        'sys/arch/ppc/kernel_hpt.c', 'sys/arch/ppc/kernel_trap.c',
        'sys/arch/ppc/kernel_vectors.S', 'sys/arch/ppc/pd_walk.c',
        'sys/arch/ppc/entry.S', 'sys/arch/ppc/ofw_note.S', 'sys/arch/ppc/mmu.S',
        'sys/arch/ppc/cache_copy.S', 'sys/arch/ppc/ppc_frame.h', 'sys/arch/ppc/ppc_oea.h',
        'sys/arch/ppc/sqlite_stack_ppc.S', 'sys/arch/ppc/kernel_fp.c',
        'sys/arch/ppc/fp_context.S', 'sys/arch/ppc/arch_kernel_fp.h',
        'sys/arch/x86/arch_kernel_fp.h', 'sys/include/exec_arch.h',
        'sys/kern/sqlite_engine.c', 'sys/kern/sqlite_engine.h', 'sys/kern/sqlite_engine_body.h',
        'lib/sqlite3/generated/sqlite_imports.h', 'lib/sqlite3/generated/sqlite_facade.h',
        'sdk/include/os32/os32_generations.h', 'sdk/gen_kapi.py',
        'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/kapi.json',
        'build/sources/common.mk', 'build/targets/ppc-powermac.mk',
        'sys/platform/powermac/ofw.c', 'sys/platform/powermac/escc_tree.c',
        'sys/platform/powermac/escc.c', 'sys/kern/kstring_c.c',
    ],
)


def run(ctx):
    import subprocess
    args = ['python3', '-B', 'tools/accept/ppc_sqlite_stack.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root)
