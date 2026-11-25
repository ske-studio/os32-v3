"""HW_BASIC_DESIGN §§3/7: real access/window/dispatch, libc-free ILP32."""
CHECK = dict(name='hwb-access-host', tier=1, covers=[
    'sys/kern/kdebug/hwb_access.c', 'sys/platform/pc98/platform_hwb.h', 'sys/include/pci_hwb_contract.h', 'sys/kern/kdebug/hwb_wire.h', 'sys/platform/pc98/kdebug/kdebug_main.c', 'sys/dev/video/trident/diag/tri_command_wire.h', 'sys/dev/video/trident/diag/trident_command_test.h',
    'sys/arch/x86/kdebug/kdebug_window.c', 'sys/arch/x86/kdebug/kdebug_window.h', 'sys/dev/video/geforce/diag/recover.h',
    'sys/dev/pci/pci_hwb.h', 'sys/kern/exec/exec_hwb.h', 'sys/include/io.h',
    'sys/include/types.h', 'sys/include/memmap.h', 'sys/kern/mm/paging.h',
    'tools/tests/hwb_access_host.c', 'tools/tests/hwb_fake.h',
    'tools/tests/hwb_host.py', 'tools/tests/kdebug_init_host.c',
    'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
    'tools/tests/mutpar.py', 'kdebug/generated/**',
    'sys/platform/pc98/platform_io.h', 'sys/arch/x86/arch_io.h',
    'sdk/include/os32/os32_kapi_shared.h'], needs=[], budget=5.0,
    basis=['HW_BASIC_DESIGN §3', 'HW_BASIC_DESIGN §7'],
)

import sys
import tempfile
from pathlib import Path
import hwb_host as host

FIXTURE = 'tools/tests/hwb_access_host.c'
FLAGS = ('-D__KERNEL_BUILD__', '-Wno-attributes')
# Move the real dispatch below the full PAUSED guard (design §7).
DISPATCH_PREFIX = '''static i32 recovery_call(u32 op, void *buf, u32 size)
{
#ifdef TICK_PROBE
    if (op == TP_OP) return tick_probe_call(buf, size);
#endif
    if (op == HW_OP) return hwb_access(buf, size);
    if (op == PROBE_OP) return probe_call(buf, size);
    struct recover *r = recover_owner();
    if ((op >= GF_OP_INSPECT && op <= GF_OP_ABORT) || op == GF_OP_MODESET ||
        op == GF_OP_C_ONLY || op == GF_OP_FOUNDATION || op == GF_OP_COMMAND) {
        const struct hwdev_ops *ops = hwdev_module_ops();
        if (!ops || recovery_attach()) return -1;
        int rc = ops->call(op, buf, size, &recovery_ops, &recovery_transport, &recovery_s0);
        recover_virtual_detach();
        return rc;
    }
    if (op == TRI_OP_COMMAND) return trident_command_call(buf, size);
    const KdebugRecoverVirtual *req = 0;
    if (op == KDEBUG_OP_RECOVER_VIRTUAL) {
        req = buf;
        if (!req || size != sizeof(*req) || req->version != RECOVER_VIRTUAL_VERSION)
            return KDEBUG_RECOVER_VIRTUAL_INVALID;
        for (u32 i = 0; i < sizeof(req->reserved) / sizeof(req->reserved[0]); i++)
            if (req->reserved[i]) return KDEBUG_RECOVER_VIRTUAL_INVALID;
    }
    if (r->state == REC_PAUSED && op != KDEBUG_OP_RECOVER_NOW &&
        op != KDEBUG_OP_RECOVER_RESTORE &&
        !(op == KDEBUG_OP_RECOVER_VIRTUAL && recover_virtual_allowed(req))) return -1;
'''
MUTATIONS = [
    ('block8-store', 'sys/platform/pc98/platform_hwb.h', 'cld; rep stosb', 'cld', 'block-narrow'),
    ('block16-store', 'sys/platform/pc98/platform_hwb.h', 'cld; rep stosw', 'cld', 'block-narrow'),
    ('block8-inc', 'sys/platform/pc98/platform_hwb.h', '1: stosb; incl %%eax;', '1: stosb; nop;', 'block-narrow'),
    ('block16-inc', 'sys/platform/pc98/platform_hwb.h', '1: stosw; incl %%eax;', '1: stosw; nop;', 'block-narrow'),
    ('block-fixed-count', 'sys/platform/pc98/platform_hwb.h', 'u32 count = block->count, value = req->value;',
     'u32 count = 1, value = req->value;', 'block32'),
    ('block-fixed-value', 'sys/platform/pc98/platform_hwb.h', '} else if (!block->increment) {',
     '} else if (1) {', 'block32'),
    ('block-alignment', 'sys/kern/kdebug/hwb_access.c', '(req->addr & (req->width - 1U)) || block->count',
     '0 || block->count', 'block-narrow'),

    ('paused-dispatch', 'sys/platform/pc98/kdebug/kdebug_main.c',
     DISPATCH_PREFIX,
     DISPATCH_PREFIX.replace('    if (op == HW_OP) return hwb_access(buf, size);\n', '') +
     '    if (op == HW_OP) return hwb_access(buf, size);\n',
     'paused'),
    ('cleanup', 'sys/platform/pc98/platform_hwb.h',
     'if (recover_window_cleanup(&window)) status = HW_WINDOW;\n    if (!write',
     'if (window.failed && recover_window_cleanup(&window)) status = HW_WINDOW;\n    if (!write',
     'window'),
    ('phys-write', 'sys/platform/pc98/platform_hwb.h',
     'write ? window.ops->store(window.opaque, va, req->width, value) :',
     'write ? 0 :', 'phys-write'),
    ('readback', 'sys/platform/pc98/platform_hwb.h',
     '        status = rc ? HW_IO_ERROR : HW_OK;',
     '        if (write) rc |= window.ops->load(window.opaque, va, req->width, &value);\n'
     '        status = rc ? HW_IO_ERROR : HW_OK;', 'write-only'),
]


def main():
    host.verify(FIXTURE, flags=FLAGS)
    # Real target TU, using precisely the build-owned kernel include order.
    with tempfile.TemporaryDirectory(prefix='hwb-access-target-') as directory:
        host.build(host.ROOT, 'sys/kern/kdebug/hwb_access.c', Path(directory) / 'access.o',
                   cross=True, flags=FLAGS)
    print('GREEN real hwb_access.c i386-elf -Werror', flush=True)
    if '--mutate' in sys.argv:
        return host.mutations(FIXTURE, MUTATIONS, flags=FLAGS)
    return 0


def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    sys.exit(main())
