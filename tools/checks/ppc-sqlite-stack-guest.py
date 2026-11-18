# 期待の出所: PPC_SQLITE_STACK_DESIGN §6 / PPC_FP_NEWLIB_DESIGN §8 S1.
CHECK = dict(
    name='ppc-sqlite-stack-guest', tier=2, budget=600,
    needs=['make:sdk/include/os32/os32_generations.h'],
    env=['ppc-cross', 'qemu-ppc'],
    basis=['PPC_SQLITE_STACK_DESIGN §6', 'PPC_FP_NEWLIB_DESIGN §8 S1'],
    covers=[
        'tools/accept/ppc_sqlite_stack.py',
        'tools/tests/ppc_sqlite_stack_guest.c', 'tools/tests/ppc_sqlite_stack_regs.S',
        'tools/ppc/kboot_vectors.py', 'tools/ppc/run.py',
        # The input audit follows the complete vectors helper module.
        'tools/ppc/kboot_vectors_fixture.[cS]', 'platform/powermac/boot.c',
        'arch/ppc/cache_copy.S', 'arch/ppc/kernel_hpt.c', 'arch/ppc/kernel_trap.c',
        'arch/ppc/kernel_vectors.S', 'arch/ppc/pd_walk.c',
        'arch/ppc/entry.S', 'arch/ppc/ofw_note.S', 'arch/ppc/mmu.S',
        'arch/ppc/sqlite_stack_ppc.S', 'arch/ppc/kernel_fp.c',
        'arch/ppc/fp_context.S', 'arch/ppc/arch_kernel_fp.h',
        'arch/x86/arch_kernel_fp.h', 'include/exec_arch.h',
        'kernel/sqlite_engine.c', 'kernel/sqlite_engine.h',
        'lib/sqlite3/generated/sqlite_imports.h', 'lib/sqlite3/generated/sqlite_facade.h',
        'sdk/include/os32/os32_generations.h', 'sdk/gen_kapi.py',
        'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/kapi.json',
        'build/sources/common.mk', 'build/targets/ppc-powermac.mk',
        'platform/powermac/ofw.c', 'platform/powermac/escc_tree.c',
        'platform/powermac/escc.c', 'lib/kstring_c.c',
    ],
)


def run(ctx):
    import subprocess
    args = ['python3', '-B', 'tools/accept/ppc_sqlite_stack.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root)
