#!/usr/bin/env python3
"""Resident shell observations follow shell_service.h, exec_launch.inc and exec_finish.inc.

Evidence is isolated below --out. Socket denial returns 3 (未), never PASS.
The normal control precedes every mutation. Unexpected timeout/build/QEMU
failures are errors, not mutation REDs. No physical agent state is used.
"""
import argparse
import hashlib
import json
import os
import shlex
from contextlib import contextmanager
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
from kboot_accept import build_media
from ppc_lan_host import UDP
import host_agent as wire
from host_agent import HostAgent
from host_operator_socket import OperatorSocket
from host_rshell import RSHELL, EXECUTE

USER = 'userland/shell/rshell_lan.c'
SINK = 'sys/kern/console/con_sink.c'
HEAP = 'sys/kern/mm/kmalloc.c'
BOOT = 'sys/platform/powermac/shell_loop_ops.c'
# 110 lines x 81 bytes = 8910 B, just past the 8 KiB sink; each line scrolls the
# OF framebuffer (~2.7 s under -icount), so allow ~5 minutes plus margin.
OVERFLOW_LINES = 110
OVERFLOW_SECONDS = 480
# Each anchor is checked against the complete source, before observation hooks.
MUTATIONS = {
    'command-group': ('userland/shell/main.c', '    shell_cmd_pci_init();', '', 'resident command groups'),
    'line-buffer': ('userland/shell/main.c', '    setvbuf(stdout, (char *)0, _IOLBF, BUFSIZ);',
                    '    setvbuf(stdout, (char *)0, _IOFBF, BUFSIZ);', 'line buffering'),
    'term-wait': (BOOT, '    return gui ? 2 : 0;', '    if (!gui) { for (;;) idle_wait_step(); } return 2;', 'autostart'),
    'ready-retry': (USER, 'if (!attempted || (u32)(g_api->get_tick() - ready_tick) >= RSH_READY_TICKS)',
                    'if (!attempted)', 'ready-retry'),
    'duplicate': (USER, 'if (have_id && id <= last_id) return OS32_ERR_EXIST;',
                  '/* duplicate guard removed */', 'duplicate'),
    'end': (USER, '    g_api->con_sink_capture(CON_SINK_CAPTURE_END);',
             '    /* END removed */', 'end'),

}


def replace_once(text, before, after):
    require(text.count(before) == 1, 'unique anchor: '+before)
    return text.replace(before, after)


def check_map(path):
    text = path.read_text()
    absent = ('hostfs_io', 'hostfs_peer', 'lan_wire', 'lan_terminal', 'terminal_input')
    require(all(not re.search(r'[/ ]'+name+r'\.o\b', text) for name in absent), 'map old TU')


def check_body(line, body, expected):
    require(body == b'> '+line.encode()+b'\n'+expected+b'\x04', 'output')


def check_heap(peak, failures):
    require(0 < peak < 1024*1024 and failures == 0, 'heap')


def text_fixture(work):
    text = work/'sample.txt'
    text.write_text('ASCII ｶﾅ 日本語\n', encoding='utf-8')
    return str(text.resolve())


def settings(work, mutation=None, *, observe_flow=False, overflow="lines"):
    """Private copies add read-only meters and USER-only fault/output stimuli.

    The meters do not send new protocol messages or change capture ownership.
    All observations are read by the existing QMP memory mechanism.
    """
    sources = {p: (ROOT/p).read_text() for p in (USER, SINK, HEAP)}
    if mutation:
        path, before, after, _ = MUTATIONS[mutation]
        sources[path] = replace_once(sources.get(path, (ROOT/path).read_text()), before, after)
    if overflow == 'cr':
        stimulus = r'''        for (int i = 0; i < 512; i++)
            g_api->kprintf(7, "%s", "0123456789abcdef\r");
        g_api->kprintf(7, "%s", "p3-cr-tail");'''
    else:
        stimulus = r'''        for (int i=0; i<P3_OVERFLOW_LINES; i++) g_api->kprintf(7, "%s", "01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");'''.replace('P3_OVERFLOW_LINES', str(OVERFLOW_LINES))
    sources[USER] = replace_once(sources[USER], '        accepting = rsh_lan_execute(line);', r'''
    /* Acceptance stimuli live only in this generated USER copy. */
    if (!strcmp(line, "p3-fault")) { __asm__ volatile("trap"); }
    if (!strcmp(line, "p3-buffer")) {
        printf("buffered line\n");
    } else if (!strcmp(line, "p3-output")) {
        const char text[] = "FD1 UTF8 \xe3\x81\x82\n";
        g_api->tvram_clear();
        g_api->sys_write(1, text, sizeof(text)-1);
    } else if (!strcmp(line, "p3-overflow")) {
P3_STIMULUS
    } else accepting = rsh_lan_execute(line);'''.replace('P3_STIMULUS', stimulus))
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
    user = work/'rshell_lan.c'; user.write_text('#include <stdio.h>\n' + sources.pop(USER))
    user_out = work/'user'
    # kb-user recursively uses these command-line variables. Separate object
    # trees ensure no mutant can reuse a normal USER object (or vice versa).
    opts = ['KBOOT_GEM_RX_DIAG=1', 'KBOOT_SH=1', 'KBOOT_LINK=1', 'KB_SH_OUT='+str(user_out),
            'PPC_SH_MUT_SRC='+USER, 'PPC_SH_MUT_FILE='+str(user),
            'KB_ROOT_BINS='+str(user_out/'hello.bin')+' '+str(user_out/'fault.bin')+' '+text_fixture(work)]
    for extra_user in ('userland/shell/rshell.c', 'userland/shell/main.c'):
        if extra_user not in sources: continue
        source = work/Path(extra_user).name
        source.write_text(sources.pop(extra_user))
        opts.extend(['PPC_RES_MUT_SRC='+extra_user, 'PPC_RES_MUT_FILE='+str(source)])
    if observe_flow:
        source = 'sys/kern/exec/exec.c'
        observer = ROOT/'tests/support/fixtures/ppc_link/flow.inc'
        sources[source] = (ROOT/source).read_text() + '\n#include "'+str(observer)+'"\n'
        opts.append('KB_KEEP_RESET=-Wl,--wrap=arch_entry_enter,--wrap=ppc_trap_dispatch,--wrap=exec_run')
    require(len(sources) <= 3, 'kernel mutation slots')
    for slot, (path, content) in zip(('', 'EXTRA_', 'THIRD_'), sources.items()):
        dest = work/(Path(path).stem+'-meter.c'); dest.write_text(content)
        opts += ['KB_MUT_'+slot+'SRC='+path, 'KB_MUT_'+slot+'FILE='+str(dest)]
    return opts


