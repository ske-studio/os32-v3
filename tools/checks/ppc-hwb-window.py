"""PowerMac HW_OP design §5 5c/6a, page ownership and labeled transfer instructions."""
CHECK = dict(name='ppc-hwb-window', tier=2, needs=[], budget=10, env=['ppc-cross'],
    covers=['tools/checks/ppc_registered.py', 'tools/ppc/test_hwb_window.py',
            'tools/ppc/hwb_window_host.c', 'platform/powermac/hwb_window.*',
            'sys/platform/powermac/hwb_instructions.h',
            'tools/ppc/hwb_paging_host.c', 'sys/platform/powermac/platform_paging_window.h',
            'sys/arch/ppc/pd_walk.c', 'sys/arch/ppc/hwb_transfer.S', 'arch/ppc/ppc_*.h', 'include/**',
            'sys/kern/mm/paging.h', 'sys/kern/mm/physmem.h', 'sys/kern/kdebug/hwb_wire.h'],
    basis=['docs/decisions/realhw/HWOP_COMMON_DESIGN.md §2.4, §3'],
    full_only=True)

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_hwb_window.py', ['gcc', 'qemu-i386'])
