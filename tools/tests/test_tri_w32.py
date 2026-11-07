"""Execute tri_w32 with byte-addressed LFB and injected store-width faults."""
CHECK = dict(name='tri-w32', tier=1, needs=[], budget=15, covers=[
    'userland/tests/hw/tri_w32.hwb', 'tools/tests/test_tri_w32.py',
    'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'userland/shell/hwb.h', 'userland/shell/hwb_parse.c',
    'userland/shell/hwb_exec.c', 'userland/shell/hwb_log.c',
    'kdebug/hwb_wire.h', 'include/types.h',
    'sdk/include/os32/os32_kapi_shared.h'])
import json
from pathlib import Path
import tempfile
import sys
import hwb_host

SCRIPT = 'userland/tests/hw/tri_w32.hwb'


def trial(mutation=None):
    script = (hwb_host.ROOT / SCRIPT).read_text()
    if mutation:
        _, before, after = mutation
        assert script.count(before) == 1
        script = script.replace(before, after)
    code = r'''
#include "hwb_fake.h"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/hwb_log.c"
static const char *script = SCRIPT_DATA;
static struct hwb_state state;
static struct hwb_program program;
static char source[HWB_FILE_BYTES];
static u8 memory[256];
static u32 fault, results, normal, lanes, reversed, units, unknown, wide;
static u32 phase, previous_write, gap;
static int has(const char *s,const char *p) {
    for(;*s;s++) {u32 n=0;while(p[n]&&p[n]==s[n])n++;if(!p[n])return 1;}return 0;
}
static int access(void *ctx,struct hw_req *r) {
    (void)ctx;r->status=HW_OK;
    if(r->space==HW_PCI) {r->value=r->addr==0x10?0x20000000:0x20400000;return 0;}
    if(r->space!=HW_PHYS || r->addr>=0x20400000) {r->value=0;return 0;}
    CHECK(r->addr>=0x20000000);
    u32 off=r->addr-0x20000000;
    if(r->action==HW_READ) {
        CHECK(r->width==1 && off<32);gap++;r->value=memory[off];return 0;
    }
    if(r->width>1) {
        if(previous_write && phase==3)CHECK(gap==0);
        if(previous_write && phase==4)CHECK(gap==1);
        previous_write=1;gap=0;
        wide++;
        if(fault==3)off/=r->width;
        if(fault==4)off*=r->width;
    }
    CHECK(off+r->width<=sizeof(memory));
    for(u32 k=0;k<r->width;k++) {
        if(fault==1 && r->width>1 && k!=0)continue;
        u32 lane=fault==2?r->width-1-k:k;
        memory[off+k]=r->value>>(lane*8);
    }
    return 0;
}
static void output(void *ctx,const char *s) {
    (void)ctx;
    if(has(s,"single32") || has(s,"control8") || has(s,"continuous32-") || has(s,"split16")) {
        phase++;previous_write=gap=0;
    }
    if(has(s,"result=")) {
        results++;
        normal+=has(s,"result=normal");lanes+=has(s,"result=lane-missing");
        reversed+=has(s,"result=reversed");units+=has(s,"result=address-unit");
        unknown+=has(s,"result=unknown");
    }
}
static int file(void *ctx,const char *path,char *buf,u32 cap,u32 *n) {
    (void)ctx;(void)path;*n=0;while(script[*n])(*n)++;CHECK(*n<=cap);
    for(u32 k=0;k<*n;k++)buf[k]=script[k];
    return 0;
}
static int now(void *ctx,u32 *lo,u32 *hi) {(void)ctx;*lo=0;*hi=0;return 0;}
static int point(void *ctx,u32 *flag) {(void)ctx;*flag=0;return 0;}
static struct hwb_backend backend={.access=access,.time_now=now,.point=point,.line=output,.read_file=file};
void _start(void) {
    HWB_TEST("tri-w32");
    for(fault=0;fault<5;fault++) {
        for(u32 k=0;k<sizeof(memory);k++)memory[k]=0x80+k;
        results=normal=lanes=reversed=units=unknown=wide=0;
        phase=previous_write=gap=0;
        hwb_init(&state,&backend);
        CHECK(hwb_run(&state,&program,source,sizeof(source),"script",0,0)==HWB_OK);
        CHECK(results==5 && wide==11 && !unknown);
        CHECK(normal==(fault?1U:5U));
        CHECK(lanes==(fault==1?4U:0U));
        CHECK(reversed==(fault==2?4U:0U));
        CHECK(units==(fault>=3?4U:0U));
        for(u32 k=0;k<32;k++)CHECK(memory[k]==0x80+k);
    }
    hwb_test_write("GREEN tri-w32: five trials, lane/order/address faults, byte restoration\n");
    hwb_test_exit(0);
}
'''.replace('SCRIPT_DATA', json.dumps(script))
    with tempfile.TemporaryDirectory(prefix='tri-w32-') as directory:
        fixture = Path(directory) / 'host.c'
        exe = Path(directory) / 'host'
        fixture.write_text(code)
        hwb_host.build(hwb_host.ROOT, fixture, exe)
        result = hwb_host.execute(exe)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:tri-w32:' not in result.stdout:
                raise RuntimeError(mutation[0] + ': expected runtime RED')
            print(mutation[0] + ': RED runtime')
        elif result.returncode:
            raise RuntimeError('tri-w32 control failed')
        else:
            hwb_host.build(hwb_host.ROOT, fixture, Path(directory) / 'target.o', cross=True)


def main():
    trial()
    if '--mutate' in sys.argv:
        for mutation in [
            ('single32-value', ' mmiowrite32 f + 4 0x04030201',
             ' mmiowrite32 f + 4 0x04030200'),
            ('byte-restore', ' mmiowrite8 f + i original[i]', ' mmiowrite8 f + i 0'),
            ('read-between', ' mmioread8 f + 4 -> t', ' log "read omitted"'),
        ]:
            trial(mutation)
    return 0


def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_tri_w32.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    sys.exit(main())