def overflow_frames(path, offset, ident):
    events = []
    tracked = set()
    with path.open('rb') as stream:
        position = 0
        for line in stream:
            before_submit = position < offset
            position += len(line)
            sent = line.startswith(b'> ')
            frame = bytes.fromhex(line.removeprefix(b'> ').decode())
            start = wire.ETH_HDR + wire.LINK_HDR
            if len(frame) < start or int.from_bytes(frame[12:14], 'big') != wire.LINK_ETHERTYPE:
                continue
            op, flags, epoch, seq, ack, size, rid, sess = wire.hdr_unpack(frame[wire.ETH_HDR:])
            if before_submit and op != wire.OP_HELLO:
                continue
            payload = frame[start:start+size]
            key = (sess, epoch, rid)
            event = dict(op=wire.OPNAME.get(op), direction='host' if sent else 'guest',
                         sess=sess, epoch=epoch, rid=rid, flags=flags, seq=seq, ack=ack)
            if op == wire.OP_REQUEST:
                parts = payload.decode('ascii', 'replace').split()
                if parts[:2] == ['RSH', 'READY'] or (
                        parts[:2] == ['RSH', 'OUT'] and parts[3:4] == [ident]):
                    tracked.add(key)
                    event['request'] = parts
                    if parts[:2] == ['RSH', 'OUT'] and len(parts) == 6:
                        event['declared_length'] = parts[5]
            if op != wire.OP_HELLO and key not in tracked:
                continue
            if op == wire.OP_RESPONSE and len(payload) == 6:
                event['status'], event['length'] = struct.unpack('<HI', payload)
            if op == wire.OP_DATA:
                event['data'] = payload.decode('ascii', 'replace')
            event['bytes'] = size
            events.append(event)
    return events


def gem_assessment(previous, current):
    if not isinstance(previous, dict) or not isinstance(current, dict):
        return 'insufficient samples'
    if previous['poll'] == current['poll']:
        return 'poll drive'
    def full(v):
        return v['packet_count'] == 4 and v['kick'] == (v['done'] + 1) & 127
    if full(previous) and full(current):
        return 'foreground demux'
    if current['done'] == previous['done']:
        if all(v['head'] == v['done'] and v['kick'] != (v['done'] + 1) & 127
               for v in (previous, current)):
            return 'QEMU/UDP'
    elif current['head'] == previous['head']:
        return 'descriptor'
    else:
        return 'demux and link matching'
    return 'inconclusive'


