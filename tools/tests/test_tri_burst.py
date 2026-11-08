"""Run tri_burst on an indexed CR39/byte LFB bus, including dropped bursts."""
CHECK = dict(name='tri-burst', tier=1, needs=[], budget=141.7, covers=[
    'userland/tests/hw/tri_burst.hwb', 'userland/tests/hw/tri_burst2.hwb', 'tools/tests/test_tri_burst.py',
    'tools/tests/tri_burst2_host.c', 'tools/tests/tri_burst3_host.c', 'tools/tests/tri_burst4_host.c',
    'userland/tests/hw/tri_burst4.hwb', 'userland/tests/hw/tri_burst5.hwb', 'tools/tests/tri_burst5_host.c',
    'userland/tests/hw/tri_burst3.hwb', 'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'userland/shell/hwb.h', 'userland/shell/hwb_probe.c', 'userland/shell/hwb_parse.c',
    'userland/shell/hwb_exec.c', 'userland/shell/hwb_log.c',
    'kdebug/probe_wire.h', 'kdebug/hwb_wire.h', 'include/types.h',
    'sdk/include/os32/os32_kapi_shared.h'])
import json
from pathlib import Path
import tempfile
import sys
import hwb_host

SCRIPT = 'userland/tests/hw/tri_burst.hwb'


