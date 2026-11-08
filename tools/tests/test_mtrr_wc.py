#!/usr/bin/env python3
"""MTRR WC lease: compile the real C with fake CPU primitives, run as ILP32."""
CHECK = dict(name='mtrr-wc-host', tier=1,
    covers=['arch/x86/kernel/mtrr_wc.c', 'arch/x86/kernel/mtrr_wc.h',
            'include/types.h', 'tools/tests/host32.py'],
    needs=[], budget=3.4)

from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'arch/x86/kernel/mtrr_wc.c'
HEADER = ROOT / 'arch/x86/kernel/mtrr_wc.h'
FLAGS = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
         '-nostdlib', '-static', '-no-pie', '-I' + str(ROOT / 'include')]

# Only instruction wrappers are replaced. ID-bit detection, capability checks,
# range selection, transactions and rollback all remain the production code.
FAKE = r'''
struct event { char op; u32 key; unsigned long long value; };
static struct event events[512];
static volatile u32 nevents;
static u32 flags, cr0, cr4, features, max_leaf, max_ext, width;
static int has_id, intel, fault, wrote, leaf8;
static unsigned long long msrs[0x300], original[0x300];
static void fail(int line)
{
    char msg[] = "FAIL line 0000\n";
    for (int i=13; i>=10; --i) { msg[i]='0'+line%10; line/=10; }
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(15) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static void event(char op,u32 key,unsigned long long value)
{ CHECK(nevents<512); events[nevents++]=(struct event){op,key,value}; }
static u32 wc_read_flags(void) { event('F',0,flags); return flags; }
static void wc_write_flags(u32 value)
{ event('f',0,value); flags=has_id ? value : (value&~0x200000UL); }
static void wc_cli(void) { event('I',0,0); flags&=~0x200UL; }
static void wc_cpuid(u32 leaf,u32 *a,u32 *b,u32 *c,u32 *d)
{
    event('P',leaf,0); *a=*b=*c=*d=0;
    if (!leaf) { *a=max_leaf; *b=intel?0x756e6547:0; *d=0x49656e69; *c=0x6c65746e; }
    else if (leaf==1) *d=features;
    else if (leaf==0x80000000UL) *a=max_ext;
    else if (leaf==0x80000008UL) { ++leaf8; *a=width; }
    else CHECK(0);
}
static unsigned long long wc_rdmsr(u32 reg)
{
    CHECK(reg<0x300); event('r',reg,msrs[reg]);
    if (reg==0x202 && wrote && (fault==2 || (fault==1 && wrote==1)))
        return msrs[reg]^0x1000ULL;
    return msrs[reg];
}
static void wc_wrmsr(u32 reg,unsigned long long value)
{
    CHECK(!(flags&0x200) && (cr0&0x60000000UL)==0x40000000UL && !(cr4&0x80));
    CHECK(reg==0x2ff || reg==0x202 || reg==0x203);
    if (reg!=0x2ff) CHECK(!(msrs[0x2ff]&0x800));
    if (reg==0x202) ++wrote;
    event('m',reg,value); msrs[reg]=value;
}
static u32 wc_read_cr0(void) { event('0',0,cr0); return cr0; }
static u32 wc_read_cr4(void) { event('4',0,cr4); return cr4; }
static void wc_write_cr0(u32 value) { event('c',0,value); cr0=value; }
static void wc_write_cr4(u32 value) { event('g',0,value); cr4=value; }
static void wc_wbinvd(void) { event('W',0,0); }
static void wc_flush_tlb(void) { event('T',0,0x123000); }
'''

