#!/usr/bin/env python3
"""HAL lifecycle and real regs/Save/Init/Mode/Enable/verify/present/Restore.

MIT oracle notices in trident.h and trident_pc98init_host.c. Save/route/Restore
use the committed HW-BASIC script; Mode uses the independent MIT reference
fixture (D:1004-1044, trident_dac.c:631-810, vgaHW.c:845-872). No local refs
or product output generate expected writes. Script reads supply operands;
C assertions independently check boot slots, mode/image and capacity.

Intentional differences from H, following reference:
* PreInit SPR (D:2920), Init protect on/off (D:622/690).
* Mode reads TridentInit inputs before Protect/Restore (dac.c:631-749),
  SR1=01 and VGA CR13=28 before extended offset50 (vgaHW.c:1219-1468).
* VGA pairs are two OUTBs; AR palette access surrounds the VGA AR table.
* Save-time IOBase / color Mode / saved Restore follow vgaHW, including mono.
* Full nondestructive capacity verification and HAL palette/client replace
  H's endpoint-only test, 8-color DAC setup, print/wait. Present uses rows.
* Final CR11 lock after Restore (D:3347), absent from the script.
"""
CHECK = dict(name='trident-lifecycle', tier=1, needs=[], budget=600,
    covers=['drivers/video/trident/lifecycle.c', 'drivers/video/trident/backend_trident.c', 'drivers/video/trident/trident.h', 'drivers/video/trident/trident_module.h', 'drivers/video/trident/regs.c', 'drivers/video/trident/probe.c', 'drivers/video/trident/state_save.c', 'drivers/video/trident/pc98_init.c', 'drivers/video/trident/mode_640.c', 'drivers/video/trident/route_ra266.c', 'drivers/video/trident/vram.c', 'drivers/video/trident/default_dac.c', 'drivers/video/trident/state_restore.c', 'drivers/video/trident/present.c', 'drivers/video/trident/palette.c', 'tools/tests/trident_lifecycle_host.c', 'tools/tests/trident_device_fake.c', 'tools/tests/trident_device_fake.h', 'tools/tests/trident_pc98init_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk', 'include/types.h', 'include/memmap.h', 'include/io.h', 'include/gfx_hal.h', 'include/gfx_mode.h', 'include/gfx_format.h', 'include/pegc.h', 'gfx/gfx.h', 'gfx/palette.h', 'gfx/gfx_internal.h', 'mm/kmalloc.h', 'lib/serial_diag.h', 'lib/crc32_core.inc', 'include/kprintf.h', 'include/config.h', 'userland/tests/hw/trident_mode.hwb', 'drivers/char/serial.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_generations.h'])
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
DIR = 'drivers/video/trident/'
SOURCES = ('lifecycle.c','regs.c','probe.c','state_save.c','pc98_init.c',
           'mode_640.c','route_ra266.c','vram.c','default_dac.c','state_restore.c',
           'present.c','palette.c')
WRAPS = ('identify','capacity_plan','state_save','pc98_init','mode_640',
         'route_enable','vram_verify','state_restore','route_disable','crtc_lock','regs_bind')
FLAGS = ['-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
         '-ffreestanding','-fno-pie','-fno-stack-protector','-fno-builtin']
