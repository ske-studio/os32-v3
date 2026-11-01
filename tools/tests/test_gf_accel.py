#!/usr/bin/env python3
"""Acceptance producer/consumer, firmware WC SKIP, clock/point failures and GUI phases."""
CHECK = dict(name='gf-accel-host', tier=1, needs=[], budget=90.0,
    covers=['tools/tests/test_gf_accel.py', 'tools/accept/gf_accel.py',
            'drivers/video/geforce/gf_accept.c', 'drivers/video/geforce/gf_present.c',
            'drivers/video/geforce/geforce_module.h', 'hwdev/gf/production.h',
            'userland/gshell/src/wm.rs',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])
from pathlib import Path
import importlib.util
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie']


def consumer(source=None):
    spec = importlib.util.spec_from_file_location('gf_accel', ROOT/'tools/accept/gf_accel.py')
    module = importlib.util.module_from_spec(spec)
    if source is None:
        spec.loader.exec_module(module)
    else:
        exec(compile(source, str(spec.origin), 'exec'), module.__dict__)
    return module


def producer(mode, scenario=0, mutation=None):
    source = (ROOT/'drivers/video/geforce/gf_accept.c').read_text()
    if mutation:
        old, new = mutation
        assert source.count(old) == 1, old
        source = source.replace(old, new, 1)
    a = source.index('/* Read-only CPU primitives begin.')
    b = source.index('/* Read-only CPU primitives end. */')
    primitives = r'''
static u32 model_flags, model_wc, ticks, timing, calls;
static u32 flags(void) { return model_flags; }
static void set_flags(u32 v) { model_flags=v; }
static void cpuid(u32 leaf,u32 sub,u32 *a,u32 *b,u32 *c,u32 *d) {
    (void)sub; *a=*b=*c=*d=0;
    if (!leaf) { *a=4; *b=0x756e6547; *d=0x49656e69; *c=0x6c65746e; }
    if (leaf==1) { *a=0x652; *d=(1<<5)|(1<<12); }
    if (leaf==2) *a=0x0a060001;
    if (leaf==0x80000000) *a=0x80000000;
}
static unsigned long long msr(u32 reg) {
    if (reg==0xfe) return 0x402;
    if (reg==0x2ff) return 0x800;
    if (reg==0x200 && model_wc) return 0x30000001;
    if (reg==0x201 && model_wc) return 0xffff80800ULL;
    return 0;
}
'''
    source = source[:a] + primitives + source[b:]
    harness = r'''
#include "accept.c"
static u8 client[GF_MODE_VRAM_BYTES], display[GF_MODE_VRAM_BYTES];
static u8 model_pattern[GF_MODE_VRAM_BYTES] __attribute__((aligned(4096)));
static u32 registers[0x10020c/4+1];
static GfxCounters counters;
static struct gf_production_resources resources;
static struct geforce_platform platform;
static GeforceModuleContext context={.counters=&counters,.platform=&platform};
const GeforceModuleContext *geforce_context=&context;
static void die(int code) { __asm__ volatile("int $0x80" : : "a"(1),"b"(code) : "memory"); for (;;) {} }
static void report(const char *s) {
    if (timing) die(51);
    u32 n=0; while (s[n]) ++n;
    __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(s),"d"(n) : "memory");
}
int sys_time_now(u32 *lo,u32 *hi) {
    timing=!timing; ++calls; ticks+=10; *lo=ticks; *hi=1;
    return SCENARIO==2 && calls==9 ? -1 : 0;
}
u32 pgalloc_alloc_phys(u32 owner,int pages) { (void)owner; if(pages!=75) die(56); return V2P(model_pattern); }
int pgalloc_free_n_owner(u32 owner,u32 pfn,int pages) { (void)owner; if(pfn!=V2P(model_pattern)/PAGE_SIZE || pages!=75) die(57); return 0; }
u32 paging_pte_raw(u32 addr) { return addr|PTE_PRESENT|PAGE_RW|PTE_PCD; }
u32 pci_cfg_read32(u32 b,u32 d,u32 f,u32 reg) { (void)b;(void)d;(void)f;(void)reg; return 0x6000; }
u16 pci_cfg_read16(u32 b,u32 d,u32 f,u32 reg) { (void)b;(void)d;(void)f;(void)reg; return 0x102; }
static int start(void) { if (SCENARIO==4) return 0; model_wc=1; return 1; }
static int stop(void) { model_wc=SCENARIO==1; return 0; }
static int point(void *v) { (void)v; return SCENARIO==3 && measured && points>1000; }
void _start(void) {
    platform.display_phys=0x30000000; platform.resources=&resources;
    platform.wc_start=start; platform.wc_stop=stop; platform.report=report;
    for (u32 i=0; i<sizeof(client); ++i) client[i]=0xa5;
    int first=gf_accept_transfer(client,640,display,640,(volatile u8 *)registers,0,0,640,480,point,0);
    if (first) die(52);
    int rc=gf_accept_transfer(client,640,display,640,(volatile u8 *)registers,1,1,32,32,point,0);
    if (SCENARIO==3) die(rc == -1 ? 0 : 53);
    if (rc) die(54);
    for (u32 i=0; i<sizeof(client); ++i) if (display[i]!=client[i]) die(55);
    if (GF_ACCEPT_MODE!=2 && SCENARIO==0) {
        model_wc=1; /* Ordinary backend startup can reacquire on GUI reentry. */
        if (gf_accept_transfer(client,640,display,640,(volatile u8 *)registers,0,0,1,1,point,0) || model_wc) die(58);
    }
    die(0);
}
'''
    with tempfile.TemporaryDirectory(prefix='gf-accept-') as directory:
        tmp = Path(directory)
        (tmp/'accept.c').write_text(source)
        (tmp/'host.c').write_text(harness)
        exe = tmp/'host'
        subprocess.run(FLAGS+(['-DGF_PRESENT_BYTE'] if mode == 0 else [])+include_flags(ROOT)+['-DOS32_GEFORCE_MODULE', f'-DGF_ACCEPT_MODE={mode}',
            '-DGF_ACCEPT_ID="fixture"', '-DGF_ACCEPT_PAIR=123', f'-DSCENARIO={scenario}', '-I'+str(ROOT/'drivers/video/geforce'),
            str(tmp/'host.c'), str(ROOT/'drivers/video/geforce/gf_present.c'), '-o', str(exe)], check=True)
        result = host32.run([str(exe)], capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (mode, scenario, result.returncode, result.stderr)
        return result.stdout


def reject(module, text):
    try:
        module.parse_log(text, 'fixture')
    except ValueError:
        return
    raise AssertionError('invalid observation accepted')


def gui(mutation=None):
    text = (ROOT/'userland/gshell/src/wm.rs').read_text()
    start = text.index('// Acceptance timing begin.')
    module = text[start:text.index('// Acceptance timing end.')]
    if mutation:
        old, new = mutation
        assert module.count(old) == 1
        module = module.replace(old, new, 1)
    harness = r'''
extern crate self as os32api;
use std::sync::atomic::{AtomicU32,Ordering};
static TICK: AtomicU32=AtomicU32::new(0);
static OUTPUT: std::sync::Mutex<Vec<String>>=std::sync::Mutex::new(Vec::new());
struct Api { sys_time_now: unsafe fn(*mut u32,*mut u32)->i32, serial_puts: unsafe fn(*const u8) }
fn api()->&'static Api { &Api { sys_time_now: now,serial_puts: report } }
unsafe fn now(lo:*mut u32,hi:*mut u32)->i32 { *lo=TICK.fetch_add(10,Ordering::SeqCst); *hi=0; 0 }
unsafe fn report(s:*const u8) { OUTPUT.lock().unwrap().push(std::ffi::CStr::from_ptr(s.cast()).to_str().unwrap().into()); }
fn main() {
    for _ in 0..20 { gf_accept::begin(); gf_accept::event_done(); gf_accept::finish(); }
    for _ in 0..310 {
        gf_accept::begin(); gf_accept::event_done();
        let p=gf_accept::present_begin(); gf_accept::present_end(p); gf_accept::finish();
    }
    let output=OUTPUT.lock().unwrap(); assert_eq!(output.len(),300);
    for (i,line) in output.iter().enumerate() {
        let words:Vec<_>=line.split_whitespace().collect();
        assert_eq!(words[..4],["GFW1","fixture","byte-uc","sample"]);
        assert_eq!(words[4].parse::<usize>().unwrap(),i);
        assert_eq!(words[5..],["40","10","20","10"]);
        print!("{line}");
    }
}
'''
    with tempfile.TemporaryDirectory(prefix='gf-wm-') as directory:
        tmp = Path(directory); path = tmp/'host.rs'; exe = tmp/'host'
        path.write_text(module+harness)
        subprocess.run(['rustc', '--edition=2021', '--cfg', 'feature="gf_accel_accept"', str(path), '-o', str(exe)],
            check=True, env=dict(os.environ, GF_ACCEPT_ID='fixture', GF_ACCEPT_MODE='byte-uc'))
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        assert result.returncode == (101 if mutation else 0), result.stderr
    print('RED GUI present phase' if mutation else 'PASS GUI: warmup, idle exclusion, 300 updates, phase sum, deferred serial')
    return result.stdout


def main():
    module = consumer()
    for count, p50, p95 in ((100, 50, 95), (300, 150, 285)):
        result = module.stats(list(range(1, count+1)))
        assert (result['p50'], result['p95']) == (p50, p95)
    assert module.stats([1, module.FAILED, 10])['failures'] == 1
    assert module.cache_lines([[2, 0, 0x0a060001, 0, 0, 0]]) == [32]
    assert module.cache_lines([[1, 0, 0, 0xff00, 0, 0]]) == 'unknown'
    logs = [producer(mode) for mode in range(3)]
    for mode, log in enumerate(logs):
        result = module.parse_log(log, 'fixture')
        assert result['status'] == 'OK'
        assert result['rectangles']['1']['pooled']['p95'] == 10
        assert result['rectangles']['1']['points'] == [48000]*3
        assert result['cpu_cache_line_bytes'] == [32]
        reject(module, log.replace('fixture', 'old'))
        reject(module, log.replace('GFA1 fixture '+module.MODES[mode]+' done OK', 'missing'))
    for mode in (0, 1):
        skipped = module.parse_log(producer(mode, 1), 'fixture')
        assert skipped['status'] == 'SKIP' and skipped['reason'] == 'bios-wc'
    assert module.parse_log(producer(2, 4), 'fixture')['status'] == 'SKIP'
    failed = module.parse_log(producer(0, 2), 'fixture')
    assert failed['status'] == 'FAIL' and failed['rectangles']['0']['pooled']['failures'] == 1
    reject(module, producer(0, 3))
    fake_uc = logs[0].replace('state UC OK', 'state WC OK')
    reject(module, fake_uc)
    wm_log = gui()
    assert module.parse_log(logs[0]+wm_log, 'fixture', require_wm=True)['wm']['total']['p95'] == 40
    with tempfile.TemporaryDirectory(prefix='gf-cli-') as directory:
        paths = [Path(directory)/f'{mode}.log' for mode in range(3)]
        for path, log in zip(paths, logs):
            path.write_text(log)
        command = [sys.executable, '-B', str(ROOT/'tools/accept/gf_accel.py'), '--build-id', 'fixture',
                   '--pair-crc', '123', *map(str, paths)]
        assert subprocess.run(command, capture_output=True).returncode == 0
        assert subprocess.run(command+['--require-wm'], capture_output=True).returncode == 1
        paths[0].write_text(logs[0].replace('pair 123', 'pair 124'))
        assert subprocess.run(command, capture_output=True).returncode == 1
    print('PASS acceptance: real present, 3 modes, 10+300 samples, readback, BIOS SKIP, failed clock/point, version and ranks')
    if '--mutate' in sys.argv:
        original = (ROOT/'tools/accept/gf_accel.py').read_text()
        anchor = "status != 'OK' or effective != expected"
        assert original.count(anchor) == 1
        bad = consumer(original.replace(anchor, "status != 'OK'", 1))
        assert bad.parse_log(fake_uc, 'fixture')['effective'] == 'WC'
        print('RED effective type: mutated consumer accepts WC as UC')
        mutation = ('usable = original == 1;', 'usable = 1;')
        reject(module, producer(0, 1, mutation))
        print('RED BIOS WC: mutated producer emits WC measurements in UC group')
        gui(('s.present += end.saturating_sub(start);', 's.present += 0;'))
    return 0


def run(ctx):
    argv = [sys.executable, '-B', str(ctx.root/'tools/tests/test_gf_accel.py')]
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    raise SystemExit(main())
