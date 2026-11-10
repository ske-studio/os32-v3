"""Run PC98 boot capture without V86, and the PowerMac image provider."""
from pathlib import Path
import subprocess
import tempfile
import sys
import struct
from kernel_includes import include_flags
import host32

ROOT = Path(__file__).resolve().parents[2]
PATHS = ['include/bootinfo.h', 'platform/pc98/bootinfo_wire.h',
         'platform/pc98/bootinfo_native.h', 'platform/pc98/bootinfo_capture.c',
         'platform/pc98/bootinfo_check.c', 'platform/pc98/bios_snapshot.c',
         'platform/pc98/bios_snapshot.h', 'platform/pc98/boot_capture.c',
         'platform/powermac/boot_capture.c', 'init/bootinfo.c']
CHECK = dict(name='boot-readers-host', tier=1, covers=['tools/tests/test_module_image.py', 'tools/tests/module_image_host.c',
         'kernel/module/module_image.c', 'kernel/module/module_image.h',
         'kernel/module/module_schema.h', 'tools/module_schema.py',
         'tools/gen_module_schema.py', 'include/bootinfo.h', 'platform/pc98/bootinfo_wire.h',
         'platform/pc98/bootinfo_native.h', 'platform/pc98/bootinfo_capture.c',
         'platform/pc98/bootinfo_check.c', 'platform/pc98/bios_snapshot.c',
         'platform/pc98/bios_snapshot.h', 'platform/pc98/boot_capture.c',
         'platform/powermac/boot_capture.c', 'platform/powermac/kboot_layout.h', 'init/bootinfo.c',
    'tools/tests/test_boot_readers.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'include/platform_boot.h',
    'include/memmap.h', 'include/types.h', 'include/build_id.h',
    'init/boot_disk.h', 'mm/paging.h', 'lib/kstring.h', 'lib/le_internal.h',
    'sdk/include/os32/**'], needs=[], budget=3.2)

PRELUDE = r'''
#include "memmap.h"
#include "paging.h"
#include "bootinfo.h"
#include "bootinfo_native.h"
#include "os32_kapi_shared.h"
#include "build_id.h"
static u8 boot_wire[BOOTINFO_WIRE_SIZE], page0[PAGE_SIZE];
#undef P2V_IO
#undef P2V_BOOT
#undef P2V
#define P2V_IO(x) (boot_wire)
#define P2V_BOOT(x) (page0)
#define P2V(x) (page0)
static int capture_sequence;
static int paging_set_page_unused;
int paging_set_page(u32 v, u32 p, u32 f) { (void)v;(void)p;(void)f;return paging_set_page_unused; }
const char os32_build_commit[BUILD_COMMIT_MAX] = "boot-readers";
void *kmemset(void *v, int c, u32 n) { u8 *p=v;while(n--) *p++=(u8)c;return v; }
void *kmemcpy(void *v, const void *s, u32 n) { u8 *p=v;const u8 *q=s;while(n--) *p++=*q++;return v; }
char *kstrncpy(char *v, const char *s, u32 n) { char *p=v;while(n--) { *p++=*s; if(*s) s++; }return v; }
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
'''
BODY = r'''
static int test(void) {
    for (unsigned i=0;i<PAGE_SIZE;i++) page0[i]=(u8)(i ^ (i>>8));
    boot_wire[BI_OFF_VERSION]=BOOTINFO_VERSION;
    boot_wire[BI_OFF_SOURCE]=BOOTINFO_SRC_HDD;
    boot_wire[BI_OFF_NDRIVES]=BOOTINFO_NDRIVES;
    bootinfo_seal(boot_wire);
    bootinfo_seal_image(boot_wire, 0x12345678, 0x87654321);
    platform_boot_capture();
    REQUIRE(capture_sequence==12);
    REQUIRE(platform_bios_ready());
    REQUIRE(!boot_wire[0] && !boot_wire[1] && !boot_wire[2] && !boot_wire[3]);
    const struct boot_common *b=boot_common_get();
    REQUIRE(b->image_valid && b->image_crc==0x12345678 && b->image_size==0x87654321);
    BootImageInfo out;
    REQUIRE(!boot_image_info(&out) && out.crc_valid && out.source==BOOTINFO_SRC_HDD);
    REQUIRE(out.image_crc==b->image_crc && out.image_size==b->image_size);
    u32 pair[2]={out.crc_valid,out.image_crc};
    __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(pair),"d"(sizeof(pair)));
    for(unsigned i=0;i<PAGE_SIZE;i++) REQUIRE(v86_bios_real_lowmem()[i]==(u8)(i^(i>>8)));
    page0[0]=99; platform_boot_capture();
    REQUIRE(v86_bios_real_lowmem()[0]==0 && boot_common_get()->image_valid);
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80"::"a"(1),"b"(rc ? 1 : 0)); __builtin_unreachable(); }
'''