# name, file, unique old, replacement, runtime rejection.
MUTATIONS = (
 ('M_CLIENT_STALE','backend_trident.c','values[i] = src[i];',
  'values[i] = src[i] & 0;','CLIENT samples reflect current RAM'),
 ('M_CLIENT_SHORT','backend_trident.c','row < sizeof(offsets) / sizeof(offsets[0])',
  'row + 1 < sizeof(offsets) / sizeof(offsets[0])','central CLIENT sample included'),
 ('M_DAC_SKIP','backend_trident.c','    palette_before_present();',
  '    if (register_entry == 0) palette_before_present();','DAC dump on each activation'),
 ('M_DAC_INDEX','backend_trident.c','TRI_DAC_READ_INDEX, 0);',
  'TRI_DAC_READ_INDEX, 1);','DAC starts at color zero'),
 ('M_DAC_MASK','backend_trident.c','TRI_DAC_MASK, &mask)',
  'TRI_DAC_DATA, &mask)','read ordinary DAC mask'),
 ('M_DAC_SHORT','backend_trident.c','i < sizeof(rgb) && !rc',
  'i + 1 < sizeof(rgb) && !rc','all 48 components observed'),

 ('M_DUMP_REPEATED','backend_trident.c','registers_pending = false;',
  'registers_pending = true;','one dump per activation'),
 ('M_DUMP_CR_SHORT','backend_trident.c','"CR", 25, 95, cr, cv',
  '"CR", 25, 94, cr, cv','all extended CRTC registers observed'),
 ('M_DUMP_AR_BLANK','backend_trident.c','i | 0x20, &ar[i]',
  'i, &ar[i]','AR sampling retains display PAS'),
 ('M_DUMP_NO_ENTER','backend_trident.c','if (!rc && !was_active)',
  'if (rc && !was_active)','EnterVT rearms diagnostic'),

 ('M_REINIT_NOOP','lifecycle.c','if (d->returning) return TRI_E_BUSY;\n    if (!d->first_init_done) {',
  'if (d->returning) return TRI_E_BUSY;\n    if (d->active) return 0;\n    if (!d->first_init_done) {','active ScreenInit reapplies mode/palette/client'),
 ('M_SAVE_AFTER_INIT','lifecycle.c',
  'STAGE(TRI_STEP_SAVE, trident_state_save(d));\n    d->init_done = false;\n    STAGE(TRI_STEP_PC98_INIT, trident_pc98_init(d));',
  'STAGE(TRI_STEP_PC98_INIT, trident_pc98_init(d));\n    d->init_done = false;\n    STAGE(TRI_STEP_SAVE, trident_state_save(d));','boot Save must precede Init'),
 ('M_REINIT_SKIP_PC98','lifecycle.c',
  'STAGE(TRI_STEP_PC98_INIT, trident_pc98_init(d));',
  'if (!d->first_init_done) STAGE(TRI_STEP_PC98_INIT, trident_pc98_init(d));',
  'second ScreenInit includes full PC98 Init write trace'),
 ('M_PLAN_LATE','lifecycle.c',
  'STAGE(TRI_STEP_CAPACITY_PLAN, trident_capacity_plan(d));',
  'if (d->init_done) STAGE(TRI_STEP_CAPACITY_PLAN, trident_capacity_plan(d));','PreInit capacity before Init'),
 ('M_INIT_DONE','lifecycle.c','d->init_done = true;','d->init_done = false;','verify requires successful Init'),
 ('M_REPROBE_ON_INIT','lifecycle.c','if (!d->first_init_done) {\n        d->scratch',
  'if (d->first_init_done || !d->first_init_done) {\n        d->scratch','verify/alloc exactly once'),
 ('M_RESAVE_ON_ENTER','lifecycle.c','int rc = mode_enable(d);\n    if (!rc) d->step',
  'int rc = trident_state_save(d);\n    if (!rc) rc = mode_enable(d);\n    if (!rc) d->step','no Save call on EnterVT'),
 ('M_CLOSE_ORDER','lifecycle.c','first = disable(d);\n        second = restore_lock(d);',
  'first = restore_lock(d);\n        second = disable(d);','CloseScreen order'),
 ('M_ACTIVE_EARLY','lifecycle.c','restore = trident_state_restore(d);',
  'd->active = false; restore = trident_state_restore(d);','Restore retains vtSema'),
 ('M_UNSAVED_LOCK','lifecycle.c','int lock = trident_crtc_lock(d, d->cur_vga_base, true);',
  'int lock = d->saved.valid ? trident_crtc_lock(d, d->cur_vga_base, true) : 0;','lock despite incomplete Save'),
 ('M_SKIP_LOCK','lifecycle.c','int lock = trident_crtc_lock(d, d->cur_vga_base, true);',
  'int lock = restore ? 0 : trident_crtc_lock(d, d->cur_vga_base, true);','lock despite restore failure'),
 ('M_SKIP_DISABLE','lifecycle.c','second = disable(d);','second = first ? 0 : disable(d);','Disable despite Restore failure'),
 ('M_DROP_RC','lifecycle.c','d->last_rc = rc;','d->last_rc = 0;','first error latch/HAL query'),
 ('M_NO_RECURSION','lifecycle.c','if (d->returning) return TRI_E_BUSY;\n    if (!d->active)',
  'if (d->returning) return TRI_OK;\n    if (!d->active)','recursive return BUSY'),
 ('M_PALETTE_WIDTH','lifecycle.c','trident_palette_dac6(sys[i].r, GFX_PALETTE_COMPONENT_BITS)',
  'sys[i].r','system DAC conversion'),
 ('M_ARM_LATE','backend_trident.c','rc = trident_context->arm(trident_restore98, &device, device.display_owner);',
  'rc = 0; if (device.active) rc = trident_context->arm(trident_restore98, &device, device.display_owner);','arm before indexed writes'),
 ('M_HAL_DROP_PRESENT','backend_trident.c','if (rc) { finish(TRI_STEP_ACTIVE, rc, true); return; }',
  'if (rc) { return; }','present error exposed by query'),
 ('M_HAL_DROP_PALETTE','backend_trident.c','if (rc) { finish(TRI_STEP_CLIENT, rc, true); return; }',
  'if (rc) { return; }','palette error retained'),
 ('M_HAL_PROBE','backend_trident.c','return !device.last_rc && device.pinned',
  'return device.pinned','failed probe rejects'),
 ('M_HAL_QUERY','backend_trident.c','if (device.last_rc) return device.last_rc;\n    if (!info)',
  'if (!info)','query returns original error'),
 ('M_HAL_CLEANUP','backend_trident.c','int recovery = trident_restore98(&device);',
  'int recovery = device.active ? 0 : trident_restore98(&device);','HAL initialization failure restores'),
)