class Session:
    usb_devices = ("-device", "usb-kbd,id=kbd")
    qemu_clock = ['-icount', 'shift=auto']
    qmp_type = None  # None = the module-level PumpQMP at start() (tests patch link_sh_run.PumpQMP)
    machine = "mac99"
    reboot_args = ("-no-reboot",)

    def __init__(self, media, cpu, out, timeout, late=False):
        self.out, self.media, self.cpu = out, media, cpu
        out.mkdir()
        self.deadline = time.monotonic()+timeout
        self.q = self.p = self.io = self.operator = self.err = self.frames = None
        self.events = []
        self.overflow_watch = None
        self.started = time.monotonic()
        self.agent_enabled = not late
        self.symbols = {p[2]: int(p[0],16) for line in command([cross('nm'), '-n', media.elf]).splitlines()
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

    def serial_a_backend(self):
        return 'file:'+str(self.out/'serial-a.log')

    def serial_args(self):
        return ['-serial', self.serial_a_backend()]

    def close_rshell(self, *, preloaded=False):
        if not preloaded:
            self.key('esc')
        self.wait(lambda: self.word('rshell_active') == 0 and
                  '[Remote shell closed]' in self.screen('closed'), 'local shell prompt', 30)

    def open_rshell(self):
        start = len(self.events)
        self.keys('rshell\n')
        self.wait(lambda: self.ready() and any(e['parts'][:2] == ['RSH', 'READY']
                  for e in self.events[start:]), 'new resident RSH READY', 30)

    def boot_args(self):
        return ['-cdrom', str(self.media.iso.resolve()), '-boot', 'd',
                '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF']

    def start(self):
        iso = self.media.iso.resolve()
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1',0)); port=reservation.getsockname()[1]
            self.io = UDP(f'127.0.0.1:0,127.0.0.1:{port}')
        args = ['qemu-system-ppc','-M',self.machine,'-cpu',self.cpu,'-m','128M','-smp','1',
            '-accel','tcg','-bios',str(ROM),*self.boot_args(),
            '-prom-env','auto-boot?=true',
            '-g','800x600x8','-display','none',*self.serial_args(),
            '-serial','file:'+str(self.out/'serial-b.log'),'-monitor','none','-qmp','stdio',*self.reboot_args,
            '-nic','none','-device','sungem,netdev=n0,mac=52:54:00:12:34:56','-netdev',
            f'socket,id=n0,udp=127.0.0.1:{self.io.sock.getsockname()[1]},localaddr=127.0.0.1:{port}',
            '-usb',*self.usb_devices,*self.qemu_clock,
            # Diagnosis only, never set by CI (trace events go through
            # OS32_QEMU_TRACE so they share the per-run trace file).
            *shlex.split(os.environ.get('OS32_QEMU_EXTRA', ''))]
        (self.out/'metadata.json').write_text(json.dumps(dict(command=args,cpu=self.cpu,
            elf=str(self.media.elf), map=str(self.media.map), iso=str(iso),
            iso_sha256=hashlib.sha256(iso.read_bytes()).hexdigest(),
            elf_sha256=hashlib.sha256(self.media.elf.read_bytes()).hexdigest()),indent=2))
        self.err=(self.out/'qemu.stderr').open('w'); self.frames=(self.out/'frames.log').open('w')
        self.qemu_started = time.monotonic()
        self.p=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.err,bufsize=0)
        self.q=(self.qmp_type or PumpQMP)(self.p,self.deadline,self.pump)

    def send(self, frame):
        self.frames.write('> '+bytes(frame).hex()+'\n'); self.io.send(frame)

    def pump(self):
        if self.p.poll() is not None: return
        while select.select([self.io],[],[],0)[0]:
            frame=self.io.receive(); self.frames.write(frame.hex()+'\n')
            if self.overflow_watch:
                self.observe_overflow_frame(frame)
            if self.agent_enabled: self.agent.handle(frame,self)
        if self.agent_enabled: self.agent.tick(self)
        for fd in select.select(self.operator.fileno_list(),[],[],0)[0]: self.operator.on_readable(fd)
        self.operator.poll(time.monotonic())

    def observe_overflow_frame(self, frame):
        watch = self.overflow_watch
        start = wire.ETH_HDR + wire.LINK_HDR
        if len(frame) < start or int.from_bytes(frame[12:14], 'big') != wire.LINK_ETHERTYPE:
            return
        op, flags, epoch, seq, ack, size, rid, sess = wire.hdr_unpack(frame[wire.ETH_HDR:])
        if op == wire.OP_REQUEST and not watch['out']:
            parts = frame[start:start+size].split()
            if parts[:2] == [b'RSH', b'OUT'] and parts[3:4] == [watch['id']]:
                watch['out'] = True
                watch['pending'].append('first-out-ack-wait')
        if watch['out'] and op == wire.OP_HELLO and flags == wire.HS_SYN:
            watch['syn'] += 1
            if watch['syn'] in (2, 3):
                watch['pending'].append('syn-retry-' + str(watch['syn'] - 1))

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

    def word(self, name, offset=0, unit="w"):
        require(name in self.symbols,'missing symbol '+name)
        # Resident ELF symbols are identity-loaded. Read their physical
        # storage independently of the guest's currently active translation.
        address = self.symbols[name] + offset
        command = f'xp /1{unit}x 0x{address:x}'
        value = self.q.call('human-monitor-command', {'command-line': command})
        match = re.search(r':\s*0x([0-9a-fA-F]+)', value)
        require(match is not None, f'unreadable {name} ({command}): {value!r}')
        return int(match[1],16)

    def overflow_probe(self, ident, until, phase, samples):
        now = time.monotonic()
        sample = dict(phase=phase, remaining=until-now, vm_remaining=self.deadline-now)
        samples.append(sample)
        path = self.out/'diag.json'
        try:
            try:
                raw = self.word('gem_rx_diag', unit='g')
                current = dict(poll=raw & 65535, packet_count=(raw >> 16) & 7,
                               head=(raw >> 19) & 127, done=(raw >> 26) & 127, kick=(raw >> 33) & 127)
                sample['gem_rx_diag'] = current
                previous = samples[-2].get('gem_rx_diag') if len(samples) > 1 else None
                sample['assessment'] = gem_assessment(previous, current)
            except CriterionFailure as exc:
                sample['gem_rx_diag'] = str(exc)
                sample['assessment'] = 'unreadable'
            registers = self.q.call('human-monitor-command', {'command-line': 'info registers'})
            for key, pattern in [('pc', 'NIP|PC'), ('msr', 'MSR')]:
                match = re.search(rf'\b(?:{pattern})\s*[:=]?\s*(?:0x)?([0-9a-f]+)', registers, re.I)
                sample[key] = match[1] if match else registers
            for name in ('tick_count', 'g_capture', 'con_sink_drop_count', 'p3_freeze_rc',
                         'p3_end_count', 'link_state', 'link_epoch', 'link_retransmits',
                         'link_resyncs', 'link_tx_deferred'):
                if name in self.symbols:
                    try:
                        sample[name] = self.word(name, unit='h' if name == 'link_epoch' else 'w')
                    except CriterionFailure as exc:
                        sample[name] = str(exc)
        finally:
            path.write_text(json.dumps(dict(id=ident, samples=samples), indent=2))

    def ready(self):
        return (self.agent.established and self.agent.operator.ready is not None
                and self.word('g_shell_service', 8) != 0
                and self.word('exec_nest_level') == 1 and self.word('cur_res_owner') == 1
                and self.word('g_cur_app') == 0)

    def cmd(self, line, expected=None, seconds=20):
        op=self.agent.operator
        self.wait(self.ready, 'RSH READY before '+line, seconds)
        ended=self.word('p3_end_count')
        # A new agent generation restarts IDs at 1: look only at events after this submit.
        start=len(self.events)
        if line == 'p3-overflow':
            now = time.monotonic()
            until = min(self.deadline, now + seconds)
            samples = []
            self.overflow_probe(None, until, 'normal', samples)
        ident=op.submit('cui',line,'accept',timeout_ms=max(20000,seconds*1000))
        def outputs():
            return [e for e in self.events[start:] if e['parts'][:2]==['RSH','OUT'] and e['parts'][3]==ident]
        # Inspect the wire result even when Operator rejects its command echo.
        # Otherwise PRINT duplication would become a generic result timeout.
        if line == 'p3-overflow':
            span = until - now
            probes = iter([(now + span / 2, 'mid'), (until - min(3, span / 4), 'deadline')])
            pending = next(probes)
            self.overflow_watch = dict(id=ident.encode(), out=False, syn=0, pending=[])
            self.frames.flush()
            offset = self.frames.tell()
            def observed_output():
                nonlocal pending
                while self.overflow_watch['pending']:
                    self.overflow_probe(ident, until, self.overflow_watch['pending'].pop(0), samples)
                if outputs():
                    return True
                if pending and time.monotonic() >= pending[0]:
                    self.overflow_probe(ident, until, pending[1], samples)
                    pending = next(probes, None)
                return bool(outputs())
            try:
                self.wait(observed_output, 'OUT '+line, until-time.monotonic())
            finally:
                self.overflow_watch = None
                self.frames.flush()
                frames = overflow_frames(self.out/'frames.log', offset, ident)
                (self.out/'diag.json').write_text(json.dumps(
                    dict(id=ident, samples=samples, frames=frames), indent=2))
        else:
            self.wait(lambda: bool(outputs()),'OUT '+line,seconds)
        if expected is not None: check_body(line,bytes.fromhex(outputs()[0]['body']),expected)
        self.wait(lambda: ident in op.results or (op.pending is not None and
                  op.pending['state'] in ('done','truncated')),'result '+line,seconds)
        result=op.results.get(ident) or op.take_result()
        first=self.word('tick_count')
        self.wait(lambda: self.word('tick_count')-first>=100,'END interval',10)
        events=outputs()
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
        names={'\n':'ret',' ':'spc','-':'minus','.':'dot','/':'slash'}
        for c in text: self.key(names.get(c,c))

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