def trial(mutation=None):
    script = (hwb_host.ROOT / SCRIPT).read_text(encoding='ascii')
    if mutation:
        _, before, after = mutation
        assert script.count(before) == 1
        script = script.replace(before, after)
    code = r'''
#include "hwb_fake.h"
#include "userland/shell/hwb_parse.c"
#include "userland/shell/hwb_probe.c"
#include "userland/shell/hwb_exec.c"
#include "userland/shell/hwb_log.c"
static const char *script = SCRIPT_DATA;
static struct hwb_state state;
static struct hwb_program program;
static char source[HWB_FILE_BYTES];
static u8 memory[4096], cr[256];
static u32 fault, ci, blocks, matches, misses, counts, before, after, restored, disabled;
static u32 reads, trials, logs, events;
static const u32 settings[4]={0xD7,0xD5,0xD3,0xD1};
static int has(const char *s,const char *p) {
    for(;*s;s++) {u32 n=0;while(p[n]&&p[n]==s[n])n++;if(!p[n])return 1;}return 0;
}
static u32 value(const char *s) {
    while(*s && !(s[0]=='v' && s[1]=='a' && s[2]=='l' && s[3]=='='))s++;
    CHECK(*s);s+=4;CHECK(s[0]=='0' && s[1]=='x');s+=2;u32 v=0;
    while(*s && *s!=' ') {v=(v<<4)|(*s<='9'?*s-'0':*s-'A'+10);s++;}return v;
}
static int drops(void) {return (cr[0x39]&fault)!=0;}
static int access(void *ctx,struct hw_req *r) {
    (void)ctx;events++;r->status=HW_OK;
    if(r->space==HW_PCI) {r->value=r->addr==0x10?0x20000000:0x20400000;return 0;}
    if(r->space==HW_IO) {if(r->addr==0xFAC && r->value==0)disabled++;return 0;}
    CHECK(r->space==HW_PHYS);
    if(r->addr>=0x20400000) {
        u32 a=r->addr-0x20400000;
        if(r->action==HW_READ) {r->value=(a==0x3D5 || a==0x3B5)?cr[ci]:0;return 0;}
        for(u32 k=0;k<r->width;k++) {
            u32 v=(r->value>>(k*8))&255;
            if(a+k==0x3D4 || a+k==0x3B4)ci=v;
            if(a+k==0x3D5 || a+k==0x3B5)cr[ci]=v;
        }
        return 0;
    }
    CHECK(r->addr>=0x20000000);
    u32 off=r->addr-0x20000000;
    if(r->action==HW_BLOCK32) {
        struct hw_block_req *b=(struct hw_block_req *)r;
        CHECK(off==0 && r->width==4 && b->count==1024 && blocks<8);
        CHECK(cr[0x39]==settings[blocks/2]);
        CHECK(b->increment==(blocks&1));
        CHECK(r->value==((blocks&1)?(0x5A000000U+(blocks/2)*0x10000U):0xA5000000U));
        if(blocks&1)CHECK(reads==0);
        reads=0;
        for(u32 n=0;n<1024;n++) {
            if(drops() && n>1 && n!=32 && n!=33 && n!=512 && n!=513)continue;
            u32 v=r->value+(b->increment?n:0);
            for(u32 k=0;k<4;k++)memory[n*4+k]=v>>(k*8);
        }
        blocks++;return 0;
    }
    CHECK(off+r->width<=sizeof(memory));
    if(r->action==HW_READ) {
        reads++;r->value=0;
        for(u32 k=0;k<r->width;k++)r->value|=(u32)memory[off+k]<<(k*8);
    } else {
        CHECK(blocks==8);restored++;
        for(u32 k=0;k<r->width;k++)memory[off+k]=r->value>>(k*8);
    }
    return 0;
}
static void output(void *ctx,const char *s) {
    (void)ctx;logs++;
    if(has(s,"CR39-restored")) CHECK(value(s)==0xD7);
    if(has(s,"CR39-before")) {CHECK(value(s)==settings[before]);before++;}
    if(has(s,"CR39-after")) {CHECK(value(s)==settings[after]);after++;}
    if(has(s,"tag=\"match-offset\"")) {
        static const u32 surviving[6]={0,4,0x80,0x84,0x800,0x804};
        CHECK(value(s)==(drops() ? surviving[matches%6] : matches*4));matches++;
    }
    if(has(s,"tag=\"mismatch-offset\"")) {CHECK(value(s)==(misses+2)*4);misses++;}
    if(has(s,"tag=\"mismatch-count\"")) {
        u32 bad=(settings[trials]&fault)?1018:0;
        CHECK(value(s)==bad && matches==(bad?6U:1024U) && misses==(bad?8U:0U));
        matches=misses=0;trials++;counts++;
    }
}
static int file(void *ctx,const char *path,char *buf,u32 cap,u32 *n) {
    (void)ctx;(void)path;*n=0;while(script[*n])(*n)++;CHECK(*n<=cap);
    for(u32 k=0;k<*n;k++)buf[k]=script[k];
    return 0;
}
static int point(void *ctx,u32 *flag) {(void)ctx;events++;*flag=0;return 0;}
static struct hwb_backend backend={.access=access,.point=point,.line=output,.read_file=file};
void _start(void) {
    HWB_TEST("tri-burst");
    for(fault=0;fault<=6;fault+=2) {
        for(u32 n=0;n<4096;n++)memory[n]=(u8)(n*13+7);
        hwbf_zero(cr,sizeof(cr));cr[0x39]=0xD1;
        ci=blocks=matches=misses=counts=before=after=restored=disabled=reads=trials=logs=events=0;
        hwb_init(&state,&backend);
        CHECK(hwb_run(&state,&program,source,sizeof(source),"script",0,0)==HWB_OK);
        CHECK(blocks==8 && counts==4 && before==4 && after==4);
        CHECK(restored==1024 && disabled==1 && cr[0x39]==0xD1);
        for(u32 n=0;n<4096;n++)CHECK(memory[n]==(u8)(n*13+7));
        CHECK(logs<131072 && logs+events<524288);
    }
    hwb_test_write("GREEN tri-burst: four CR39 settings, dropped bursts, offsets/counts, restoration\n");
    hwb_test_exit(0);
}
'''.replace('SCRIPT_DATA', json.dumps(script))
    with tempfile.TemporaryDirectory(prefix='tri-burst-') as directory:
        fixture = Path(directory) / 'host.c'
        exe = Path(directory) / 'host'
        fixture.write_text(code)
        hwb_host.build(hwb_host.ROOT, fixture, exe)
        result = hwb_host.execute(exe)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:tri-burst:' not in result.stdout:
                raise RuntimeError(mutation[0] + ': expected runtime RED')
            print(mutation[0] + ': RED runtime')
        elif result.returncode:
            raise RuntimeError('tri-burst control failed')
        else:
            hwb_host.build(hwb_host.ROOT, fixture, Path(directory) / 'target.o', cross=True)


