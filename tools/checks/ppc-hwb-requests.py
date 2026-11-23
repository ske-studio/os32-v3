"""PowerMac HW_OP §5 stages 5b/7c, wire validation and port NOSYS."""
CHECK = dict(name='ppc-hwb-requests', tier=1, needs=[], budget=4, env=['ppc-cross'],
    covers=['tools/ppc/test_hwb_requests.py', 'tools/ppc/hwb_requests_host.c', 'tools/ppc/hwb_fill_host.c',
            'tools/tests/hwb_fake.h', 'userland/shell/hwb*', 'kdebug/probe_wire.h',
            'platform/powermac/platform_hwb*.h', 'platform/powermac/diag_slots.h',
            'platform/powermac/hwb_resource.[ch]', 'platform/powermac/*.h', 'arch/ppc/*.h',
            'drivers/net/gem/gem.h', 'drivers/pci/pci_hwb.[ch]', 'kdebug/hwb_access.c', 'kdebug/hwb_wire.h',
            'include/**', 'kernel/module/*.h', 'sdk/include/os32/*.h',
            'tools/checks/ppc_registered.py'],
    basis=['docs/decisions/realhw/HWOP_COMMON_DESIGN.md §2.4',
           'docs/decisions/ppc/PPC_HWOP_BACKEND_DESIGN.md §5'])

def run(ctx):
    from ppc_registered import run_test
    return run_test(ctx, 'tools/ppc/test_hwb_requests.py', ['gcc', 'qemu-i386'])