def flow_point(s, index, expected):
    words = [s.word('shell_flow', 4*(12*index+field)) for field in range(3, 12)]
    generation, *fields = words
    require(generation > 0 and tuple(fields[:5]) == expected,
            f'resident flow point {index}: {words}')
    return generation, *fields[5:]


class LineSession(Session):
    uart = None

    def start(self):
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(10)
            self.serial_port = listener.getsockname()[1]
            super().start()
            self.uart, _ = listener.accept()

    def serial_args(self):
        return ['-chardev', f'socket,id=uart,host=127.0.0.1,port={self.serial_port},'
                f'logfile={self.out / "serial-a.log"}', '-serial', 'chardev:uart',
                *self.trace_args()]

    def trace_args(self):
        # One QEMU trace sink per run: the UART reader always needs
        # escc_mem_readb_data; diagnosis may add documented events through
        # OS32_QEMU_TRACE="name,name" (never set by CI). Extra events share the
        # same per-run file so later options cannot redirect the UART trace.
        extra = [n for n in os.environ.get('OS32_QEMU_TRACE', '').split(',') if n.strip()]
        if not extra:
            return ['-trace', 'enable=escc_mem_readb_data,file='+str(self.out/'uart.trace')]
        events = self.out/'trace-events'
        events.write_text('\n'.join(['escc_mem_readb_data', *[n.strip() for n in extra]])+'\n')
        return ['-trace', 'events='+str(events)+',file='+str(self.out/'uart.trace')]

    def pump(self):
        super().pump()
        if self.uart:
            while select.select([self.uart], [], [], 0)[0]:
                if not self.uart.recv(65536):
                    self.uart.close()
                    self.uart = None
                    break

    def close(self):
        try:
            super().close()
        finally:
            if self.uart:
                self.uart.close()