def fixture():
    s=(ROOT/'tools/tests/trident_pc98init_host.c').read_text()
    macros=s[s.index('#define EVENT('):s.index('#define LENGTH(')]
    out=macros+'\n'
    for mono in (0,1):
        values=dict(misc='f.regs[TRI_MISC_WRITE]',seq='f.sr[1]',
                    old='f.sr[0x0B]',ext='f.sr[0x0E]',config='f.sr[0x0C]',video=0,
                    base=0x3B4 if mono else 0x3D4,mono=mono,cr11='f.cr_mono[0x11]' if mono else 'f.cr_color[0x11]')
        out+=''.join(f'#define {k} {v}\n' for k,v in values.items())
        out+=f'const struct trident_fake_event init_{"mono" if mono else "color"}[] = {{PREFIX, CONFIG_ALTERNATE, SUFFIX}};\n'
        out+=''.join(f'#undef {k}\n' for k in values)
    return out

class Trace:
    def __init__(self, events):
        self.events=events
        self.pos=0
    def access(self, write, width, off, value=None, space=0, count=1):
        assert self.pos<len(self.events), ('short trace',off,self.pos)
        e=self.events[self.pos]; self.pos+=1
        s,p,v,n,w,wr,rc=e
        assert (s,p,n,w,wr,rc)==(space,off,count,width,write,0),(self.pos,e,(space,off,count,width,write))
        if value is not None: assert v==value,(self.pos,hex(off),v,value)
        return v
    def done(self):
        assert self.pos==len(self.events),('extra trace',self.pos,len(self.events))