def run_case(mutation=None, none=False, bad_image=False, pair_probe=False):
    with tempfile.TemporaryDirectory(prefix='boot-readers-') as td:
        tmp=Path(td)
        paths=['init/bootinfo.c']
        if none:
            paths+=['platform/powermac/boot_capture.c']
            body=BODY[BODY.index('void _start'):]
            body='__asm__(".globl __kernel_image_start\\n.set __kernel_image_start, 0x100000\\n.globl __kernel_file_end\\n.set __kernel_file_end, 0x170000\\n");\nstatic int test(void) { const struct boot_common value={1,2,3}; boot_common_set(&value,1); platform_boot_capture(); REQUIRE(!boot_common_get()->image_valid && !boot_common_get()->image_crc && boot_common_get()->image_size==0x70000); return 0; }\n'+body
        else:
            paths+=['platform/pc98/bootinfo_check.c','platform/pc98/bootinfo_capture.c',
                    'platform/pc98/bios_snapshot.c','platform/pc98/boot_capture.c']
            body=BODY
        if bad_image:
            body=body.replace('    platform_boot_capture();', '    boot_wire[BI_OFF_IMG_CHECK] ^= 1;\n    platform_boot_capture();', 1)
            body=body.replace('b->image_valid && b->image_crc==0x12345678 && b->image_size==0x87654321', '!b->image_valid && !b->image_crc && !b->image_size')
            body=body.replace('!boot_image_info(&out) && out.crc_valid', '!boot_image_info(&out) && !out.crc_valid')
            body=body.replace('v86_bios_real_lowmem()[0]==0 && boot_common_get()->image_valid', 'v86_bios_real_lowmem()[0]==0 && !boot_common_get()->image_valid')
        prelude=PRELUDE
        if none:
            for line in ('#include "paging.h"\n',
                         '#include "bootinfo_native.h"\n',
                         'static u8 boot_wire[BOOTINFO_WIRE_SIZE], page0[PAGE_SIZE];\n'):
                prelude=prelude.replace(line, '')
        source=prelude+'\n'
        for rel in paths:
            text=(ROOT/rel).read_text()
            if mutation and mutation[0]==rel:
                _,old,new=mutation
                assert text.count(old)==1, old
                text=text.replace(old,new)
            # Quoted relative includes resolve against the source file, not this fixture.
            text=text.replace('"../../init/boot_disk.h"','"init/boot_disk.h"').replace('"../../mm/paging.h"','"mm/paging.h"')
            if rel=='platform/pc98/bootinfo_capture.c':
                source+='#define bootinfo_capture capture_impl\n'+text+'\n#undef bootinfo_capture\nvoid bootinfo_capture(void) { capture_sequence=capture_sequence*10+1; capture_impl(); }\n'
            elif rel=='platform/pc98/bios_snapshot.c':
                source+='#define v86_bios_save_real save_impl\n'+text+'\n#undef v86_bios_save_real\nvoid v86_bios_save_real(void) { capture_sequence=capture_sequence*10+2; save_impl(); }\n'
            else:
                source+=text+'\n'
        source+=body
        (tmp/'fixture.c').write_text(source)
        exe=tmp/'fixture'
        subprocess.run(['gcc','-m32','-march=i386','-std=gnu11','-D__cdecl=',
            '-MMD','-MF',str(tmp/'fixture.d'),'-ffreestanding','-fno-pie','-fno-stack-protector','-nostdlib','-static',
            '-no-pie','-ffunction-sections','-Wl,--gc-sections', '-I'+str(ROOT), *(['-I'+str(ROOT/'platform/powermac')] if none else []),
            *include_flags(ROOT),str(tmp/'fixture.c'),'-o',str(exe)],check=True,
            capture_output=True)
        assert not none or 'platform/pc98/' not in (tmp/'fixture.d').read_text()
        result=host32.run([str(exe)], capture_output=True)
        if pair_probe:
            assert result.returncode==0 and len(result.stdout)==8
            return struct.unpack('<II',result.stdout)
        return result.returncode


def module_pair():
    from test_module_image import build_validator, validate, fixture, word, crc, POLICY
    valid, image_crc=run_case(pair_probe=True)
    with tempfile.TemporaryDirectory(prefix='boot-pair-') as td:
        exe=build_validator(Path(td))
        image=crc(word(fixture(),31,image_crc))
        assert validate(exe,image,pair=image_crc,img_valid=valid,**POLICY).returncode==0
        mismatch=validate(exe,image,pair=image_crc ^ 1,img_valid=valid,**POLICY)
        assert mismatch.returncode==1 and mismatch.stdout==b'RED 04\n'
        invalid, _=run_case(bad_image=True,pair_probe=True)
        missing=validate(exe,image,pair=image_crc,img_valid=invalid,**POLICY)
        assert missing.returncode==1 and missing.stdout==b'RED 05\n'
    print('GREEN captured image ABI feeds real module pair validation; mismatched/missing CRC rejected')


def main():
    module_pair()
    assert run_case()==0
    assert run_case(none=True)==0
    assert run_case(bad_image=True)==0
    print('GREEN native image/ABI, immutable page0, capture order, feature-off and none')
    if '--mutate' in sys.argv:
        mutations=[
            ('platform/pc98/bootinfo_check.c', 'out->magic = le_read32(raw + BI_OFF_MAGIC);', 'out->magic = __builtin_bswap32(le_read32(raw + BI_OFF_MAGIC));'),
            ('platform/pc98/bootinfo_check.c', 'le_read32(raw + BI_OFF_IMG_CHECK) == BOOTINFO_IMG_CHECK_OF(crc, size)', '1'),
            ('platform/pc98/boot_capture.c', '    bootinfo_capture();\n    v86_bios_save_real();', '    v86_bios_save_real();\n    bootinfo_capture();'),
            ('platform/pc98/boot_capture.c', '    v86_bios_save_real();', '    /* optional guest disabled */'),
        ]
        for m in mutations:
            assert run_case(m, bad_image='IMG_CHECK' in m[1])==1, m
            print('RED',m[0],m[1].splitlines()[0])


def run(ctx) -> int:
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_boot_readers.py', *(['--mutate'] if ctx.mutate else [])]
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)


if __name__=='__main__':
    main()