CASES = r'''
static void setup(int pge,int enabled_if)
{
    nevents=0; flags=enabled_if?0x202:2; cr0=0xa0000011UL; cr4=0x10|(pge?0x80:0);
    features=0x1020; max_leaf=1; max_ext=0x80000007UL; width=36;
    has_id=intel=1; fault=wrote=leaf8=0;
    for (int i=0;i<0x300;++i) msrs[i]=0;
    msrs[0xfe]=0x504; msrs[0x2ff]=0xc06; msrs[0x277]=0x0007040600070406ULL;
    msrs[0x200]=6; msrs[0x201]=0xff0000800ULL; /* RAM below 256 MiB */
    msrs[0x202]=0x1234567800000006ULL; msrs[0x203]=0xabcde00000000400ULL;
    for (int i=0x250;i<=0x26f;++i) msrs[i]=0x0606060606060606ULL;
    for (int i=0;i<0x300;++i) original[i]=msrs[i];
}
static int count(char op)
{ int n=0; for(u32 i=0;i<nevents;++i) n+=events[i].op==op; return n; }
static void unchanged(void)
{ for(int i=0;i<0x300;++i) CHECK(msrs[i]==original[i]); }
static void expect(u32 *pos,char op,u32 key,unsigned long long value)
{
    CHECK(*pos<nevents); struct event e=events[(*pos)++];
    CHECK(e.op==op && e.key==key && e.value==value);
}
static void pair(u32 *p,unsigned long long base,unsigned long long mask,
                 unsigned long long previous)
{
    expect(p,'r',0x203,previous);
    expect(p,'m',0x203,previous&~0x800ULL);
    expect(p,'m',0x202,base); expect(p,'m',0x203,mask);
    expect(p,'r',0x202,base); expect(p,'r',0x203,mask);
}
static void sequence(int pge,int enabled_if,int owned,int restoring)
{
    u32 p=0; while(p<nevents && events[p].op!='I') ++p;
    CHECK(p>=3); p-=3;
    expect(&p,'F',0,enabled_if?0x202:2); expect(&p,'0',0,0xa0000011UL);
    expect(&p,'4',0,pge?0x90:0x10); expect(&p,'I',0,0);
    expect(&p,'c',0,0xc0000011UL); expect(&p,'W',0,0);
    if(pge) expect(&p,'g',0,0x10);
    expect(&p,'T',0,0x123000);
    if(owned) {
        expect(&p,'m',0x2ff,0x406);
        pair(&p,restoring?original[0x202]:0x30000001ULL,
             restoring?original[0x203]:0xffff80800ULL,
             restoring?0xffff80800ULL:original[0x203]);
        expect(&p,'m',0x2ff,0xc06);
    }
    expect(&p,'W',0,0); expect(&p,'T',0,0x123000);
    expect(&p,'c',0,0xa0000011UL); expect(&p,'g',0,pge?0x90:0x10);
    expect(&p,'f',0,enabled_if?0x202:2); CHECK(p==nevents);
    CHECK(flags==(enabled_if?0x202UL:2UL) && cr0==0xa0000011UL && cr4==(pge?0x90UL:0x10UL));
}
static void unavailable(void)
{
    for(int why=0;why<17;++why) {
        setup(0,1); struct x86_wc_lease l={0};
        switch(why) {
        case 0: has_id=0; break;
        case 1: max_leaf=0; break;
        case 2: intel=0; break;
        case 3: features&=~0x20UL; break;
        case 4: features&=~0x1000UL; break;
        case 5: msrs[0xfe]&=~0x400ULL; break;
        case 6: msrs[0x2ff]&=~0x800ULL; break;
        case 7: msrs[0xfe]=0x400; break;
        case 8: for(int i=0;i<4;++i) { msrs[0x200+2*i]=6; msrs[0x201+2*i]=0xff0000800ULL; } break;
        case 9: msrs[0x206]=0x30000006; msrs[0x207]=0xffff80800ULL; break;
        case 10: msrs[0x206]=0x30000001; msrs[0x207]=0xffffc0800ULL; break;
        case 11: features|=0x10000; msrs[0x277]=0; break;
        case 12: max_ext=0x80000008UL; width=31; break;
        case 13: max_ext=0x80000008UL; width=53; break;
        case 15: msrs[0x206]=0x30080006; msrs[0x207]=0xffff00800ULL; break;
        case 16: msrs[0x204]=0x30000001; msrs[0x205]=0xffffc0800ULL;
                 msrs[0x206]=0x30040001; msrs[0x207]=0xffffc0800ULL; break;
        case 14: msrs[0x202]=0x30000001; msrs[0x203]=0xffff00800ULL;
                 msrs[0x206]=0x30040006; msrs[0x207]=0xffffc0800ULL; break;
        }
        for(int i=0;i<0x300;++i) original[i]=msrs[i];
        CHECK(x86_wc_acquire(0x30000000,0x80000,&l)==0 && !l.active);
        CHECK(!count('m') && !count('I') && !count('W') && !count('4'));
        if(why<5) CHECK(!count('r'));
        if(!why) CHECK(!count('P'));
        CHECK(flags==0x202); unchanged();
        nevents=0; CHECK(x86_wc_release(&l)==0 && !nevents);
    }
}
static void owned(void)
{
    for(int pge=0;pge<2;++pge) for(int iff=0;iff<2;++iff) {
        setup(pge,iff); struct x86_wc_lease l={0};
        CHECK(x86_wc_acquire(0x30000000,0x80000,&l)==1);
        CHECK(l.active && l.owned && l.slot==1 && l.old_base==original[0x202] && l.old_mask==original[0x203]);
        CHECK(!leaf8); sequence(pge,iff,1,0);
        for(int i=0;i<0x300;++i) if(i!=0x202 && i!=0x203) CHECK(msrs[i]==original[i]);
        nevents=0; CHECK(x86_wc_release(&l)==0 && !l.active);
        sequence(pge,iff,1,1); unchanged();
        nevents=0; CHECK(x86_wc_release(&l)==0 && !nevents);
    }
    setup(1,1); max_ext=0x80000008UL; width=40; features|=0x10000;
    msrs[0x201]|=0xf000000000ULL; original[0x201]=msrs[0x201];
    struct x86_wc_lease l={0}; CHECK(x86_wc_acquire(0x30000000,0x80000,&l)==1);
    CHECK(leaf8==1 && msrs[0x203]==0xfffff80800ULL); CHECK(x86_wc_release(&l)==0); unchanged();
}
static void borrowed(void)
{
    for(int identity_failure=0;identity_failure<2;++identity_failure) {
        setup(1,1); msrs[0x206]=0x30000001; msrs[0x207]=0xffff00800ULL;
        if(identity_failure) {
            msrs[0x202]=6; msrs[0x203]=0xff0000800ULL;
            msrs[0x204]=6; msrs[0x205]=0xff0000800ULL;
        }
        for(int i=0;i<0x300;++i) original[i]=msrs[i];
        struct x86_wc_lease l={0}; CHECK(x86_wc_acquire(0x30000000,0x80000,&l)==1);
        CHECK(l.active && !l.owned && !count('m') && !count('I') && !count('W')); unchanged();
        /* identity failure has no BAR callback: release must use CPU only. */
        nevents=0; CHECK(x86_wc_release(&l)==0 && !l.active);
        CHECK(count('W')==2 && count('T')==2);
        sequence(1,1,0,0); unchanged();
        nevents=0; CHECK(x86_wc_release(&l)==0 && !nevents);
    }
}
static void rollback(void)
{
    for(int mode=1;mode<=2;++mode) {
        setup(1,0); fault=mode; struct x86_wc_lease l={0};
        int result=x86_wc_acquire(0x30000000,0x80000,&l);
        CHECK(result==(mode==1?0:-1)); CHECK(l.active==(mode==2));
        CHECK(wrote==2 && count('I')==1 && count('W')==2 && count('T')==2);
        CHECK(flags==2 && cr0==0xa0000011UL && cr4==0x90); unchanged();
        if(mode==2) { fault=0; CHECK(x86_wc_release(&l)==0 && !l.active); unchanged(); }
    }
    setup(0,1); struct x86_wc_lease l={0};
    CHECK(x86_wc_acquire(0x30000000,0x80000,&l)==1); fault=2;
    CHECK(x86_wc_release(&l)==-1 && l.active);
    fault=0; CHECK(x86_wc_release(&l)==0 && !l.active); unchanged();
}
static void boundaries(void)
{
    static const u32 ranges[][2]={{0x30000000,0},{0x30000000,0x80001},
        {0x30001000,0x80000},{0,0x80000},{0x30000000,0x800}};
    for(u32 i=0;i<sizeof(ranges)/sizeof(ranges[0]);++i) {
        setup(0,1); struct x86_wc_lease l={0};
        CHECK(x86_wc_acquire(ranges[i][0],ranges[i][1],&l)==0 && !l.active && !nevents);
    }
    setup(0,1); flags|=0x200000; struct x86_wc_lease l={0};
    /* Neither adjacent ranges nor a high physical range aliases our target. */
    msrs[0x204]=0x2ff80006; msrs[0x205]=0xffff80800ULL;
    msrs[0x206]=0x130000006ULL; msrs[0x207]=0xffff80800ULL;
    for(int i=0;i<0x300;++i) original[i]=msrs[i];
    CHECK(x86_wc_acquire(0x30000000,0x80000,&l)==1 && flags==0x200202);
    CHECK(x86_wc_release(&l)==0 && flags==0x200202); unchanged();
    for(int bits=32;bits<=52;bits+=20) {
        setup(0,1); max_ext=0x80000008UL; width=bits;
        unsigned long long addr_mask=((1ULL<<bits)-1)&~0xfffULL;
        msrs[0x201]=(addr_mask&~0xfffffffULL)|0x800ULL;
        for(int i=0;i<0x300;++i) original[i]=msrs[i];
        CHECK(x86_wc_acquire(0xfff80000UL,0x80000,&l)==1);
        CHECK(msrs[0x202]==0xfff80001ULL && msrs[0x203]==((addr_mask&~0x7ffffULL)|0x800ULL));
        CHECK(x86_wc_release(&l)==0); unchanged();
    }
}
void _start(void)
{
    unavailable(); owned(); borrowed(); rollback(); boundaries();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0) : "memory");
    __builtin_unreachable();
}
'''