def script(label, trace, env):
    lines=(ROOT/'userland/tests/hw/trident_mode.hwb').read_text().splitlines()
    labels={s.strip()[6:]:i for i,s in enumerate(lines) if s.strip().startswith('label ')}
    pc=labels[label]+1; stack=[]
    def val(s): return eval(s,{'__builtins__':{}},env)
    def assign(s,v):
        if '[' in s:
            name,index=s[:-1].split('['); env[name][val(index)]=v
        else: env[s]=v
    while True:
        line=lines[pc].strip(); number=pc+1; pc+=1
        if not line or line.startswith(('#','label ','print ')): continue
        if line.startswith('dim '):
            name,size=line[4:].split(); env[name]=[0]*int(size)
        elif line.startswith('let '):
            name,expr=line[4:].split(' = '); assign(name,val(expr))
        elif line.startswith('call '):
            stack.append(pc); pc=labels[line[5:]]+1
        elif line=='return':
            if not stack: break
            pc=stack.pop()
        elif line.startswith('goto '): pc=labels[line[5:]]+1
        elif line.startswith('if '):
            cond,target=line[3:].split(' goto ')
            if val(cond): pc=labels[target]+1
        else:
            m=re.fullmatch(r'mmio(write|read)(8|16) v \+ (0x[0-9A-Fa-f]+|cb|\([^)]*\)) (.*)',line)
            if m:
                kind,bits,address,expr=m.groups(); off=val(address); width=int(bits)//8
                if kind=='read': assign(expr[3:],trace.access(False,width,off))
                elif width==2 and 1539<=number<=1550:
                    value=val(expr); trace.access(True,1,off,value&255); trace.access(True,1,off+1,value>>8)
                else: trace.access(True,width,off,val(expr)&((1<<int(bits))-1))
            else:
                m=re.fullmatch(r'iowrite8 port (0x[0-9A-Fa-f]+) (.*)',line)
                assert m,(number,line)
                trace.access(True,1,val(m[1]),val(m[2]),space=2)
    trace.done()

def mode_oracle():
    base, reference, seed, misc = 0x3D4, 0, 0, 0xC3
    """Explicit reference operation order; no product parser or device model."""
    out = []
    def w(p, v, width=1):
        out.append((p, v, width, 1))
    def r(p, v):
        out.append((p, v, 1, 0))
    def rd(p, i, v):
        w(p, i)
        r(p + 1, v)
    def pair(p, i, v):
        w(p, i)
        w(p + 1, v)
    def ext(p, i, v):
        w(p, i | v << 8, 2)
    inactive = misc is not None and bool(misc & 1) != (base == 0x3D4)
    cr = lambda i: 0x92 if i == 0x11 else 0
    sr = lambda i: 0
    gr = lambda i: 0
    # ModeInit unlock, then TridentInit's reads (including unused video reads).
    rd(base, 0x11, cr(0x11))
    pair(base, 0x11, cr(0x11) & 127)
    rd(0x3C4, 0x0B, sr(0x0B))
    rd(0x3CE, 0x0F, gr(0x0F))
    rd(base, 0xC0, reference)
    rd(base, 0x2A, cr(0x2A))
    rd(base, 0x2F, cr(0x2F))
    w(base, 0x3A)
    rd(base, 0x29, cr(0x29))
    rd(0x3CE, 0x2F, gr(0x2F))
    rd(base, 0x39, cr(0x39))
    rd(0x3C4, 0x20, sr(0x20))
    rd(0x3C4, 0x12, sr(0x12))
    # Protect, followed by vgaHWRestoreMode. Sequencer image is unmodified 01.
    rd(0x3C4, 1, sr(1))
    pair(0x3C4, 0, 1)
    pair(0x3C4, 1, sr(1) | 32)
    r(base + 6, 0x5A)
    w(0x3C0, 0)
    w(0x3C2, 0xE3)
    for i, v in enumerate((1, 15, 0, 14), 1):
        pair(0x3C4, i, v)
    pair(0x3D4, 17, 0x2C)
    # Full vgaHWInit table; standard pitch guess precedes Trident Offset.
    crt = (95, 79, 79, 128, 82, 30, 11, 62, 0, 64, 0, 0, 0, 0, 0, 0,
           234, 44, 223, 40, 0, 223, 11, 195, 255)
    for i, v in enumerate(crt):
        pair(0x3D4, i, v)
    for i, v in enumerate((0, 0, 0, 0, 0, 64, 5, 15, 255)):
        pair(0x3CE, i, v)
    r(0x3DA, 0x5A)
    w(0x3C0, 0)
    for i, v in enumerate((*range(16), 65, 0, 15, 0, 0)):
        r(0x3DA, 0x5A)
        w(0x3C0, i)
        w(0x3C0, v)
    r(0x3DA, 0x5A)
    w(0x3C0, 32)
    # TridentRestore; word accesses must remain single trace events.
    rd(0x3C4, 0x0B, sr(0x0B))
    ext(0x3C4, 14, 194)
    r(0x3C8, 1)
    for _ in range(4):
        r(0x3C6, 255)
    w(0x3C6, 0)
    r(0x3C8, 1)
    for p, i, v in (
        (0x3D4, 0x1E, 128), (0x3D4, 0x21, 32), (0x3C4, 13, 32),
        (0x3D4, 0x50, 0), (0x3D4, 0x27, 8), (0x3D4, 0x2B, 0),
        (0x3D4, 0x29, cr(0x29) & 207), (0x3D4, 0x36, 128),
        (0x3D4, 0x2F, cr(0x2F) | 16), (0x3D4, 0x2A, cr(0x2A) | 64),
        (0x3D4, 0x3A, 0), (0x3D4, 0x38, 0),
        (0x3D4, 0x39, (cr(0x39) & 249) | 6), (0x3D4, 0x55, 31),
        (0x3CE, 0x2F, gr(0x2F) | 4),
        (0x3CE, 15, (gr(15) & 240) | 2), (0x3D4, 19, 80)):
        ext(p, i, v)
    a, b = (164, 14) if reference & 128 else (229, 30)
    w(0x43C8, a)
    w(0x43C9, b)
    w(0x3C2, 235)
    ext(0x3C4, 14, 194)
    rd(0x3C4, 1, 1)
    pair(0x3C4, 1, 1)
    pair(0x3C4, 0, 3)
    r(0x3DA, 0x5A)
    w(0x3C0, 32)
    return out