def trial2(mutation=None):
    script = (hwb_host.ROOT / 'userland/tests/hw/tri_burst2.hwb').read_text(encoding='ascii')
    if mutation:
        name, before, after = mutation
        assert script.count(before) == 1, name
        script = script.replace(before, after)
    code = (hwb_host.ROOT / 'tools/tests/tri_burst2_host.c').read_text().replace('SCRIPT_DATA', json.dumps(script))
    with tempfile.TemporaryDirectory(prefix='tri-burst2-') as directory:
        fixture = Path(directory) / 'host.c'
        exe = Path(directory) / 'host'
        fixture.write_text(code)
        hwb_host.build(hwb_host.ROOT, fixture, exe)
        result = hwb_host.execute(exe)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:tri-burst2:' not in result.stdout:
                raise RuntimeError(name + ': expected runtime RED')
            print(name + ': RED runtime')
        elif result.returncode:
            raise RuntimeError('tri-burst2 control failed')
        else:
            hwb_host.build(hwb_host.ROOT, fixture, Path(directory) / 'target.o', cross=True)


def trial3(mutation=None):
    script = (hwb_host.ROOT / 'userland/tests/hw/tri_burst3.hwb').read_text(encoding='ascii')
    if mutation:
        name, before, after = mutation
        assert script.count(before) == 1, name
        script = script.replace(before, after)
    code = (hwb_host.ROOT / 'tools/tests/tri_burst3_host.c').read_text().replace('SCRIPT_DATA', json.dumps(script))
    with tempfile.TemporaryDirectory(prefix='tri-burst3-') as directory:
        fixture = Path(directory) / 'host.c'
        exe = Path(directory) / 'host'
        fixture.write_text(code)
        hwb_host.build(hwb_host.ROOT, fixture, exe)
        result = hwb_host.execute(exe)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:tri-burst3:' not in result.stdout:
                raise RuntimeError(name + ': expected runtime RED')
            print(name + ': RED runtime')
        elif result.returncode:
            raise RuntimeError('tri-burst3 control failed')
        else:
            hwb_host.build(hwb_host.ROOT, fixture, Path(directory) / 'target.o', cross=True)


def trial4(mutation=None):
    script = (hwb_host.ROOT / 'userland/tests/hw/tri_burst4.hwb').read_text(encoding='ascii')
    if mutation:
        name, before, after = mutation
        assert script.count(before) == 1, name
        script = script.replace(before, after)
    code = (hwb_host.ROOT / 'tools/tests/tri_burst4_host.c').read_text().replace('SCRIPT_DATA', json.dumps(script))
    with tempfile.TemporaryDirectory(prefix='tri-burst4-') as directory:
        fixture = Path(directory) / 'host.c'
        exe = Path(directory) / 'host'
        fixture.write_text(code)
        hwb_host.build(hwb_host.ROOT, fixture, exe)
        # Eight complete matrix runs exceed the usual one-script 60s budget.
        result = hwb_host.host32.run([str(exe)], cwd=hwb_host.ROOT,
                                     capture_output=True, text=True, timeout=240)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:tri-burst4:' not in result.stdout:
                raise RuntimeError(name + ': expected runtime RED')
            print(name + ': RED runtime')
        elif result.returncode:
            raise RuntimeError('tri-burst4 control failed')
        else:
            hwb_host.build(hwb_host.ROOT, fixture, Path(directory) / 'target.o', cross=True)