def accept(media,cpu,out,timeout,late=False,verdict=None,overflow_mode="lines"):
    # The 7450 non-late VM also runs the overflow observation.
    check_help = verdict in (None, 'resident command groups')
    check_overflow = cpu == '7450' and not late and verdict is None and overflow_mode == 'cr'
    extra = OVERFLOW_SECONDS * (int(check_help) + int(check_overflow))
    s=LineSession(media,cpu,out,timeout+extra,late)
    try:
        s.start()
        if late:
            # shell_service_publish follows the resident slot commit.
            def app_started():
                # Early in boot the kernel VA may not be readable yet: keep waiting.
                try: return (s.word('g_shell_service',8)!=0 and s.word('exec_nest_level')==1 and
                             s.word('cur_res_owner')==1 and s.word('g_cur_app')==0)
                except CriterionFailure: return False
            s.wait(app_started,'resident before delayed agent',60)
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
        # sys/include/config.h and cmd_env.c define the initial resident environment.
        s.cmd('pwd', b'/\n')
        s.cmd('echo $PATH', b'/bin:/sbin:/usr/bin\n')
        s.cmd('echo $HOME', b'/home/user\n')
        s.cmd('echo $SHELL', b'/shell\n')
        s.cmd('echo "two words" | cat', b'two words\n')
        s.cmd('cat /bin/sample.txt > /tmp/rsh', b'')
        s.cmd('cat /tmp/rsh', 'ASCII ｶﾅ 日本語\n'.encode())
        if check_help:
            help_text=s.cmd('help', seconds=OVERFLOW_SECONDS)['result']
            require(all(name in help_text for name in (b'rshell', b'echo', b'lspci')), 'resident command groups')
        require(s.cmd('p3-buffer')['result'] == b'> p3-buffer\nbuffered line\n\x04', 'line buffering')
        s.cmd('echo hello',b'hello\n')
        ready_count=lambda: sum(e['parts'][:2]==['RSH','READY'] for e in s.events)
        count=ready_count(); first=s.word('tick_count')
        s.wait(lambda: s.word('tick_count')-first>=500,'READY retry interval',30)
        require(ready_count()>count,'ready-retry')
        s.cmd('cat /no-such-file')
        failed=s.cmd('echo $?')['result']
        require(re.fullmatch(rb'> echo \$\?\n[1-9][0-9]*\n\x04',failed) is not None,'failure status')
        if verdict is None:
            generation, root, api, aux = flow_point(s, 0, (1, 0, 1, 1, 0))
            require(root == 0 and aux == s.symbols['g_shell_service'] and api == s.symbols['ppc_kapi_table'], 'resident API/aux')
        s.cmd('/bin/hello.bin ok', b'hello ok\n')
        s.cmd('echo $?', b'7\n')
        s.cmd('echo afterexit', b'afterexit\n')
        if verdict is None:
            _, child_root, child_api, child_aux = flow_point(s, 1, (2, 1, 2, 2, 2))
            require(child_root and child_api == s.word('ring3_tramp_page') and child_aux == 0, 'USER API/aux')
            flow_point(s, 2, (1, 0, 1, 1, 0))
            s.cmd('/bin/fault.bin')
            s.cmd('echo $?', b'139\n')
            flow_point(s, 3, (1, 0, 1, 1, 0))
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
        s.close_rshell()
        s.keys('echo usb\n')
        # The last key only queues the line; the APP loop runs it on a later pass.
        usb_seen=lambda text: 'OS32:/$ echo usb' in text and re.search(r'(?m)^usb *$',text)
        deadline=time.monotonic()+5
        screen=s.screen('usb')
        while not usb_seen(screen) and time.monotonic()<deadline:
            s.settle(.25); screen=s.screen('usb')
        require(usb_seen(screen),'USB line screen')
        s.open_rshell()
        if check_overflow:
            # Every overflow line scrolls the OF framebuffer, which is slow under
            # -icount (~2.7 s per scroll in QEMU 8.2), so one VM per run covers it.
            overflow=s.cmd('p3-overflow',seconds=OVERFLOW_SECONDS)
            require(overflow['state']=='truncated' and overflow['drop']>0,'result drop')
            if overflow_mode == 'cr':
                require(overflow['result'].endswith(b'0123456789abcdef\rp3-cr-tail\x04'), 'overflow tail')
            require(s.word('p3_freeze_rc')!=0,'freeze-drop')
            s.cmd('echo recovered',b'recovered\n')
        previous=s.word('g_shell_service',8)
        s.agent.notifier.notify_number(RSHELL,EXECUTE,b'00001000 p3-fault\n')
        count=ready_count(); s.settle(4)
        serial=(s.out/'serial-a.log').read_text(errors='replace')
        require('/sys/shell.bin returned ' in serial,'resident fault reached')
        require(s.word('g_capture')==0 and s.word('g_reader')==0xffffffff,'owner')
        if verdict == 'fault-restart' and s.word('g_shell_service',8) == previous:
            before = len(s.events)
            s.agent.notifier.notify_number(RSHELL, EXECUTE, b'00001001 echo afterfault\n')
            s.settle(2)
            require(not any(e['parts'][:2] == ['RSH','OUT'] for e in s.events[before:]),
                    'command completed after stopped restart')
        require(ready_count()>count and s.word('g_shell_service',8)>previous,'fault-restart')
        s.cmd('echo afterfault',b'afterfault\n')
        if verdict is None:
            flow_point(s, 4, (0, 0, 0, 0, 0))
            restarted, root, new_api, new_aux = flow_point(s, 5, (1, 0, 1, 1, 0))
            require(restarted > previous and not root and new_api == api and new_aux == aux,
                    'new resident generation')
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