def check_trace(data):
    event=struct.Struct('<IIIIBB2xi'); pos=0; stages=[]; env={}
    while pos<len(data):
        step,count=struct.unpack_from('<II',data,pos); pos+=8
        events=[event.unpack_from(data,pos+i*event.size) for i in range(count)]
        pos+=count*event.size; stages.append(step); trace=Trace(events)
        if step==4:
            trace.access(True,1,0x3C4,9); trace.access(False,1,0x3C5,0x10); trace.done()
        elif step==7:
            trace.access(True,1,0x3D4,0x1F); trace.access(False,1,0x3D5,0x7F); trace.done()
        elif step==5: script('state_save',trace,env)
        elif step==6:
            # C wrapper compares every read/write/value/width against the
            # independent MIT Init fixture before opening shared fake's gate.
            assert count>50
        elif step==8:
            for off,v,width,write in mode_oracle(): trace.access(write,width,off,v)
            trace.done()
        elif step==9: script('enable',trace,env)
        elif step==10: assert not count  # LFB trace disabled; full verifier actually ran
        elif step==11:
            palette=[((i<<2)|(i>>2),)*3 for i in range(16)]
            palette += [(r*63//5,g*63//5,b*63//5) for r in range(6) for g in range(6) for b in range(6)]
            palette += [(((8+i*10)*63+127)//255,)*3 for i in range(24)]
            for i,color in enumerate(palette):
                for port,value in [(0x3C6,255),(0x3C8,i),*((0x3C9,v) for v in color)]:
                    trace.access(True,1,port,value)
                    trace.access(False,1,0x3DA,0x5A); trace.access(False,1,0x3DA,0x5A)
            trace.done()
        elif step==12:
            # Read-only diagnostic immediately before the first LFB write.
            for port,first,last in [(0x3C4,0,15),(0x3D4,0,24),
                                    (0x3D4,25,95),(0x3CE,0,15)]:
                selector=trace.access(False,1,port)
                for index in range(first,last+1):
                    trace.access(True,1,port,index)
                    trace.access(False,1,port+1)
                trace.access(True,1,port,selector)
            selector=trace.access(False,1,0x3C0)
            for index,value in zip(range(16,21),[0x41,0,15,0,0]):
                trace.access(False,1,0x3DA,0x5A)
                trace.access(True,1,0x3C0,index|32)
                trace.access(False,1,0x3C1,value)
            trace.access(False,1,0x3DA,0x5A)
            trace.access(True,1,0x3C0,selector)
            trace.access(False,1,0x3DA,0x5A)
            trace.access(False,1,0x3CC,0xEB)
            dac_selector=trace.access(False,1,0x3C8)
            trace.access(False,1,0x3C6,255)
            trace.access(True,1,0x3C7,0)
            for i in range(16):
                for _ in range(3):
                    trace.access(False,1,0x3C9,(i<<2)|(i>>2))
                    trace.access(False,1,0x3DA,0x5A)
                    trace.access(False,1,0x3DA,0x5A)
            trace.access(True,1,0x3C8,dac_selector)
            # Every LFB byte, including the final byte, must use write8.
            # Full H-equivalent bitmap is also checked in the C harness.
            for off in range(640*480): trace.access(True,1,off,space=1,count=1)
            for off in range(640*480): trace.access(False,1,off,space=1,count=1)
            for x,y in [(0,0),(320,240),(32,30),(32,90),(32,150),
                        (32,210),(32,270),(32,350),(340,260)]:
                trace.access(False,1,y*640+x,space=1,count=1)
            trace.done()
        elif step==15: script('disable',trace,env)
        elif step==13:
            trace.access(False,1,0x3CC)  # vgaIOBase selection, vgaHW:845
            script('state_restore',trace,env)
        elif step==14:
            trace.access(True,1,0x3D4,0x11)
            v=trace.access(False,1,0x3D5)
            trace.access(True,1,0x3D4,0x11); trace.access(True,1,0x3D5,v|128); trace.done()
        else: raise AssertionError(step)
    assert pos==len(data)
    assert stages==[4,7,5,6,8,9,10,11,12,15,13,14],stages

def build(work, changed=None):
    exe=work/'lifecycle'
    (work/'backend_under_test.c').write_text(
        changed[1] if changed and changed[0]=='backend_trident.c' else
        (ROOT/DIR/'backend_trident.c').read_text())
    sources=[]
    for name in SOURCES:
        if changed and name==changed[0]:
            p=work/name; p.write_text(changed[1]); sources.append(str(p))
        else: sources.append(str(ROOT/DIR/name))
    subprocess.run(['gcc',*FLAGS,'-I'+str(work),'-I'+str(ROOT),
                    '-I'+str(ROOT/DIR),*include_flags(ROOT),
                    '-nostdlib','-static','-no-pie',
                    str(ROOT/'tools/tests/trident_lifecycle_host.c'),
                    str(ROOT/'tools/tests/trident_device_fake.c'),*sources,
                    *('-Wl,--wrap=trident_'+n for n in WRAPS),'-o',str(exe)],check=True)
    return exe

def execute(exe, runner, red=False):
    result=host32.run([str(exe)],runner=runner,capture_output=True)
    if red:
        rejected=result.returncode==1 and b'RED lifecycle ' in result.stdout
        if result.returncode==0:
            try: check_trace(result.stdout)
            except (AssertionError,struct.error): rejected=True
        assert rejected,(result.returncode,result.stdout[-200:])
    else:
        assert result.returncode==0,(result.returncode,result.stdout[-200:])
        check_trace(result.stdout)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate',action='store_true'); args=parser.parse_args()
    runners=os.environ.get('HOST32_RUNNERS','qemu').split(); assert runners
    with tempfile.TemporaryDirectory(prefix='trident-life-') as tmp:
        work=Path(tmp); (work/'lifecycle_expected.h').write_text(fixture())
        cross=Path(os.environ.get('CROSS_DIR','/home/hight/opt/cross'))/'bin/i386-elf-gcc'
        for name in ('lifecycle.c','backend_trident.c'):
            subprocess.run([str(cross),*FLAGS,'-I'+str(ROOT),*include_flags(ROOT),
                            '-c',str(ROOT/DIR/name),'-o',str(work/(name+'.o'))],check=True)
        exe=build(work)
        for runner in runners:
            execute(exe,runner)
            print(f'GREEN lifecycle/HAL/real-regs assembly and script/reference trace ({runner})',flush=True)
        if args.mutate:
            for name,path,old,new,rejects in MUTATIONS:
                s=(ROOT/DIR/path).read_text(); assert s.count(old)==1,(name,s.count(old))
                exe=build(work,(path,s.replace(old,new)))
                for runner in runners: execute(exe,runner,red=True)
                print(f'MUTATE RED {name}: {rejects} (compiled, runtime)',flush=True)
    return 0

def run(ctx):
    env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3','-B','tools/tests/test_trident_lifecycle.py',
                            *(['--mutate'] if ctx.mutate else [])],cwd=ctx.root,env=env)
if __name__=='__main__': raise SystemExit(main())
