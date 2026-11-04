"""HW_BASIC_DESIGN §§2-4,7 executor integration and runtime mutations."""
CHECK = dict(name='hwb-exec-host', tier=1, budget=12.0, needs=[],
    covers=['userland/shell/hwb_exec.c', 'userland/shell/hwb_parse.c',
            'userland/shell/hwb_log.c', 'userland/shell/hwb.h',
            'kdebug/hwb_wire.h', 'include/types.h',
            'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/hwb_exec_host.c',
            'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py',
            'tools/tests/host32.py', 'tools/tests/mutpar.py',
            'tools/tests/kernel_includes.py'])
import sys
import hwb_host

FIXTURE = 'tools/tests/hwb_exec_host.c'
PRODUCT = 'userland/shell/hwb_exec.c'
MUTATIONS = [
    ('unregistered-address', PRODUCT,
     'if (s->backend->access(s->backend->ctx, &req)) return HWB_IO;',
     'if (addr == 0x34567000UL) return HWB_ARG;\n    if (s->backend->access(s->backend->ctx, &req)) return HWB_IO;', 'fresh-addresses'),
    ('automatic-readback', PRODUCT,
     'if (i->op == HWB_WRITE) { s->writes++; return HWB_OK; }',
     'if (i->op == HWB_WRITE) { s->writes++; he_access(s, HW_READ, i->space, i->width, r->bdf, r->addr, &a); return HWB_OK; }', 'write-only'),
    ('address-end-overflow', PRODUCT,
     'if (r->addr > limit || i->width - 1U > limit - r->addr) return HWB_ARG;',
     'if (r->addr > limit) return HWB_ARG;', 'boundary'),
    ('instruction-point', PRODUCT,
     'enum hwb_rc rc = he_point(s, 0); /* every instruction, including label/goto */',
     'enum hwb_rc rc = HWB_OK; /* removed instruction point */', 'instruction-point'),
    ('time-difference-32', PRODUCT,
     'unsigned long long elapsed_us = now - start;',
     'unsigned long long elapsed_us = (u32)(now - start);', 'clock-high-difference'),
    ('write-width', PRODUCT,
     'i->space, i->width, r->bdf, r->addr, &a);',
     'i->space, i->op == HWB_WRITE && i->width == 2 && r->addr == 4 ? 4 : i->width, r->bdf, r->addr, &a);', 'widths-rmw'),
    ('bar-cache', PRODUCT,
     'rc = he_bar(s, (u32)slot, bar, i->space, &base);',
     'rc = he_bar(s, (u32)slot, bar, i->space, &base); if (!rc && base == 0x600000UL) base = 0x500000UL;', 'bars'),
    ('fac-value', PRODUCT,
     'action == HW_WRITE ? *value : 0, 0};',
     'action == HW_WRITE ? (space == HW_IO && addr == 0xFACU ? (*value ^ 2U) : *value) : 0, 0};', 'trident-fac-order'),
    ('fac-enable-order', PRODUCT,
     'if (s->backend->access(s->backend->ctx, &req)) return HWB_IO;',
     'if (action == HW_WRITE && space == HW_IO && addr == 0xFACU && *value == 2U) return HWB_OK;\n'
     '    if (action == HW_WRITE && space == HW_IO && addr == 0x68U && *value == 0x0EU) {\n'
     '        struct hw_req early = {HW_VERSION, HW_WRITE, HW_IO, 1, 0, 0xFACU, 2, 0};\n'
     '        s->backend->access(s->backend->ctx, &early);\n'
     '    }\n'
     '    if (s->backend->access(s->backend->ctx, &req)) return HWB_IO;', 'trident-fac-order'),
    ('fac-disable-order', PRODUCT,
     'if (s->backend->access(s->backend->ctx, &req)) return HWB_IO;',
     'static int pending_fac;\n'
     '    if (action == HW_WRITE && space == HW_IO && addr == 0xFACU && *value == 0) { pending_fac = 1; return HWB_OK; }\n'
     '    if (s->backend->access(s->backend->ctx, &req)) return HWB_IO;\n'
     '    if (pending_fac && action == HW_WRITE) {\n'
     '        struct hw_req late = {HW_VERSION, HW_WRITE, HW_IO, 1, 0, 0xFACU, 0, 0};\n'
     '        s->backend->access(s->backend->ctx, &late); pending_fac = 0;\n'
     '    }', 'trident-fac-order'),
    ('poll-read-point', PRODUCT,
     'rc = he_point(s, 0); if (rc) return rc;\n            rc = he_address',
     'rc = HWB_OK; if (rc) return rc;\n            rc = he_address', 'instruction-point'),
]

def main():
    hwb_host.verify(FIXTURE)
    if '--mutate' in sys.argv:
        return int(bool(hwb_host.mutations(FIXTURE, MUTATIONS)))
    return 0

if __name__ == '__main__':
    sys.exit(main())

def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_hwb_exec.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)