class ResetQMP(PumpQMP):
    def __init__(self, process, deadline, pump):
        self.events = []
        super().__init__(process, deadline, pump)

    def read(self):
        message = super().read()
        if 'event' in message:
            self.events.append(message)
        return message


class ResetSession(Session):
    qmp_type = ResetQMP
    reboot_args = ()

    def serial(self):
        path = self.out/'serial-a.log'
        return path.read_text(errors='replace') if path.is_file() else ''

    def echo(self):
        op = self.agent.operator
        ident = op.submit('cui', 'echo x', 'accept')
        self.wait(lambda: ident in op.results or (op.pending and
                  op.pending['state'] == 'done'), 'echo x result')
        result = op.take_result() or op.results[ident]
        require(result['id'] == ident and result['state'] == 'done', 'echo x completion')
        check_body('echo x', result['result'], b'x\n')


def accept_reset(media, out, timeout, via, mutation=None):
    """PPC_RESET_DESIGN §7: real CUI reboot, guest RESET, and a new boot ID."""
    s = ResetSession(media, '7450', out, timeout)
    if via == 'pmu':
        s.machine = 'mac99,via=pmu'
    try:
        s.start()
        op = s.agent.operator
        via_ready = re.compile(rf'VIA kind={via} [^\r\n]* rc=ok ')
        s.wait(lambda: s.ready() and op.ready['mode'] == 'cui' and
               via_ready.search(s.serial()),
               f'{via} VIA and CUI READY', 60)
        for line in s.serial().splitlines():
            if f'VIA kind={via} ' in line:
                print(line, flush=True)
        s.echo()
        s.wait(lambda: s.word('g_capture') == 0, 'echo capture released')
        # The response drains all events queued before the RSH submission.
        s.q.call('query-status')
        first_event = len(s.q.events)
        serial_offset = (out/'serial-a.log').stat().st_size
        boot = op.ready['boot']
        require(s.word('detached') == 1, 'OF detached before reboot')
        ident = op.submit('cui', 'reboot', 'accept', timeout_ms=30000)
        print(f'RSH reboot: id={ident} boot={boot} serial_offset={serial_offset}', flush=True)

        def observe_reset():
            s.q.call('query-status')
            return next((event for event in s.q.events[first_event:]
                         if event['event'] == 'RESET' and
                         event.get('data', {}).get('guest') is True), None)

        if mutation == 'cuda-reset-byte':
            def sent_mutant_byte():
                require(not observe_reset(), 'SURVIVED '+mutation)
                return s.word('reset_sent_byte') == 0x1ff
            s.wait(sent_mutant_byte, 'Cuda reset byte 0xff sent', 30)
            print('Cuda reset byte sent: 0xff', flush=True)

        until = time.monotonic() + 30
        reset = None
        while time.monotonic() < until:
            reset = observe_reset()
            if reset:
                break
            time.sleep(.05)
        if mutation:
            require(not reset, 'SURVIVED '+mutation)
            if mutation == 'reboot-absent':
                result = op.take_result() or op.results.get(ident)
                require(result and result['id'] == ident, 'reboot stub did not return')
                check_body('reboot', result['result'], b'')
            else:
                require(op.pending and op.pending['id'] == ident and
                        op.pending['result'] is None, 'reboot unexpectedly returned')
            print(f'RED {mutation}: reboot ran without guest RESET (30 s)', flush=True)
            return

        require(reset, f'{via}: no guest RESET within 30 s; evidence={out}')
        print(json.dumps(reset), flush=True)
        # UDP has no disconnect event. Mirror the HostAgent CLI's disconnect
        # handling so old NOTIFY retries cannot fill sungem's RX queue during
        # OpenBIOS. Keep the operator so the new READY can retire reboot.
        op.session_reset()
        s.agent.established = False
        s.agent.notifier.reset()
        s.wait(lambda: s.ready() and op.ready['mode'] == 'cui' and op.ready['boot'] != boot,
               'CUI READY from a new boot', 60)
        retired = op.results.get(ident)
        require(retired and retired['state'] == 'boot_changed' and
                retired['boot'] == boot and op.pending is None, 'reboot request retirement')
        s.echo()
        print(f'GREEN {via} 7450: boot {boot} -> {op.ready["boot"]}; reboot retired; echo x', flush=True)
    finally:
        if s.q:
            (out/'qmp-events.json').write_text(json.dumps(s.q.events, indent=2))
        s.close()


FLOW_CASES = ('root-branch', 'missing-aux', 'restore-root', 'return-target')


