#!/usr/bin/env python3
"""PPC P3 A-6: link-SH acceptance; existing HostAgent, RSHELL and QMP only.

Evidence is isolated below --out. Socket denial returns 3 (未), never PASS.
The normal control precedes every mutation. Unexpected timeout/build/QEMU
failures are errors, not mutation REDs. No physical agent state is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tools/ppc')]
from run import ROM, command, require, CriterionFailure
from link_run import PumpQMP
from kboot_accept import build
from ppc_lan_host import UDP
from host_agent import HostAgent
from host_operator_socket import OperatorSocket
from host_rshell import RSHELL, EXECUTE
from usb_run import USB_QEMU_CLOCK

USER = 'userland/shell/rshell_lan.c'
SINK = 'kernel/console/con_sink.c'
HEAP = 'mm/kmalloc.c'
BOOT = 'platform/powermac/shell_boot.c'
# Each anchor is checked against the complete source, before observation hooks.
MUTATIONS = {
    'term-wait': (BOOT, '#if !KBOOT_LINK\n    if (pm_console_text_ready())',
                  '#if KBOOT_LINK\n    if (pm_console_text_ready())', 'autostart'),
    'ready-retry': (USER, 'if (!attempted || (u32)(g_api->get_tick() - ready_tick) >= RSH_READY_TICKS)',
                    'if (!attempted)', 'ready-retry'),
    'duplicate': (USER, 'if (have_id && id <= last_id) return OS32_ERR_EXIST;',
                  '/* duplicate guard removed */', 'duplicate'),
    'print-twice': ('kernel/console/console.c', '    con_sink_push_print(buf, size, color);',
                    '    con_sink_push_print(buf, size, color); con_sink_push_print(buf, size, color);', 'output'),
    'drop-compare': (SINK, 'if (con_sink_drop_count != g_capture_drop_start) rc = OS32_ERR_NOSPC;',
                     '/* drop comparison removed */', 'freeze-drop'),
    'end': (USER, '    g_api->con_sink_capture(CON_SINK_CAPTURE_END);',
             '    /* END removed */', 'end'),
    'owner': (SINK, '    if (g_reader == id) {',
               '    if (0) {', 'owner'),
    'fault-break': (BOOT, 'if (kind != EXEC_KIND_EXITED && kind != EXEC_KIND_FAULT) break;',
                     'if (kind != EXEC_KIND_EXITED) break;', 'fault-restart'),
    'half-line': (USER, '        if (ln.bytes == 0) {', '        if (1) {', 'half-line'),
}


def replace_once(text, before, after):
    require(text.count(before) == 1, 'unique anchor: '+before)
    return text.replace(before, after)


def check_map(path):
    text = path.read_text()
    absent = ('hostfs_io', 'hostfs_peer', 'hostfs_boot', 'lan_wire', 'lan_terminal', 'terminal_input')
    require(all(not re.search(r'[/ ]'+name+r'\.o\b', text) for name in absent), 'map old TU')


def check_body(line, body, expected):
    require(body == b'> '+line.encode()+b'\n'+expected+b'\x04', 'output')


def check_heap(peak, failures):
    require(0 < peak < 1024*1024 and failures == 0, 'heap')


def settings(work, mutation=None):
    """Private copies add read-only meters and USER-only fault/output stimuli.

    The meters do not send new protocol messages or change capture ownership.
    All observations are read by the existing QMP memory mechanism.
    """
    sources = {p: (ROOT/p).read_text() for p in (USER, SINK, HEAP)}
    if mutation:
        path, before, after, _ = MUTATIONS[mutation]
        sources[path] = replace_once(sources.get(path, (ROOT/path).read_text()), before, after)
    if mutation == 'term-wait':
        sources[BOOT] = ('int pm_term_connected(void) { return 0; }\n'
            'int pm_terminal_trygetkey(void) { return -1; }\n'
            + sources[BOOT])
        sources[BOOT] = sources[BOOT].replace('pm_terminal_poll();', 'idle_work_run();')
    sources[USER] = replace_once(sources[USER], '    (void)execute_command(line);', r'''
    /* Acceptance stimuli live only in this generated USER copy. */
    if (!strcmp(line, "p3-fault")) { __asm__ volatile("trap"); }
    if (!strcmp(line, "p3-output")) {
        const char text[] = "FD1 UTF8 \xe3\x81\x82\n";
        g_api->tvram_clear();
        g_api->sys_write(1, text, sizeof(text)-1);
    } else if (!strcmp(line, "p3-overflow")) {
        for (int i=0; i<200; i++) g_api->kprintf(7, "%s", "01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
    } else (void)execute_command(line);''')
    sources[SINK] = replace_once(sources[SINK], 'int con_sink_capture(unsigned int op)',
        'volatile int p3_freeze_rc;\nvolatile unsigned p3_end_count;\nint con_sink_capture(unsigned int op)')
    # This return belongs to capture, not another sink operation.
    start = sources[SINK].index('int con_sink_capture(unsigned int op)')
    end = sources[SINK].index('\nint con_sink_capture_active', start)
    part = sources[SINK][start:end]
    part = replace_once(part, '    con_sink_unlock(f);\n    return rc;',
        '    if (op == CON_SINK_CAPTURE_FREEZE) p3_freeze_rc = rc;\n'
        '    if (op == CON_SINK_CAPTURE_END && !rc) p3_end_count++;\n'
        '    con_sink_unlock(f);\n    return rc;')
    sources[SINK] = sources[SINK][:start]+part+sources[SINK][end:]
    sources[HEAP] = replace_once(sources[HEAP], 'void *kmalloc(u32 size)',
                                 'volatile unsigned p3_alloc_failures;\nvoid *kmalloc(u32 size)')
    sources[HEAP] = replace_once(sources[HEAP], '    void *p = kheap_alloc(&kernel_heap, size);',
        '    void *p = kheap_alloc(&kernel_heap, size);\n    if (!p && size) p3_alloc_failures++;')
    user = work/'rshell_lan.c'; user.write_text(sources.pop(USER))
    user_out = work/'user'
    # kb-user recursively uses these command-line variables. Separate object
    # trees ensure no mutant can reuse a normal USER object (or vice versa).
    opts = ['KBOOT_SH=1', 'KBOOT_LINK=1', 'KB_SH_OUT='+str(user_out),
            'PPC_SH_MUT_SRC='+USER, 'PPC_SH_MUT_FILE='+str(user),
            'KB_ROOT_BINS='+str(user_out/'hello.bin')+' '+str(user_out/'fault.bin')]
    require(len(sources) <= 3, 'kernel mutation slots')
    for slot, (path, content) in zip(('', 'EXTRA_', 'THIRD_'), sources.items()):
        dest = work/(Path(path).stem+'-meter.c'); dest.write_text(content)
        opts += ['KB_MUT_'+slot+'SRC='+path, 'KB_MUT_'+slot+'FILE='+str(dest)]
    return opts


class Session:
    def __init__(self, elf, cpu, out, timeout, late=False):
        self.out, self.elf, self.cpu = out, elf, cpu
        out.mkdir()
        self.deadline = time.monotonic()+timeout
        self.q = self.p = self.io = self.operator = self.err = self.frames = None
        self.events = []
        self.started = time.monotonic()
        self.agent_enabled = not late
        self.symbols = {p[2]: int(p[0],16) for line in command([cross('nm'), '-n', elf]).splitlines()
                        if len(p := line.split()) == 3}
        self.new_agent()

    def new_agent(self):
        if self.operator: self.operator.close()
        directory = self.out/('agent-'+str(len(self.events))); directory.mkdir()
        self.agent = HostAgent(bytes.fromhex('02005e000001'), state_dir=str(directory),
                               quiet=True, allow_net=False, clip='none')
        self.operator = OperatorSocket(('127.0.0.1',0), self.agent.operator, lambda: {})
        self.agent.operator_socket = self.operator
        service = self.agent._service_rshell
        def observed(rid, ent, parts, out):
            self.events.append(dict(rid=rid, parts=list(parts), body=bytes(ent['got']).hex(), at=time.monotonic()))
            return service(rid, ent, parts, out)
        self.agent._service_rshell = observed

    def start(self):
        cd = self.out/'cd'; cd.mkdir(); shutil.copyfile(self.elf, cd/'PPCKERN.ELF')
        iso = self.out/'boot.iso'
        command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1',0)); port=reservation.getsockname()[1]
            self.io = UDP(f'127.0.0.1:0,127.0.0.1:{port}')
        args = ['qemu-system-ppc','-M','mac99','-cpu',self.cpu,'-m','128M','-smp','1',
            '-accel','tcg','-bios',str(ROM),'-cdrom',str(iso),'-boot','d',
            '-prom-env','boot-device=cd:,\\PPCKERN.ELF','-prom-env','auto-boot?=true',
            '-g','800x600x8','-display','none','-serial','file:'+str(self.out/'serial-a.log'),
            '-serial','file:'+str(self.out/'serial-b.log'),'-monitor','none','-qmp','stdio','-no-reboot',
            '-nic','none','-device','sungem,netdev=n0,mac=52:54:00:12:34:56','-netdev',
            f'socket,id=n0,udp=127.0.0.1:{self.io.sock.getsockname()[1]},localaddr=127.0.0.1:{port}',
            '-usb','-device','usb-kbd,id=kbd',*USB_QEMU_CLOCK]
        (self.out/'metadata.json').write_text(json.dumps(dict(command=args,cpu=self.cpu,
            elf_sha256=hashlib.sha256(self.elf.read_bytes()).hexdigest()),indent=2))
        self.err=(self.out/'qemu.stderr').open('w'); self.frames=(self.out/'frames.log').open('w')
        self.p=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.err,bufsize=0)
        self.q=PumpQMP(self.p,self.deadline,self.pump)

    def send(self, frame):
        self.frames.write('> '+bytes(frame).hex()+'\n'); self.io.send(frame)

    def pump(self):
        if self.p.poll() is not None: return
        while select.select([self.io],[],[],0)[0]:
            frame=self.io.receive(); self.frames.write(frame.hex()+'\n')
            if self.agent_enabled: self.agent.handle(frame,self)
        if self.agent_enabled: self.agent.tick(self)
        for fd in select.select(self.operator.fileno_list(),[],[],0)[0]: self.operator.on_readable(fd)
        self.operator.poll(time.monotonic())

    def wait(self, predicate, label, seconds=20):
        until=min(self.deadline,time.monotonic()+seconds)
        while time.monotonic()<until:
            if self.p.poll() is not None: raise RuntimeError('QEMU stopped before '+label)
            self.pump()
            if predicate(): return
            time.sleep(.005)
        raise RuntimeError('deadline '+label+' evidence='+str(self.out))

    def settle(self, seconds):
        until=time.monotonic()+seconds
        self.wait(lambda: time.monotonic()>=until,'settle',seconds+1)

    def word(self, name, offset=0):
        require(name in self.symbols,'missing symbol '+name)
        value=self.q.call('human-monitor-command',{'command-line':f'x /1wx 0x{self.symbols[name]+offset:x}'})
        match=re.search(r':\s*0x([0-9a-fA-F]+)',value)
        require(match is not None,'unreadable '+name)
        return int(match[1],16)

    def ready(self):
        return self.agent.established and self.agent.operator.ready is not None

    def cmd(self, line, expected=None):
        op=self.agent.operator
        ended=self.word('p3_end_count')
        ident=op.submit('cui',line,'accept',timeout_ms=20000)
        def outputs():
            return [e for e in self.events if e['parts'][:2]==['RSH','OUT'] and e['parts'][3]==ident]
        # Inspect the wire result even when Operator rejects its command echo.
        # Otherwise PRINT duplication would become a generic result timeout.
        self.wait(lambda: bool(outputs()),'OUT '+line)
        if expected is not None: check_body(line,bytes.fromhex(outputs()[0]['body']),expected)
        self.wait(lambda: ident in op.results or (op.pending is not None and
                  op.pending['state'] in ('done','truncated')),'result '+line)
        result=op.results.get(ident) or op.take_result()
        first=self.word('tick_count')
        self.wait(lambda: self.word('tick_count')-first>=100,'END interval',10)
        events=[e for e in self.events if e['parts'][:2]==['RSH','OUT'] and e['parts'][3]==ident]
        require(len(events)==1,'one OUT')
        if expected is not None: check_body(line,result['result'],expected)
        require(self.word('g_capture')==0 and self.word('g_reader')==0xffffffff and
                self.word('rshell_active')==1 and self.word('boot_serial_mirror')==0 and
                self.word('p3_end_count')==ended+1,'end')
        return result

    def key(self, code):
        self.q.call('send-key',{'keys':[{'type':'qcode','data':code}],'hold-time':100})
        self.settle(.18)

    def keys(self, text):
        for c in text: self.key('ret' if c=='\n' else 'spc' if c==' ' else c)

    def screen(self, name):
        # PPC fbcon's cell shadow is the input to the framebuffer renderer.
        # Offsets are pinned to fbcon.h by the host test below.
        cells=self.word('terminal',8)
        cols, rows=self.word('terminal',16),self.word('terminal',20)
        require(0<cols<=200 and 0<rows<=100,'fbcon geometry')
        dump=self.out/(name+'.cells')
        self.q.call('stop')
        try:
            self.q.call('human-monitor-command',{'command-line':f'pmemsave 0x{cells:x} {cols*rows*4} "{dump}"'})
            self.q.call('screendump',{'filename':str(self.out/(name+'.ppm'))})
        finally: self.q.call('cont')
        data=dump.read_bytes()
        return '\n'.join(''.join(chr(struct.unpack_from('>H',data,(y*cols+x)*4)[0])
                        for x in range(cols)) for y in range(rows))

    def close(self):
        (self.out/'events.json').write_text(json.dumps(self.events,indent=2))
        if self.p and self.p.poll() is None:
            self.p.terminate()
            try: self.p.wait(timeout=5)
            except subprocess.TimeoutExpired: self.p.kill(); self.p.wait()
        if self.operator: self.operator.close()
        if self.io: self.io.sock.close()
        if self.q: self.q.close()
        if self.err: self.err.close()
        if self.frames: self.frames.close()


def cross(tool):
    return str(Path(os.environ.get('PPC_CROSS_DIR','/home/hight/opt/cross-ppc'))/'bin'/('powerpc-elf-'+tool))


def accept(elf,cpu,out,timeout,late=False,verdict=None):
    s=Session(elf,cpu,out,timeout,late)
    try:
        s.start()
        if late:
            s.wait(lambda: s.word('rshell_active')==1,'APP before delayed agent',60)
            first=s.word('tick_count')
            s.wait(lambda: s.word('tick_count')-first>=300,'late agent wait',30)
            s.agent_enabled=True
        if verdict in ('autostart', 'ready-retry'):
            s.wait(lambda: s.agent.established, 'HELLO established')
            first=s.word('tick_count')
            s.wait(lambda: s.word('tick_count')-first>=600,'startup tick progress',30)
            require(s.ready(),verdict)
        else: s.wait(s.ready,'HELLO to cui READY',60)
        require(s.agent.operator.ready['mode']=='cui','autostart')
        s.cmd('echo hello',b'hello\n')
        ready_count=lambda: sum(e['parts'][:2]==['RSH','READY'] for e in s.events)
        count=ready_count(); first=s.word('tick_count')
        s.wait(lambda: s.word('tick_count')-first>=500,'READY retry interval',30)
        require(ready_count()>count,'ready-retry')
        s.cmd('cat /no-such-file')
        failed=s.cmd('echo $?')['result']
        require(re.fullmatch(rb'> echo \$\?\n[1-9][0-9]*\n\x04',failed) is not None,'failure status')
        s.cmd('exit 3',b''); s.cmd('echo afterexit',b'afterexit\n')
        # Bypass host line validation: the guest must reject these notices too.
        before=len(s.events); ident=s.agent.operator.next_id-1
        for data in (f'{ident:08x} echo duplicate\n'.encode(),b'00000001 echo old\n',
                     b'00000100 '+b'x'*127+b'\n',b'00000101 echo bad\necho bad\n'):
            s.agent.notifier.notify_number(RSHELL,EXECUTE,data)
            s.settle(.5)
        require(not any(e['parts'][:2]==['RSH','OUT'] for e in s.events[before:]),'duplicate')
        s.cmd('echo accepted',b'accepted\n')
        s.cmd('p3-output',b'FD1 UTF8 \xe3\x81\x82\n')
        screen=s.screen('utf8')
        require('> p3-output' not in screen and 'FD1 UTF8' in screen,'clear/FB')
        require(chr(0x2422) in screen,'UTF8 FB glyph')
        require('KBOOT' not in screen,'LOG mixed into FB')
        s.keys('echo usb\n')
        screen=s.screen('usb')
        require('> echo usb' in screen and re.search(r'(?m)^usb *$',screen),'USB line screen')
        s.keys('echo half')
        op=s.agent.operator; op.submit('cui','echo deferred','accept')
        s.settle(1)
        require(op.pending['result'] is None,'half-line')
        s.key('ret')
        s.wait(lambda: op.pending['state']=='done','deferred command')
        result=op.take_result(); check_body('echo deferred',result['result'],b'deferred\n')
        s.wait(lambda: s.word('g_capture')==0,'deferred END',5)
        screen=s.screen('deferred')
        require('> echo half' in screen and '> echo deferred' in screen and
                screen.index('> echo half') < screen.index('> echo deferred'),'USB before LAN')
        overflow=s.cmd('p3-overflow')
        require(overflow['state']=='truncated' and overflow['drop']>0,'result drop')
        require(s.word('p3_freeze_rc')!=0,'freeze-drop')
        s.cmd('echo recovered',b'recovered\n')
        # A fault in the APP itself is distinct from a child fault.
        s.agent.notifier.notify_number(RSHELL,EXECUTE,b'00001000 p3-fault\n')
        count=ready_count(); s.settle(4)
        serial=(s.out/'serial-a.log').read_text(errors='replace')
        require('sh result kind=2' in serial,'USER fault reached')
        require(s.word('g_capture')==0 and s.word('g_reader')==0xffffffff,'owner')
        require(ready_count()>count,'fault-restart')
        s.cmd('echo afterfault',b'afterfault\n')
        generation=s.agent.host_generation
        s.new_agent()
        s.wait(s.ready,'new agent generation',30)
        require(s.agent.host_generation!=generation,'generation')
        s.cmd('echo generation',b'generation\n')
        check_heap(s.word('kmalloc_peak_bytes'),s.word('p3_alloc_failures'))
        (out/'result.json').write_text(json.dumps(dict(cpu=cpu,late=late,heap_peak=s.word('kmalloc_peak_bytes'),
            allocation_failures=s.word('p3_alloc_failures'),end_count=s.word('p3_end_count')),indent=2))
        s.q.call('quit'); s.p.wait(timeout=5); require(s.p.returncode==0,'QEMU rc')
    finally: s.close()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--timeout',type=int,default=180)
    ap.add_argument('--mutate',action='store_true')
    ap.add_argument('--build-only',action='store_true')
    args=ap.parse_args()
    required=tuple(cross(tool) for tool in ('gcc','nm','objdump','ld','objcopy'))
    if not args.build_only: required+=('qemu-system-ppc','genisoimage')
    missing=[p for p in required if not shutil.which(p)]
    if not args.build_only and not ROM.is_file(): missing.append(str(ROM))
    if missing:
        print('SKIP ppc-link-sh-run: unavailable '+', '.join(missing))
        return int(os.environ.get('OS32_REQUIRE_PPC')=='1')
    args.out.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='run-',dir=args.out.resolve()))
    print('evidence='+str(work),flush=True)
    if not args.build_only:
        try:
            for kind in (socket.SOCK_DGRAM,socket.SOCK_STREAM):
                with socket.socket(socket.AF_INET,kind) as probe: probe.bind(('127.0.0.1',0))
        except OSError as exc:
            print('未 ppc-link-sh-run: AF_INET unavailable: '+str(exc)); return 3
    control=work/'normal'; control.mkdir()
    elf=build(control/'kernel',*settings(control))
    check_map(elf.with_suffix('.map'))
    if args.build_only:
        if args.mutate:
            for name in MUTATIONS:
                folder=work/name; folder.mkdir()
                mutant=build(folder/'kernel',*settings(folder,name))
                check_map(mutant.with_suffix('.map'))
                print('BUILT mutation '+name,flush=True)
        return 0
    for cpu in ('7450','7455'):
        for late in (False,True):
            accept(elf,cpu,work/(cpu+('-late' if late else '-normal')),args.timeout,late)
            print('GREEN '+cpu+' late='+str(late),flush=True)
    if args.mutate:
        for name,(_,_,_,expected) in MUTATIONS.items():
            folder=work/name; folder.mkdir()
            mutant=build(folder/'kernel',*settings(folder,name))
            try: accept(mutant,'7450',folder/'guest',args.timeout,verdict=expected)
            except CriterionFailure as exc:
                require(str(exc)==expected,'unexpected mutation verdict '+name+': '+str(exc))
                print('RED '+name+': '+str(exc),flush=True)
            else: raise RuntimeError('SURVIVED '+name)
    return 0


if __name__=='__main__': sys.exit(main())