MUTATIONS = [
    ('mtrr-feature', 'if ((d & required) != required) return 0;',
     'if ((d & (required & ~CPUID_MTRR)) != (required & ~CPUID_MTRR)) return 0;'),
    ('msr-feature', 'if ((d & required) != required) return 0;',
     'if ((d & (required & ~CPUID_MSR)) != (required & ~CPUID_MSR)) return 0;'),
    ('id-restore', 'wc_write_flags(old);', 'wc_write_flags(old ^ EFLAGS_ID);'),
    ('cap-wc', 'if (!(cap & MTRRCAP_WC) || !(def_type & MTRR_ENABLE)) return 0;',
     'if ((!(cap & MTRRCAP_WC) && 0) || !(def_type & MTRR_ENABLE)) return 0;'),
    ('def-enabled', 'if (!(cap & MTRRCAP_WC) || !(def_type & MTRR_ENABLE)) return 0;',
     'if (!(cap & MTRRCAP_WC) || (!(def_type & MTRR_ENABLE) && 0)) return 0;'),
    ('extended-leaf-bound', 'if (a >= CPUID_ADDRESS_WIDTH) {', 'if (1) {'),
    ('mask-high', 'new_mask = (addr_mask & ~((unsigned long long)bytes - 1)) | MTRR_VALID;',
     'new_mask = ((addr_mask & ~((unsigned long long)bytes - 1)) | MTRR_VALID) & 0xffffffffULL;'),
    ('cache-disable', 'wc_write_cr0((saved->cr0 | CR0_CD) & ~CR0_NW);',
     'wc_write_cr0(saved->cr0);'),
    ('pge-clear', 'if (saved->cr4 & CR4_PGE) wc_write_cr4(saved->cr4 & ~CR4_PGE);',
     'if (saved->cr4 & CR4_PGE) wc_write_cr4(saved->cr4);'),
    ('first-flush', 'wc_wbinvd();\n    if (saved->cr4', '/* omit WBINVD */\n    if (saved->cr4'),
    ('cr0-restore', 'wc_write_cr0(saved->cr0);', 'wc_write_cr0(saved->cr0 | CR0_CD);'),
    ('flags-restore', 'wc_write_flags(saved->flags);', 'wc_write_flags(saved->flags | 0x200);'),
    ('second-tlb', 'wc_flush_tlb(); /* second flush */', '/* second flush omitted */'),
    ('pat2', 'if (((wc_rdmsr(MSR_PAT) >> PAT2_SHIFT) & BYTE_MASK) != PAT_UC_MINUS) return 0;',
     'if (((wc_rdmsr(MSR_PAT) >> PAT2_SHIFT) & BYTE_MASK) != PAT_UC_MINUS && 0) return 0;'),
    ('borrowed-drain', 'if (!lease->owned) {', 'if (!lease->owned) { lease->active = 0; return 0;'),
    ('overlap-type', 'if ((range_base & BYTE_MASK) != MTRR_WC) return 0;',
     'if ((range_base & BYTE_MASK) != MTRR_WC && 0) return 0;'),
    ('partial-wc', 'if (overlap || free_slot == count) return 0;',
     'if ((overlap && 0) || free_slot == count) return 0;'),
    ('lowest-slot', 'if (free_slot == count) {', 'if (1) {'),
    ('save-mask', 'lease->old_mask = range_mask;', 'lease->old_mask = 0;'),
    ('base-valid-order', 'wc_wrmsr(reg, wc_rdmsr(reg) & ~MTRR_VALID);',
     'wc_wrmsr(reg, wc_rdmsr(reg) | MTRR_VALID);'),
    ('readback-rollback', 'result = wc_pair_matches(lease->slot, lease->old_base, lease->old_mask) ? 0 : -1;',
     'result = wc_pair_matches(lease->slot, lease->old_base, lease->old_mask) ? 1 : -1;'),
    ('fatal-rollback', 'if (result >= 0) lease->active = result;',
     'if (result >= 0) lease->active = result; else { lease->active = 0; result = 0; }'),
]


def run_fixture(source, directory, runner=None):
    start = source.index('/* CPU primitives begin. */')
    end = source.index('/* CPU primitives end. */')
    unit = directory / 'fixture.c'
    unit.write_text(source[:start] + FAKE + source[end:] + CASES)
    exe = directory / 'host'
    subprocess.run([*FLAGS, '-I' + str(HEADER.parent), str(unit), '-o', str(exe)], check=True)
    return host32.run([str(exe)], runner=runner, capture_output=True, text=True, timeout=30)


def main(mutate=False, runner=None):
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory(prefix='mtrr-wc-') as tmp:
        directory = Path(tmp)
        result = run_fixture(source, directory, runner)
        print('GREEN' if result.returncode == 0 else 'FAIL', host32.selected_runner(runner), result.stdout, flush=True)
        assert result.returncode == 0, result
        if mutate:
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, (name, source.count(old))
                result = run_fixture(source.replace(old, new), directory, runner)
                print(f'RED {name} runtime rc={result.returncode}: {result.stdout.strip()}', flush=True)
                assert result.returncode == 1 and 'FAIL line' in result.stdout, (name, result)
    return 0


def run(ctx) -> int:
    for runner in ctx.runners:
        rc = main(ctx.mutate, runner)
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