def flow_settings(folder, case):
    """Observe child teardown and exercise the selected entry or return decision."""
    source = (ROOT/'sys/kern/exec/exec.c').read_text()
    source = 'volatile unsigned shell_flow_error, shell_child_returning, shell_return_target, shell_valid_root;\n' + source
    guard = 'do { shell_flow_error = %d; for (;;) __asm__ volatile("" ::: "memory"); } while (0);'
    for fragment in ('exec_launch.inc', 'exec_finish.inc'):
        text = (ROOT/'sys/kern/exec'/fragment).read_text()
        if fragment == 'exec_launch.inc':
            original = 'args.aux = is_shell ? (u32)(uptr)&g_shell_service : 0;'
            assignment = 'args.aux = 0;' if case == 'missing-aux' else original
            text = replace_once(text, original, assignment + '\n'
                '        if (is_shell && args.aux != (u32)(uptr)&g_shell_service) ' + guard % 2)
        else:
            anchor = 'appslot_switch_to(parent);'
            text = replace_once(text, anchor, 'shell_valid_root = 0;\n        shell_return_target = parent;\n'
                                '        ++shell_child_returning;\n        ' + anchor)
            # Publish the argument before installing it, while memory is accessible.
            root = '0' if case == 'restore-root' else 'paging_kernel_pd_phys()'
            original = 'g_cur_app = 0;\n        paging_load_cr3(paging_kernel_pd_phys());'
            text = replace_once(text, original, 'g_cur_app = 0;\n'
                                f'        u32 root = {root};\n'
                                '        shell_valid_root = root == paging_kernel_pd_phys() ? 1 : 2;\n'
                                '        paging_load_cr3(root);')
            if case == 'return-target':
                text = replace_once(text, 'parent = appslot_return_target(id);', 'parent = id;')
        (folder/fragment).write_text(text)
    observer = ROOT/'tests/support/fixtures/ppc_link/flow.inc'
    (folder/'exec.c').write_text(source + '\n#include "' + str(observer) + '"\n')
    entry = (ROOT/'sys/arch/ppc/exec_enter.S').read_text()
    # Record the actual branch before an invalid root can be used by the MMU.
    branch = 'bne' if case == 'root-branch' else 'beq'
    entry = replace_once(entry, 'beq .Lresident', branch + ' .Lresident\n'
                         '    cmpwi 4,0\n    beq .Lwrong_root')
    entry = replace_once(entry, '.Lresident:\n', '.Lwrong_root:\n'
                         '    lis 8,shell_flow_error@ha\n    li 9,1\n'
                         '    stw 9,shell_flow_error@l(8)\n1:  b 1b\n'
                         '.Lresident:\n    cmpwi 4,0\n    bne .Lwrong_root\n')
    (folder/'exec_enter.S').write_text(entry)
    user = folder/'user'
    return ['IMAGE=kboot-link-sh', 'KB_SH_OUT='+str(user), 'KB_ROOT_BINS='+str(user/'hello.bin'),
            'KB_MUT_SRC=sys/kern/exec/exec.c', 'KB_MUT_FILE='+str(folder/'exec.c'),
            'KB_MUT_EXTRA_SRC=sys/arch/ppc/exec_enter.S', 'KB_MUT_EXTRA_FILE='+str(folder/'exec_enter.S'),
            'KB_KEEP_RESET=-Wl,--wrap=arch_entry_enter,--wrap=ppc_trap_dispatch,--wrap=exec_run']


def flow_accept(media, folder, case):
    s = Session(media, '7450', folder, 180)
    try:
        s.start()
        if case in ('control', 'restore-root', 'return-target'):
            s.wait(s.ready, 'resident before child', 60)
            flow_point(s, 0, (1, 0, 1, 1, 0))
            previous = s.word('shell_child_returning')
            s.agent.operator.submit('cui', '/bin/hello.bin ok', 'flow')
            # The kernel image is identity-loaded. Sample physical memory so
            # a broken return root cannot hide the completed child teardown.
            def physical(name):
                address = s.symbols[name]
                result = s.q.call('human-monitor-command', {'command-line': f'xp /1wx 0x{address:x}'})
                value = re.search(r':\s*0x([0-9a-fA-F]+)', result)
                return int(value[1], 16) if value else None
            s.wait(lambda: physical('shell_child_returning') == previous + 1, 'child reached parent restoration', 30)
            target = physical('shell_return_target')
            if case == 'return-target':
                require(target == 2, f'expected child return target 2, got {target}')
                print('RED flow-return-target: shell_return_target=2', flush=True)
                return
            require(target == 1, f'expected shell return target 1, got {target}')
            valid = 2 if case == 'restore-root' else 1
            s.wait(lambda: physical('shell_valid_root') == valid, f'parent root decision {valid}', 10)
            if case == 'restore-root':
                print('RED flow-restore-root: shell_valid_root=2', flush=True)
                return
            start = len(s.events)
            ident = '00002000'
            execute = s.agent.notifier.notify_number(RSHELL, EXECUTE, ident.encode()+b' echo next $?\n')
            s.wait(lambda: execute.delivery == 'acked', 'guest ACK for post-child EXECUTE '+ident, 10)
            s.settle(20)
            replies = [bytes.fromhex(e['body']) for e in s.events[start:]
                       if e['parts'][:2] == ['RSH', 'OUT'] and e['parts'][3] == ident]
            expected = b'> echo next $?\nnext 7\n\x04'
            require(expected in replies, 'control command after child return')
            flow_point(s, 1, (2, 1, 2, 2, 2))
            flow_point(s, 2, (1, 0, 1, 1, 0))
            result = s.agent.operator.take_result()
            check_body('/bin/hello.bin ok', result['result'], b'hello ok\n')
            require(s.word('shell_flow_error') == 0, 'clean entry and return')
            return
        def stopped():
            try:
                return s.word('shell_flow_error') == FLOW_CASES.index(case) + 1
            except CriterionFailure:
                return False
        s.wait(stopped, 'specific rejected decision '+case, 60)
        require(not s.ready(), 'READY after invalid resident entry')
    finally:
        s.close()


