#!/usr/bin/env python3
"""Exercise the real inventory include with a freestanding ILP32 VFS fixture."""
CHECK = dict(name='hsync-inventory', tier=1, needs=[], budget=60,
    covers=['userland/system/hsync_inventory.inc', 'userland/system/hsync.c',
            'sys/kern/exec/os32x_hdr.c', 'lib/crc32_core.inc',
            'tools/tests/test_hsync_inventory.py', 'tools/tests/host32.py'],
    basis=['HSYNC_MANIFEST_DESIGN §3', 'HSYNC_MANIFEST_DESIGN §8'])

import argparse
import os
import pathlib
import subprocess
import tempfile
import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
INC = ROOT / 'userland/system/hsync_inventory.inc'
CRC = ROOT / 'lib/crc32_core.inc'

HARNESS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define OS32_ERR_IO -1
#define OS32_ERR_NOTFOUND -2
#define OS32_ERR_NOTDIR -6
#define OS32_ERR_INVAL -9
#define OS32_ERR_NOSYS -10
#define OS32_ERR_STALE -11
#define OS32_ERR_FULL -13
#define OS32_ERR_NAMETOOLONG -16
#define OS32_ERR_TIMEOUT -19
#define OS32_MAX_PATH 256
#define HS_MAX_FILE_SIZE 0x7fffffffU
#define HS_MAX_PATH_DEPTH 32
#define MAX_DEPTH 8
#define MAX_FILES 128
#define NAME_CAP 64
#define FILE_BUF_SIZE 65536
#define KAPI_O_RDONLY 0
typedef struct { u32 st_size; } OS32_Stat;
typedef struct {
    void *(*mem_alloc)(u32); void (*mem_free)(void *);
    int (*sys_open)(const char *, int); void (*sys_close)(int);
    int (*sys_read)(int, void *, u32); int (*sys_stat)(const char *, OS32_Stat *);
    u32 (*get_tick)(void); int (*sys_yield)(void);
} KernelAPI;
static KernelAPI *api;
static u8 buffer[FILE_BUF_SIZE], *file_buf = buffer;
#include "crc32_core.inc"
#include "hsync_inventory.inc"
static u8 wire[HSI_MAX_BYTES], allocated[HSI_MAX_BYTES], offsets[HSI_MAX_ENTRIES * 4];
static u8 desc[32], local[100];
static u32 wirelen, cursor, tick, reads, closes, local_len, chunks508, chunks1024;
static int allocno, mode, busy, state_error;
static u32 transport;
static char lastpath[256];
static void copy(u8 *d, const u8 *s, u32 n) { while (n--) *d++ = *s++; }
static void put16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static void put32(u8 *p, u32 v) { put16(p, v); put16(p+2, v >> 16); }
static int contains(const char *p, const char *s) {
    u32 i, j; for (i=0; p[i]; i++) { for (j=0; s[j] && p[i+j]==s[j]; j++) {} if (!s[j]) return 1; } return 0;
}
static void *alloc(u32 n) { (void)n; allocno++; if (mode == 3) return (void *)0; return allocno & 1 ? allocated : offsets; }
static void free_mem(void *p) { (void)p; }
static int open_file(const char *path, int flags) {
    u32 i=0; (void)flags; while (path[i] && i<255) { lastpath[i]=path[i]; i++; } lastpath[i]=0;
    cursor=0;
    if (contains(path, "/new")) return mode == 4 ? OS32_ERR_NOTFOUND : 1;
    if (contains(path, "/status/")) return 2;
    if (contains(path, "/data/")) return 3;
    return 4;
}
static void close_file(int fd) { (void)fd; closes++; }
static int read_file(int fd, void *out, u32 n) {
    const u8 *src; u32 len;
    if (fd == 1 || fd == 2) {
        put16(desc+6, busy ? 0 : (state_error ? 2 : 1));
        put32(desc+28, (u32)state_error);
        if (busy && fd == 2 && mode != 5) { busy--; if (!busy) { put16(desc+6, 1); put32(desc+28, 0); } }
        if (fd == 2 && mode == 6) desc[8] ^= 1;
        src=desc; len=mode == 7 ? 3 : 32;
    } else if (fd == 3) { src=wire; len=wirelen; reads++; }
    else { src=local; len=local_len; }
    if (n > len-cursor) n=len-cursor;
    if (fd == 3 && mode == 1 && n) n--;
    if (fd == 3) {
        u32 done=0;
        while (done<n) {
            u32 part=n-done; if (part>transport) part=transport;
            copy((u8 *)out+done, src+cursor+done, part); done+=part;
            if (transport==508) chunks508++; else chunks1024++;
        }
    } else copy(out, src+cursor, n);
    cursor+=n; return (int)n;
}
static int stat_file(const char *path, OS32_Stat *s) { (void)path; s->st_size=wirelen + (mode==2); return 0; }
static u32 get_tick(void) { return tick++; }
static int yield(void) { return 0; }
static KernelAPI mock = {alloc,free_mem,open_file,close_file,read_file,stat_file,get_tick,yield};
static u32 make_record(u32 pos, int kind, const char *path) {
    u32 n=0; while (path[n]) n++;
    wire[pos]=kind; wire[pos+1]=0; put16(wire+pos+2,n);
    put32(wire+pos+4,0); put32(wire+pos+8,0); put32(wire+pos+12,0);
    copy(wire+pos+16,(const u8 *)path,n); return pos+16+n;
}
static void header(u32 n) {
    copy(wire,(const u8 *)"HMT1",4); put16(wire+4,1); put16(wire+6,24);
    put32(wire+8,wirelen); put32(wire+12,n);
    put32(wire+16,crc32_core_final(crc32_core_update(CRC32_INIT,wire+24,wirelen-24)));
    put32(wire+20,3);
}
static void fixture(void) {
    u32 i;
    api=&mock; allocno=mode=busy=state_error=0; tick=reads=closes=chunks508=chunks1024=0; transport=508;
    copy(desc,(const u8 *)"HMD1",4); put16(desc+4,1); put16(desc+6,1);
    for (i=0;i<16;i++) desc[8+i]=i==0?0xab:i;
    wirelen=make_record(24,2,"a"); wirelen=make_record(wirelen,1,"a/x");
    wirelen=make_record(wirelen,3,"b"); header(3);
    put32(desc+24,wirelen); put32(desc+28,0);
}
#define CHECK(c,n) do { if (!(c)) return n; } while (0)
static int test_crc(void) {
    u32 c=CRC32_INIT, crc, bytes;
    CHECK(crc32_core_final(c)==0,1);
    c=crc32_core_update(c,"1234",4); c=crc32_core_update(c,"56789",5);
    CHECK(crc32_core_final(c)==0xcbf43926U,2);
    copy(local,(const u8 *)"123456789",9); local_len=9;
    CHECK(hsi_local_crc("/local",9,&crc,&bytes)==0 && crc==0xcbf43926U && bytes==9,3);
    CHECK(hsi_local_crc("/local",8,&crc,&bytes)==OS32_ERR_IO,4);
    local_len=0; CHECK(hsi_local_crc("/local",0,&crc,&bytes)==0 && crc==0,5);
    return 0;
}
static int test_acquire(void) {
    HsInventory inv; HsDescriptor d; int rc;
    fixture(); busy=1; rc=hsi_acquire("/host",&inv);
    CHECK(rc==0 && inv.count==3 && inv.skipped==3,10);
    CHECK(contains(lastpath,"/data/ab0102030405060708090a0b0c0d0e0f"),11);
    CHECK(inv.offsets[0]==24 && inv.offsets[1]==41 && inv.offsets[2]==60,12);
    hsi_release(&inv);
    fixture(); mode=1; CHECK(hsi_acquire("/host",&inv)==OS32_ERR_IO,13);
    fixture(); mode=2; CHECK(hsi_acquire("/host",&inv)==OS32_ERR_IO,14);
    fixture(); mode=4; CHECK(hsi_acquire("/host",&inv)==HSI_FALLBACK,15);
    fixture(); state_error=OS32_ERR_FULL; CHECK(hsi_acquire("/host",&inv)==HSI_FALLBACK,16);
    fixture(); state_error=OS32_ERR_STALE; CHECK(hsi_acquire("/host",&inv)==OS32_ERR_STALE,17);
    fixture(); desc[0]='X'; CHECK(hsi_acquire("/host",&inv)==HSI_FALLBACK,18);
    fixture(); put16(desc+4,2); CHECK(hsi_acquire("/host",&inv)==OS32_ERR_IO,19);
    fixture(); mode=3; CHECK(hsi_acquire("/host",&inv)==HSI_FALLBACK,32);
    fixture(); mode=5; busy=1; CHECK(hsi_acquire("/host",&inv)==OS32_ERR_TIMEOUT,33);
    fixture(); mode=6; busy=1; CHECK(hsi_acquire("/host",&inv)==OS32_ERR_IO,34);
    fixture();
    for (u32 len=0; len<4; len++) CHECK(hsi_descriptor(desc,len,&d)==HSI_FALLBACK,37);
    CHECK(hsi_descriptor(desc,4,&d)==OS32_ERR_IO,38);
    mode=7; CHECK(hsi_read_descriptor("/status/test",0,&d)==OS32_ERR_IO,39);
    fixture(); mode=7; CHECK(hsi_acquire("/host",&inv)==HSI_FALLBACK,35);
    fixture(); put32(wire+12,8193); CHECK(hsi_acquire("/host",&inv)==OS32_ERR_IO,36);
    return 0;
}
static int test_decode(void) {
    HsInventory inv; u32 off[HSI_MAX_ENTRIES];
    fixture(); inv.data=wire; inv.bytes=wirelen; inv.offsets=off;
    CHECK(hsi_decode(&inv)==0,20);
    wire[24+1]=1; header(3); CHECK(hsi_decode(&inv)==OS32_ERR_IO,21);
    fixture(); wire[wirelen-1]^=1; CHECK(hsi_decode(&inv)==OS32_ERR_IO,22);
    fixture(); wirelen=make_record(24,1,"a/x"); header(1); inv.bytes=wirelen;
    CHECK(hsi_decode(&inv)==OS32_ERR_IO,23);
    fixture(); wirelen=make_record(24,1,"a"); wirelen=make_record(wirelen,1,"a"); header(2); inv.bytes=wirelen;
    CHECK(hsi_decode(&inv)==OS32_ERR_IO,24);
    fixture(); put16(wire+4,2); CHECK(hsi_decode(&inv)==OS32_ERR_IO,25);
    fixture(); inv.bytes=wirelen-1; CHECK(hsi_decode(&inv)==OS32_ERR_IO,26);
    return 0;
}
static int test_boundary(void) {
    HsInventory inv; u32 i, j, pos, k, t, count, left, width; char name[64]; int rc;
    static const u32 lengths[] = {507,508,509,1023,1024,1025};
    for (k=0; k<6; k++) {
        fixture(); pos=24; left=lengths[k]-24;
        count=(left+78)/79;
        for (i=0; i<count; i++) {
            width=(left-16*(count-i))/(count-i);
            name[0]='A'+i; for (j=1;j<width;j++) name[j]='x'; name[width]=0;
            pos=make_record(pos,1,name); left-=16+width;
        }
        wirelen=pos; header(count); put32(desc+24,wirelen);
        for (t=0; t<2; t++) {
            transport=t ? 1024 : 508; reads=chunks508=chunks1024=0; allocno=0;
            rc=hsi_acquire("/host",&inv);
            CHECK(rc==0 && wirelen==lengths[k] && reads==1 &&
                  (t ? chunks1024==(wirelen+1023)/1024 : chunks508==(wirelen+507)/508),31);
            hsi_release(&inv);
        }
    }
    fixture(); pos=24;
    /* 128 parents, each with 40 children, produces a list over 64512 B. */
    for (i=0;i<100;i++) {
        name[0]='a'+i/26; name[1]='a'+i%26; name[2]=0;
        pos=make_record(pos,2,name); name[2]='/';
        for (j=0;j<40;j++) {
            name[3]='a'+j/26; name[4]='a'+j%26; name[5]=0;
            pos=make_record(pos,1,name);
        }
    }
    wirelen=pos; header(4100); put32(desc+24,wirelen);
    rc=hsi_acquire("/host",&inv);
    CHECK(rc==0 && inv.count==4100 && reads>=2,30);
    hsi_release(&inv); return 0;
}
static int run(void) { int r; fixture(); r=test_crc(); if(r)return r; r=test_acquire(); if(r)return r; r=test_decode(); if(r)return r; return test_boundary(); }
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
'''


def execute(directory, inc, crc):
    (directory / 'hsync_inventory.inc').write_text(inc)
    (directory / 'crc32_core.inc').write_text(crc)
    (directory / 'harness.c').write_text(HARNESS)
    exe = directory / 'harness'
    host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra',
                  '-Werror', '-ffreestanding', '-fno-pie', '-no-pie',
                  '-fno-stack-protector', '-nostdlib', '-static',
                  str(directory / 'harness.c'), '-o', str(exe)],
                 check=True, capture_output=True, text=True)
    return host32.run([str(exe)], capture_output=True, timeout=60).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    inc, crc = INC.read_text(), CRC.read_text()
    with tempfile.TemporaryDirectory(prefix='hsync-inventory-') as tmp:
        directory = pathlib.Path(tmp)
        # Also compile against the real KernelAPI, types and hsync globals.
        target = directory / 'target.c'
        target.write_text((ROOT / 'userland/system/hsync.c').read_text() +
                          '\n#include "hsync_inventory.inc"\n')
        subprocess.run([str(pathlib.Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'),
                        '-std=gnu11', '-m32', '-ffreestanding', '-fno-pie',
                        '-Werror=implicit-function-declaration', '-Werror=implicit-int',
                        '-Werror=vla', '-fsyntax-only', '-D__OS32_USERLAND__',
                        '-I.', '-Iinclude', '-Iarch/x86', '-Isdk/include',
                        '-Isdk/include/os32', '-Iuserland/lib', '-Iuserland/system',
                        str(target)], cwd=ROOT, check=True, capture_output=True, text=True)
        rc = execute(directory, inc, crc)
        assert rc == 0, f'normal rc={rc}'
        if args.mutate:
            mutants = [
                ('short descriptor', 'inc', 'if (len < 4) return HSI_FALLBACK;', 'if (len < 4) return OS32_ERR_IO;'),
                ('oversized header', 'inc', 'count > HSI_MAX_ENTRIES || inv->bytes > HSI_MAX_BYTES) return OS32_ERR_IO;', 'count > HSI_MAX_ENTRIES || inv->bytes > HSI_MAX_BYTES) return HSI_FALLBACK;'),
                ('CRC final XOR' , 'crc', 'return state ^ CRC32_XOROUT;', 'return state;'),
                ('data offset', 'inc', 'pos += (u32)rc;', 'pos += (u32)rc + 1;'),
                ('structure flags', 'inc', 'p[pos] > 3 || p[pos + 1] ||', 'p[pos] > 3 || 0 ||'),
            ]
            for name, target, old, new in mutants:
                original = inc if target == 'inc' else crc
                assert original.count(old) == 1, name
                changed = original.replace(old, new)
                rc = execute(directory, changed if target == 'inc' else inc,
                             changed if target == 'crc' else crc)
                assert rc > 0, f'{name}: expected runtime RED, rc={rc}'
                subprocess.run(['grep', '-nF', '#include "hsync_inventory.inc"',
                                str(directory / 'harness.c')], check=True)
                if target == 'inc':
                    subprocess.run(['grep', '-nF', new,
                                    str(directory / 'hsync_inventory.inc')], check=True)
                assert execute(directory, inc, crc) == 0, f'{name}: restored control failed'
                print(f'RED {name}: rc={rc}', flush=True)
    print('PASS inventory CRC, acquisition, malformed tree and chunked reads')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', str(pathlib.Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