def trial5(mutation=None):
    script = (hwb_host.ROOT / 'userland/tests/hw/tri_burst5.hwb').read_text(encoding='ascii')
    if mutation:
        name, before, after = mutation
        assert script.count(before) == 1, name
        script = script.replace(before, after)
    code = (hwb_host.ROOT / 'tools/tests/tri_burst5_host.c').read_text().replace('SCRIPT_DATA', json.dumps(script))
    with tempfile.TemporaryDirectory(prefix='tri-burst5-') as directory:
        fixture = Path(directory) / 'host.c'
        exe = Path(directory) / 'host'
        fixture.write_text(code)
        hwb_host.build(hwb_host.ROOT, fixture, exe)
        # Four complete pacing matrices exceed the usual one-script 60s budget.
        result = hwb_host.host32.run([str(exe)], cwd=hwb_host.ROOT,
                                     capture_output=True, text=True, timeout=240)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:tri-burst5:' not in result.stdout:
                raise RuntimeError(name + ': expected runtime RED')
            print(name + ': RED runtime')
        elif result.returncode:
            raise RuntimeError('tri-burst5 control failed')
        else:
            hwb_host.build(hwb_host.ROOT, fixture, Path(directory) / 'target.o', cross=True)


def main():
    trial()
    trial2()
    trial3()
    trial4()
    trial5()
    if '--mutate' in sys.argv:
        for mutation in [
            ('burst-bit1', 'let trialcr = burstcr & 0xFD', 'let trialcr = burstcr & 0xFB'),
            ('burst-b-value', '1024 seed inc', '1024 (seed+1) inc'),
            ('burst-restore', 'mmiowrite32 f + (i << 2) original[i]', 'mmiowrite32 f + (i << 2) 0'),
            ('burst-count', 'print "mismatch-count" bad', 'print "mismatch-count" 0'),
            ('burst-match', 'print "match-offset" off', 'print "match-offset" 0'),
            ('burst-cr39-restore', '0x39 | (burstcr << 8)', '0x39 | (0 << 8)'),
        ]:
            trial(mutation)
        for mutation in [
            ('repeat-seed', 'let seed = 0x5B000000', 'let seed = 0x5A000000'),
            ('width8', 'mmioblock8 f + 0 count seed', 'mmioblock16 f + 0 count seed'),
            ('width16', 'mmioblock16 f + 0 count seed', 'mmioblock8 f + 0 count seed'),
            ('short-length', 'let count = 64', 'let count = 128'),
            ('range-start', 'print "match-start" off', 'print "match-start" 1'),
            ('range-end', 'print "match-end" (off - 1)\n let inrange = 0', 'print "match-end" 0\n let inrange = 0'),
            ('range-final', 'print "match-end" (off - 1)\n label compared', 'print "match-end" 0\n label compared'),
            ('range-count', 'print "mismatch-count" bad', 'print "mismatch-count" 0'),
            ('restore-vram', 'mmiowrite32 f + (i << 2) original[i]', 'mmiowrite32 f + (i << 2) 0'),
            ('restore-cr55', '0x55 | ((save[841]) << 8)', '0x55 | (0 << 8)'),
            ('cr07', 'let trialcr = (burstcr & 0xF8) | 7', 'let trialcr = (burstcr & 0xF8) | 1'),
        ]:
            trial2(mutation)
        for mutation in [
            ('interface-bit', '0x2A | ((t | 0x40) << 8)', '0x2A | ((t | 0x00) << 8)'),
            ('performance-bit', '0x2F | ((t | 0x10) << 8)', '0x2F | ((t | 0x00) << 8)'),
            ('ge-wait', 'if (status & 0x80) == 0 goto ge_idle', 'if (status & 0x80) != 0 goto ge_idle'),
            ('ge-bound', 'if polls != 10000 goto ge_wait', 'if polls != 9999 goto ge_wait'),
            ('ge-init', 'mmiowrite16 v + 0x2122 0x100', 'mmiowrite16 v + 0x2122 0'),
            ('sr-unlock', '((t ^ 2) | 0x80)', '((t ^ 2) | 0x00)'),
            ('readback', 'print "CR39-before" t', 'print "CR39-before" trialcr'),
            ('observe', 'call observe', 'log "observe omitted"'),
            ('range', 'print "match-start" off', 'print "match-start" 1'),
            ('misses', 'print "mismatch-count" bad', 'print "mismatch-count" 0'),
            ('vram-restore', 'mmiowrite32 f + (i << 2) original[i]', 'mmiowrite32 f + (i << 2) 0'),
            ('cr23-restore', '0x23 | ((save[852]) << 8)', '0x23 | (0 << 8)'),
            ('cr55-restore', '0x55 | ((save[841]) << 8)', '0x55 | (0 << 8)'),
            ('ge-restore', 'mmiowrite16 v + 0x2122 ge_saved', 'mmiowrite16 v + 0x2122 0'),
        ]:
            trial3(mutation)
        for mutation in [
            ('matrix-pixel16', 'let pixelbus = 5', 'let pixelbus = 0'),
            ('matrix-pitch32', 'let offset = 0x140', 'let offset = 0x40'),
            ('matrix-dac16', 'let dacval = 0x30', 'let dacval = 0'),
            ('matrix-gr32', 'let ext = 0x0A', 'let ext = 2'),
            ('matrix-pll-ntsc', 'let plla = 0x77', 'let plla = a'),
            ('matrix-pll-pal', 'let plla = 0x75', 'let plla = a'),
            ('matrix-cr39', 'let trialcr = 1', 'let trialcr = 7'),
            ('matrix-width8', 'mmioblock8 f + 0 count seed', 'mmioblock16 f + 0 count seed'),
            ('matrix-width16', 'mmioblock16 f + 0 count seed', 'mmioblock8 f + 0 count seed'),
            ('matrix-length', 'let count = 4096', 'let count = 2048'),
            ('matrix-seed', 'let seed = (0x41 + delta) & 0xFF', 'let seed = 0x41'),
            ('matrix-range', 'print "match-start" off', 'print "match-start" 1'),
            ('matrix-count', 'print "mismatch-count" bad', 'print "mismatch-count" 0'),
            ('matrix-read16', 'mmioread16 f + off -> got', 'mmioread8 f + off -> got'),
            ('matrix-cleanup', 'let depth = 8\n call set_depth\n log "cleanup-8bpp"', 'let depth = 32\n call set_depth\n log "cleanup-8bpp"'),
            ('matrix-vram-restore', 'mmiowrite32 f + (i << 2) original[i]', 'mmiowrite32 f + (i << 2) 0'),
            ('matrix-cr23-restore', '0x23 | ((save[852]) << 8)', '0x23 | (0 << 8)'),
        ]:
            trial4(mutation)
        for mutation in [
            ('pacing-variant', 'if variant != 4 goto variant_loop', 'if variant != 3 goto variant_loop'),
            ('pacing-read-lfb', 'mmioread16 f + off -> discard', 'mmioread8 f + off -> discard'),
            ('pacing-read-status', 'mmioread8 v + 0x3DA -> discard', 'mmioread8 v + 0x3CC -> discard'),
            ('pacing-store8', 'mmiowrite8 f + off data', 'mmiowrite16 f + off data'),
            ('pacing-store16', 'mmiowrite16 f + off data', 'mmiowrite8 f + off data'),
            ('pacing-store32', 'mmiowrite32 f + off data', 'mmiowrite16 f + off data'),
            ('pacing-depth', 'call set_depth\n print "depth-bpp" depth', 'log "depth omitted"\n print "depth-bpp" depth'),
            ('pacing-seed', 'let delta = delta + 0x01010101\n return', 'let delta = delta + 0x02020202\n return'),
            ('pacing-count', 'print "mismatch-count" bad', 'print "mismatch-count" 0'),
            ('pacing-range', 'print "match-start" off', 'print "match-start" 1'),
            ('pacing-vram', 'mmiowrite32 f + (i << 2) original[i]', 'mmiowrite32 f + (i << 2) 0'),
            ('pacing-cr23', '0x23 | ((save[852]) << 8)', '0x23 | (0 << 8)'),
        ]:
            trial5(mutation)
    return 0


def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)


if __name__ == '__main__':
    sys.exit(main())