# These protocol mutations exercise the product-shell session alongside its
# entry/return controls. The remaining mutations follow the transport controls.
FLOW_MUTATIONS = ('command-group', 'line-buffer', 'ready-retry')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path)
    ap.add_argument('--suite', choices=('all', 'run', 'flow'), default='all')
    ap.add_argument('--list-cases', action='store_true')
    ap.add_argument('--timeout',type=int,default=180)
    ap.add_argument('--mutate',action='store_true')
    ap.add_argument('--build-only',action='store_true')
    ap.add_argument('--overflow', choices=('lines', 'cr'), default='lines')
    args=ap.parse_args()
    controls = [(cpu, late) for cpu in ('7450', '7455') for late in (False, True)
                if args.suite != 'flow']
    flows = ('control', *FLOW_CASES) if args.mutate else ('control',)
    if args.suite == 'run':
        flows = ()
    mutations = [name for name in MUTATIONS if args.mutate and
                 (args.suite == 'all' or (name in FLOW_MUTATIONS) == (args.suite == 'flow'))]
    if args.list_cases:
        for case in flows:
            print('flow-' + case)
        for cpu, late in controls:
            print(cpu + ('-late' if late else '-normal'))
        for name in mutations:
            print(name)
        return 0
    if args.out is None:
        ap.error('--out is required to build or run')
    required=tuple(cross(tool) for tool in ('gcc','nm','objdump','ld','objcopy'))
    required += ('genisoimage', 'isoinfo')
    if not args.build_only: required+=('qemu-system-ppc',)
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
    hashes = set()
    if controls:
        control=work/'normal'; control.mkdir()
        media=build_media(control/'kernel',*settings(control, observe_flow=True, overflow=args.overflow))
        check_map(media.map)
        digest = hashlib.sha256(media.elf.read_bytes()).digest()
        hashes.add(digest)
        print('normal-'+args.overflow, digest.hex(), flush=True)
    failures = []

    @contextmanager
    def record_case(name):
        try:
            yield
        except Exception as exc:
            failures.append(f'{name}: {exc}')
            print('FAIL '+failures[-1], flush=True)

    for case in flows:
        with record_case('flow-'+case):
            folder = work/('flow-'+case)
            folder.mkdir()
            image = build_media(folder/'kernel', *flow_settings(folder, case), 'kb-user', 'kernel')
            digest = hashlib.sha256(image.elf.read_bytes()).digest()
            require(digest not in hashes, 'distinct flow image '+case)
            hashes.add(digest)
            print('flow-'+case, digest.hex(), flush=True)
            if not args.build_only:
                flow_accept(image, folder/'guest', case)
    if args.build_only:
        if args.mutate and controls:
            other = 'cr' if args.overflow == 'lines' else 'lines'
            folder = work/('normal-'+other)
            folder.mkdir()
            image = build_media(folder/'kernel', *settings(folder, observe_flow=True, overflow=other))
            digest = hashlib.sha256(image.elf.read_bytes()).digest()
            require(digest not in hashes, 'distinct overflow image '+other)
            hashes.add(digest)
            print('normal-'+other, digest.hex(), flush=True)
        for name in mutations:
            folder=work/name; folder.mkdir()
            mutant=build_media(folder/'kernel',*settings(folder,name,overflow=args.overflow))
            check_map(mutant.map)
            digest=hashlib.sha256(mutant.elf.read_bytes()).digest()
            require(digest not in hashes, 'distinct image '+name)
            hashes.add(digest)
            print(name, digest.hex(), flush=True)
        require(not failures, '\n'.join(failures))
        print(f'Built {len(hashes)} distinct images', flush=True)
        return 0
    for cpu, late in controls:
        with record_case(f'{cpu} late={late}'):
            accept(media,cpu,work/(cpu+('-late' if late else '-normal')),args.timeout,late,overflow_mode=args.overflow)
            print('GREEN '+cpu+' late='+str(late),flush=True)
    if args.mutate:
        for name in mutations:
            expected = MUTATIONS[name][3]
            with record_case(name):
                folder=work/name; folder.mkdir()
                mutant=build_media(folder/'kernel',*settings(folder,name,overflow=args.overflow))
                try: accept(mutant,'7450',folder/'guest',args.timeout,verdict=expected,overflow_mode=args.overflow)
                except CriterionFailure as exc:
                    require(str(exc)==expected,'unexpected mutation verdict '+name+': '+str(exc))
                    print('RED '+name+': '+str(exc),flush=True)
                else: raise RuntimeError('SURVIVED '+name)
    require(not failures, '\n'.join(failures))
    return 0


if __name__=='__main__': sys.exit(main())
