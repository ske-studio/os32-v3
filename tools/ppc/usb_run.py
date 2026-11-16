#!/usr/bin/env python3
"""PowerMac USB acceptance over serial, with optional LAN RPC-stall regression."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time

from kboot_accept import build
from run import CriterionFailure
from kboot_run import ROOT, ROM, QMP, command, require, serial, validate, PREFIX

# Keep TB reads and OHCI timers on instruction-counted virtual time. With
# host-paced TCG, device_add can leave frames stale for >20ms of guest TB.
USB_QEMU_CLOCK = ['-icount', 'shift=auto']

CHECK = dict(name='ppc-usb', tier=2, budget=900, needs=['make:kernel'],
             covers=['platform/powermac/console_stream.c', 'platform/powermac/console_log.c',
                     'platform/powermac/log.c', 'platform/powermac/kboot_log.h', 'platform/powermac/input_usb.c', 'platform/powermac/input_keymap.c', 'platform/powermac/input_usb.h', 'tools/tests/host32.py', 'tools/ppc/usb_run.py', 'platform/powermac/usb*', 'build/ppc-kboot.mk', 'init/ppc_kernel.c',
                     'platform/powermac/kboot_capture.c', 'drivers/usb/*.[ch]',
                     'include/usb_platform.h', 'include/endian_dma.h',
                     'arch/ppc/arch_endian_dma.h', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'platform/powermac/shell_boot.c', 'platform/powermac/input_boot.c',
                     'arch/ppc/entry.S',
                     'arch/ppc/kernel_dec.c',
                     'arch/ppc/kernel_mmu.S',
                     'arch/ppc/kernel_probe.S',
                     'arch/ppc/kernel_trap.c',
                     'arch/ppc/ofw_note.S',
                     'platform/powermac/boot.c',
                     'platform/powermac/clock.c',
                     'platform/powermac/escc.c',
                     'platform/powermac/escc_tree.c',
                     'platform/powermac/mpic.c',
                     'platform/powermac/ofw.c',
                     'tools/host_image.py',
                     'tools/lan_bridge.py',
                     'tools/lanfs_host.py',
                     'tools/ppc/kboot_mutations.py',
                     'tools/ppc_hostfs.py',
                     'tools/ppc_lan_host.py',
                     'tools/serialfs_host.py'],
             inputs=['tools/ppc/sd_run.py', 'arch/ppc/kernel_boot.h'])

PROBE = r"""
#include "clock_source.h"
#include "kernel_tick.h"
#include "usb_platform.h"
#include "kprintf.h"
static int usb_probe_input(void)
{
    static u32 start, stalled, returned, fresh;
    u32 now, hi;
    platform_clock_read_us(&now, &hi);
    if (!start) start = now;
    /* The first call after a returned key is the next getkey syscall's first
     * try, before any wait_step: no _idle in this syscall has enabled EE yet,
     * so MSR[EE] is exactly what the syscall entry set (syscall-ee). */
    u32 first = fresh;
    fresh = 0;
    if (!stalled && first && (u32)(now - start) >= 3000000) {
        stalled = 1;
        /* AC-CTX-1/6: a real USER syscall stays FG1 during this one-second
         * input poll; DEC delivery must continue before syscall return. */
        u32 ticks = tick_count;
        for (u32 i = 0; i < 10; i++) usbp_udelay(100000);
        kprintf(7, "USB STALL ticks=%u\n", (u32)(tick_count - ticks));
        kprintf(7, "USB STALL END\n");
        return '\n';
    }
    if ((u32)(now - start) >= 12000000) {
        kprintf(7, "USB ACCEPT END\n");
        __asm__ volatile("mfmsr 3; rlwinm 3,3,0,17,15; mtmsr 3; isync" ::: "r3", "memory");
        __asm__ volatile(".global usb_accept_stop\nusb_accept_stop: b usb_accept_stop");
        __builtin_unreachable();
    }
    if (!stalled && (u32)(now - returned) >= 10000) {
        returned = now;
        fresh = 1;
        return '\n';
    }
    return -1;
}
"""


def mutation(work, mode):
    src = ('arch/ppc/kernel_trap.c' if mode == 'syscall-ee' else
           'platform/powermac/usb_boot.c' if mode == 'no-service' else 'platform/powermac/usb_platform.c')
    content = (ROOT/src).read_text()
    before, after = {
        'syscall-ee': ('            ppc_enable();', '            /* mutation: no syscall enable */'),
        'no-service': ('    usb_service();', '    /* mutation: omit USB service */'),
        'invert-key': ('pm_input_usb_key(src, sc, down);', 'pm_input_usb_key(src, sc, !down);'),
    }[mode]
    require(content.count(before) == 1, 'unique USB mutation anchor')
    dest = work/(mode+'-'+Path(src).name)
    dest.write_text(content.replace(before, after))
    return src, dest


def settings(work, mode):
    # As in sc_run's serial mode, substitute only the external LAN connection
    # and byte-input boundaries. USER sh and the USB product closure are real.
    boot = (ROOT/'platform/powermac/shell_boot.c').read_text()
    anchor = 'while (!pm_term_connected() && !pm_input_usb_has_key()) pm_terminal_poll();'
    require(boot.count(anchor) == 1, 'unique USB wait anchor')
    boot = boot.replace(anchor,
                        '/* Serial acceptance supplies the terminal connection. */')
    inp = (ROOT/'platform/powermac/input_boot.c').read_text()
    inp = inp.replace('static PmInputDecoder', PROBE+'\nstatic PmInputDecoder')
    if mode != 'normal':
        inp = inp.replace('if (!stalled && (u32)(now - returned) >= 10000)',
                          'if ((u32)(now - returned) >= 10000)')
    require(inp.count('pm_terminal_trygetkey()') == 1, 'unique USB input anchor')
    inp = inp.replace('pm_terminal_trygetkey()', 'usb_probe_input()')
    result = ['KBOOT_SH=1']
    for slot, src, content in [('', 'platform/powermac/shell_boot.c', boot),
                                ('EXTRA_', 'platform/powermac/input_boot.c', inp)]:
        dest = work/(mode+'-'+Path(src).name)
        dest.write_text(content)
        result += ['KB_MUT_'+slot+'SRC='+src, 'KB_MUT_'+slot+'FILE='+str(dest)]
    if mode != 'normal':
        src, dest = mutation(work, mode)
        result += ['KB_MUT_THIRD_SRC='+src, 'KB_MUT_THIRD_FILE='+str(dest)]
    else:
        # Normal stall acceptance also observes the real sh execution result.
        observation = product_settings(work, 'stall-stream')
        result += [x.replace('KB_MUT_', 'KB_MUT_THIRD_') for x in observation if x.startswith('KB_MUT_')]
    return result


def accept(elf, cpu, out, mode):
    while os.getloadavg()[0] >= 4: time.sleep(5)
    out.mkdir()
    cd = out/'cd'; cd.mkdir()
    shutil.copyfile(elf, cd/'PPCKERN.ELF')
    iso = out/'boot.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    a, b = out/'serial-a.log', out/'serial-b.log'
    args = ['qemu-system-ppc', *(['-g', '800x600x8'] if os.environ.get('PPC_FB') == '1' else []), '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-smp', '1',
            '-accel', 'tcg', '-bios', str(ROM), '-drive', f'file={iso},media=cdrom,id=bootcd',
            '-boot', 'd', '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
            '-prom-env', 'auto-boot?=true', *(['-display', 'none'] if os.environ.get('PPC_FB') == '1' else ['-nographic']), '-serial', 'file:'+str(a),
            '-serial', 'file:'+str(b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot',
            '-nic', 'none', '-usb', '-device', 'usb-kbd,id=kbd', '-device', 'usb-mouse,id=mouse']
    args += USB_QEMU_CLOCK
    (out/'argv.json').write_text(json.dumps(args, indent=2))
    deadline = time.monotonic()+60
    q = None
    with (out/'qemu.stderr').open('w') as err:
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p, deadline)
            phase, sent_at = 0, 0
            while time.monotonic() < deadline:
                text = serial(a).replace('\r', '')
                validate(text, serial(b))
                ready = 'sh> ' in (stream_payload(text) if mode == 'normal' else text)
                keyboard = re.search(r'USB DEV addr=(\d+) vid:pid=0627:0001 class=03 subclass=01 protocol=01', text)
                mouse = re.search(r'USB DEV addr=(\d+) vid:pid=0627:0001 class=03 subclass=01 protocol=02', text)
                if (phase == 0 and ready and ((keyboard and mouse) or mode == 'no-service')
                        and 'USB STALL END' in text):
                    q.call('send-key', {'keys': [{'type': 'qcode', 'data': 'a'}], 'hold-time': 100})
                    phase = 1; sent_at = time.monotonic()
                elif phase == 1 and time.monotonic()-sent_at > .4:
                    q.call('send-key', {'keys': [{'type': 'qcode', 'data': 'shift'},
                                               {'type': 'qcode', 'data': 'a'}], 'hold-time': 100})
                    phase = 2; sent_at = time.monotonic()
                elif phase == 2 and time.monotonic()-sent_at > .4:
                    q.call('send-key', {'keys': [{'type': 'qcode', 'data': 'ret'}], 'hold-time': 100})
                    phase = 3; sent_at = time.monotonic()
                elif phase == 3 and time.monotonic()-sent_at > .4:
                    if mode == 'normal':
                        for code in ['e','c','h','o','spc','h','i','ret']:
                            releases = len(re.findall(r'USB KEY src=1 sc=[0-9a-f]+ down=0', serial(a)))
                            q.call('send-key', {'keys':[{'type':'qcode','data':code}], 'hold-time':100})
                            while len(re.findall(r'USB KEY src=1 sc=[0-9a-f]+ down=0', serial(a))) <= releases:
                                require(time.monotonic() < deadline, 'post-stall USB key release')
                                time.sleep(.01)
                    q.call('input-send-event', {'events': [
                        {'type': 'rel', 'data': {'axis': 'x', 'value': 12}},
                        {'type': 'rel', 'data': {'axis': 'y', 'value': -7}},
                        {'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
                    phase = 4; sent_at = time.monotonic()
                elif phase == 4 and time.monotonic()-sent_at > .4:
                    q.call('input-send-event', {'events': [
                        {'type': 'btn', 'data': {'button': 'left', 'down': False}}]})
                    # Unplug while A is held: release must be observable.
                    q.call('human-monitor-command', {'command-line': 'sendkey a 10000'})
                    phase = 5; sent_at = time.monotonic()
                elif phase == 5 and time.monotonic()-sent_at > .4:
                    q.call('device_del', {'id': 'kbd'})
                    phase = 6
                if 'USB ACCEPT END\n' in text:
                    q.call('stop')
                    regs = q.call('human-monitor-command', {'command-line': 'info registers'})
                    (out/'registers.txt').write_text(regs)
                    symbols = command([PREFIX+'nm', '-n', elf])
                    address = next(int(l.split()[0], 16) for l in symbols.splitlines()
                                   if l.endswith(' usb_accept_stop'))
                    pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                    if not pc or int(pc[1], 16) != address:
                        q.call('cont')
                        time.sleep(.01)
                        continue
                    validate(text, serial(b), True)
                    require(ready and phase == 6 and 'USB STALL END' in text and re.search(r'USB STALL ticks=(?:9[0-9]|10[0-9]|110)\r?\n', text) and 'USB HC bdf=' in text and 'USB TAKEOVER quiet' in text,
                            'USB startup/reached markers: '+text)
                    keys = [(int(sc, 16), int(down)) for sc, down in
                            re.findall(r'USB KEY src=1 sc=([0-9a-f]+) down=(\d)', text)]
                    expected = [(0x1d, 1), (0x1d, 0), (0x70, 1), (0x1d, 1),
                                (0x1d, 0), (0x70, 0), (0x1c, 1), (0x1c, 0)]
                    if mode == 'normal':
                        for sc in [0x12,0x2b,0x22,0x18,0x34,0x22,0x17,0x1c]:
                            expected += [(sc,1),(sc,0)]
                    expected += [(0x1d,1)]
                    if mode == 'no-service':
                        require(not keyboard and not mouse and not keys and 'USB MOUSE' not in text,
                                'no-tick exact missing enumeration/input verdict')
                    else:
                        require('usb: HC dead' not in text, 'healthy HC after stall')
                        require(keyboard and mouse, 'HID identities: '+text)
                        require(keys == expected, 'PC98 make/break order: '+repr(keys))
                        if mode == 'normal':
                            cooked = stream_payload(text).replace('\r','')
                            require('\nhi\nsh> ' in cooked and cooked.count('sh> ') >= 3,
                                    'real USB echo hi after FG1 stall: '+repr(cooked))
                            (out/'stream.txt').write_text(cooked)
                        require('USB MOUSE src=1 dx=12 dy=-7 b=1' in text and
                                'USB MOUSE src=1 dx=0 dy=0 b=0' in text, 'relative mouse/buttons: '+text)
                        require('USB RELEASE kind=0 src=1' in text and
                                'USB DETACH addr='+keyboard[1] in text, 'keyboard detach/release')
                    q.call('quit'); p.wait(timeout=2)
                    require(p.returncode == 0, 'QEMU exit code')
                    print(('GREEN' if mode == 'normal' else 'RED')+' '+mode+' '+cpu+
                          ': exact serial verdict, stop PC, QEMU rc=0', flush=True)
                    return
                time.sleep(.01)
            q.deadline = time.monotonic()+10
            q.call('stop')
            for label, cmd in [('registers', 'info registers'), ('pci', 'info pci'), ('qtree', 'info qtree')]:
                (out/(label+'.txt')).write_text(q.call('human-monitor-command', {'command-line': cmd}))
            symbols = command([PREFIX+'nm', '-n', elf])
            for name in ['tick_count', 'controllers', 'pm_usb_roots', 'pm_usb_pci', 'usb_busy']:
                matches = [l for l in symbols.splitlines() if l.endswith(' '+name)]
                for i, line in enumerate(matches):
                    address = int(line.split()[0], 16)
                    data = q.call('human-monitor-command', {'command-line': 'xp /96wx 0x%x' % address})
                    (out/(name+str(i)+'.txt')).write_text(data)
            raise RuntimeError('USB timeout; evidence='+str(out))
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()



def stream_payload(text):
    """Length-delimited mirror records; USB log bytes are never payload."""
    result, pos = '', 0
    while True:
        match = re.search(r'~USBSTREAM:([0-9a-f]{8}):', text[pos:])
        if not match: return result
        start = pos + match.end()
        size = int(match[1], 16)
        if len(text) < start + size: return result
        result += text[start:start+size]
        pos = start + size


def product_settings(work, mode, late=False):
    src = 'platform/powermac/console_stream.c'
    text = (ROOT/src).read_text()
    anchor = '(void)pm_escc_write(&serial, bytes, len);'
    require(text.count(anchor) == 1, 'unique stream observation anchor')
    mirror = r'''
    char frame[] = "~USBSTREAM:00000000:";
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 8; i++) frame[11+i] = hex[(len >> (28-4*i)) & 15];
    /* One observation frame must not interleave with IRQ-context USB logs:
     * a split header hides its payload from stream_payload (seen with NAP). */
    extern u32 ppc_irq_save(void); extern void ppc_irq_restore(u32);
    u32 frame_irq = ppc_irq_save();
    (void)pm_escc_write(&serial, frame, sizeof(frame)-1);
    (void)pm_escc_write(&serial, bytes, len);
    ppc_irq_restore(frame_irq);'''
    text = text.replace(anchor, mirror)
    file = work/(mode+'-stream.c'); file.write_text(text)
    opts = ['KBOOT_SH=1', 'KB_MUT_SRC='+src, 'KB_MUT_FILE='+str(file)]
    if mode in ('invert-key', 'no-forward'):
        src = 'platform/powermac/usb_platform.c'
        text = (ROOT/src).read_text()
        anchor = 'pm_input_usb_key(src, sc, down);'
        require(text.count(anchor) == 1, 'unique whole callback anchor')
        text = text.replace(anchor, 'pm_input_usb_key(src, sc, !down);' if mode == 'invert-key'
                            else '/* omitted callback forwarding */')
        file = work/(mode+'-platform.c'); file.write_text(text)
        opts += ['KB_MUT_EXTRA_SRC='+src, 'KB_MUT_EXTRA_FILE='+str(file)]
    if late:
        src = 'platform/powermac/shell_boot.c'
        text = (ROOT/src).read_text()
        anchor = 'while (!pm_term_connected() && !pm_input_usb_has_key()) pm_terminal_poll();'
        require(text.count(anchor) == 1, 'unique mouse-only startup wait anchor')
        text = '#include "usb_platform.h"\n' + text
        text = text.replace(anchor, r'''u32 start = usbp_ms();
    int reported = 0;
    while (!pm_term_connected() && !pm_input_usb_has_key()) {
        pm_terminal_poll();
        if (!reported && (u32)(usbp_ms() - start) >= 3000) {
            reported = 1;
            kprintf(7, "USB MOUSE WAIT END\n");
        }
    }''')
        file = work/(mode+'-mouse-wait.c'); file.write_text(text)
        opts += ['KB_MUT_EXTRA_SRC='+src, 'KB_MUT_EXTRA_FILE='+str(file)]
    return opts


def product_accept(elf, cpu, out, mode, late=False):
    """Real sh/input; late observes three seconds inside the startup wait."""
    while os.getloadavg()[0] >= 4: time.sleep(5)
    out.mkdir()
    cd = out/'cd'; cd.mkdir(); shutil.copyfile(elf, cd/'PPCKERN.ELF')
    iso = out/'boot.iso'
    command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
    a, b = out/'serial-a.log', out/'serial-b.log'
    argv = ['qemu-system-ppc', *(['-g', '800x600x8'] if os.environ.get('PPC_FB') == '1' else []),'-M','mac99','-cpu',cpu,'-m','128M','-smp','1',
            '-accel','tcg','-bios',str(ROM),'-drive',f'file={iso},media=cdrom',
            '-boot','d','-prom-env','boot-device=cd:,\\PPCKERN.ELF',
            '-prom-env','auto-boot?=true',*(['-display', 'none'] if os.environ.get('PPC_FB') == '1' else ['-nographic']),'-serial','file:'+str(a),
            '-serial','file:'+str(b),'-monitor','none','-qmp','stdio','-no-reboot',
            '-nic','none','-usb','-device','usb-kbd,id=kbd','-device','usb-mouse,id=mouse']
    if late:
        slot = argv.index('usb-kbd,id=kbd')
        del argv[slot-1:slot+1]
    argv += USB_QEMU_CLOCK
    (out/'argv.json').write_text(json.dumps(argv, indent=2))
    # The live FB renders the full editing/hotplug transcript as well as ESCC.
    started = time.monotonic()
    # Each key now waits for the guest to see the press before releasing it, so
    # a loaded CI runner needs ~55 s for the transcript (local 1-CPU: 46 s).
    deadline = started+(180 if os.environ.get('PPC_FB') == '1' else 120)
    q = None
    with (out/'qemu.stderr').open('w') as err:
        p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p, deadline)
            def output():
                text = serial(a)
                validate(text, serial(b))
                require('usb: HC dead' not in text, 'healthy product HC: '+str(out))
                return stream_payload(text).replace('\r','')
            def wait_for(predicate, label):
                while time.monotonic() < deadline:
                    if predicate(): return
                    time.sleep(.01)
                # CI keeps no evidence directory: print where the guest stopped.
                tail = serial(a)[-1500:]
                print('product USB timeout '+label+' after %.1fs; serial tail:\n' % (time.monotonic()-started) +
                      tail, flush=True)
                raise RuntimeError('product USB timeout: '+label+' evidence='+str(out))
            def key(code, mods=(), hold=100):
                # Hold is measured in guest time. QEMU's send-key hold-time runs on the
                # virtual clock, which under -icount drifts from real time while the
                # guest naps (PPC_NOTES §3); release after the guest has seen every
                # press, and for a long hold after its first typematic repeat.
                releases = len(re.findall(r'USB KEY src=1 sc=[0-9a-f]+ down=0', serial(a)))
                presses = len(re.findall(r'USB KEY src=1 sc=[0-9a-f]+ down=1', serial(a)))
                keys = [*mods, code]
                q.call('input-send-event', {'events': [
                    {'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': c}}} for c in keys]})
                wait_for(lambda: len(re.findall(r'USB KEY src=1 sc=[0-9a-f]+ down=1', serial(a)))
                         >= presses+len(keys), 'key press '+code)
                if hold > 100:
                    repeats = len(re.findall(r'USB REPEAT src=1 sc=', serial(a)))
                    wait_for(lambda: len(re.findall(r'USB REPEAT src=1 sc=', serial(a))) > repeats,
                             'key repeat '+code)
                q.call('input-send-event', {'events': [
                    {'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': c}}} for c in reversed(keys)]})
                wait_for(lambda: len(re.findall(r'USB KEY src=1 sc=[0-9a-f]+ down=0', serial(a)))
                         >= releases+len(mods)+1, 'key release '+code)
                time.sleep(.03)
            def text_keys(text):
                for c in text:
                    if c == ' ': key('spc')
                    elif c == '!': key('1', ('shift',))
                    elif c.isupper(): key(c.lower(), ('shift',))
                    else: key(c)
            def execute(text, expected):
                previous = output().count('sh> ')
                start = len(output())
                text_keys(text); key('ret')
                count = previous + (1 if previous else 2)
                wait_for(lambda: output().count('sh> ') >= count and output().endswith('sh> '), 'prompt after '+text)
                block = output()[start:output().rfind('sh> ')]
                if not previous: block = block[block.index('sh> ')+4:]
                lines = block.splitlines()[1:]  # discard the echoed command line
                require(expected in lines, 'product command output '+repr((text, block, expected)))
                return block
            if late:
                wait_for(lambda: 'USB MOUSE WAIT END' in serial(a), 'three-second mouse-only wait')
                require('protocol=02' in serial(a) and 'sh> ' not in output(),
                        'mouse enumerated while real sh startup waits')
                require('usb: HC dead' not in serial(a), 'healthy mouse-only wait')
                q.call('device_add', {'driver':'usb-kbd', 'id':'kbd'})
            wait_for(lambda: 'protocol=01' in serial(a), 'keyboard enumeration')
            require('sh> ' not in output(), 'USB sh still waits before first key')
            if mode == 'no-forward':
                text_keys('echo hi'); key('ret')
                require('USB KEY src=1 sc=1c down=0' in serial(a), 'no-forward reached Enter release')
                require('sh> ' not in output() and 'hi\n' not in output(), 'no-forward exact missing sh startup')
            else:
                execute('echo hi', 'hi')  # first e must survive startup
                if mode == 'invert-key':
                    start = len(output()); text_keys('echo Hi!'); key('ret')
                    wait_for(lambda: output()[start:].endswith('sh> '), 'invert-key result prompt')
                    block = output()[start:output().rfind('sh> ')]
                    require('Hi!' not in block.splitlines()[1:] and 'USB KEY src=1 sc=70 down=1' in serial(a),
                            'invert-key exact shifted result mismatch: '+repr(block))
                else:
                    execute('echo Hi!', 'Hi!')
                    # Backspace, cancellation, history and all six cooked edit keys.
                    start = len(output()); text_keys('echo hx'); key('backspace'); key('i'); key('ret')
                    wait_for(lambda: output()[start:].endswith('sh> '), 'backspace prompt')
                    require('hi' in output()[start:].splitlines()[1:], 'backspace result')
                    start=len(output()); prompts=output().count('sh> ')
                    text_keys('echo cancelled'); key('c', ('ctrl',)); key('ret')
                    wait_for(lambda: output().count('sh> ') >= prompts+2 and output().endswith('sh> '), 'cancel prompts')
                    require('cancelled' not in output()[start:].splitlines()[1:], 'Ctrl-C cancellation')
                    execute('echo history', 'history')
                    start=len(output()); key('up'); key('down'); key('up'); key('ret')
                    # DOWN can redraw an empty 'sh> ' before UP/Enter is
                    # consumed, especially while the framebuffer is drawing.
                    # Require the fd1 result as well as the final prompt.
                    wait_for(lambda: output()[start:].endswith('sh> ') and
                             'history' in output()[start:].splitlines()[1:], 'history result prompt')
                    require('history' in output()[start:].splitlines()[1:], 'history result')
                    start=len(output()); text_keys('echo hxi'); key('left'); key('left'); key('right');
                    key('home');
                    # HOME moves to the beginning; advance to x and delete it.
                    for _ in range(6): key('right')
                    key('delete'); key('ret')
                    wait_for(lambda: output()[start:].endswith('sh> '), 'edit prompt')
                    require('hi' in output()[start:].splitlines()[1:], 'HOME/LEFT/RIGHT/DEL result')
                    start=len(output()); text_keys('echo '); key('a', hold=800); time.sleep(.15)
                    time.sleep(.15); held=output(); time.sleep(.15); require(output()==held, 'no repeat after release')
                    key('ret'); wait_for(lambda: output()[start:].endswith('sh> '), 'repeat prompt')
                    require(any(re.fullmatch('a{2,}', line) for line in output()[start:].splitlines()[1:]), 'repeat result')
                    q.call('human-monitor-command', {'command-line':'sendkey shift 10000'})
                    time.sleep(.2); q.call('device_del', {'id':'kbd'})
                    wait_for(lambda: 'USB RELEASE kind=0 src=1' in serial(a), 'held unplug release')
                    require(q.call('query-status')['running'], 'sh alive after detach')
                    q.call('device_add', {'driver':'usb-kbd','id':'kbd2'})
                    wait_for(lambda: serial(a).count('protocol=01')>=2, 'reattach enumeration')
                    execute('echo alive', 'alive')
            require('USB HC bdf=' in serial(a) and 'usb: HC dead' not in serial(a), 'healthy HC reached')
            q.call('stop')
            if os.environ.get('PPC_FB') == '1':
                q.call('screendump', {'filename': str(out/'screen.ppm')})
                require('FB text ready' in serial(a), 'INDEX8 console attached')
            (out/'registers.txt').write_text(q.call('human-monitor-command', {'command-line':'info registers'}))
            (out/'stream.txt').write_text(output())
            q.call('quit'); p.wait(timeout=2); require(p.returncode==0, 'product QEMU rc=0')
            print(('GREEN' if mode=='normal' else 'RED')+' product-sh '+('mouse-wait ' if late else '')+mode+' '+cpu+
                  ': reached HID/command boundary, exact stream verdict, QEMU rc=0 elapsed=%.1fs'
                  % (time.monotonic()-started), flush=True)
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()

def lan_accept(elf, cpu, out, mode):
    while os.getloadavg()[0] >= 4: time.sleep(5)
    # Reuse S-D's real host/transport and FS_REQ drop boundary.
    from sd_run import Session, result

    class USBSession(Session):
        def start(self, number):
            original = subprocess.Popen
            def launch(argv, *args, **kwargs):
                argv = list(argv) + USB_QEMU_CLOCK + ['-usb', '-device', 'usb-mouse,id=mouse']
                (self.out/'argv.json').write_text(json.dumps(argv, indent=2))
                return original(argv, *args, **kwargs)
            subprocess.Popen = launch
            try:
                super().start(number)
            finally:
                subprocess.Popen = original

    s = USBSession(elf, cpu, out, mode)
    try:
        (s.root/'f').write_text('usb-rpc\n')
        s.start(1)
        # LAN-only startup precedes attaching the keyboard.
        s.q.call('device_add', {'driver':'usb-kbd', 'id':'kbd'})
        deadline = time.monotonic()+10
        while 'protocol=02' not in serial(s.a) or 'protocol=01' not in serial(s.a):
            require(time.monotonic() < deadline, 'HID enumeration before RPC')
            # Warm the real USER/USB closure while DEC continues in FG1.
            s.check('echo warm', 'warm\n')
        s.drop = True
        started = time.monotonic()
        failure = result(s.cmd('cat /host/f', 5), 'cat /host/f')
        require(failure.startswith('cat: /host/f: '), 'RPC absence diagnostic')
        require(s.saved is not None, 'FS_REQ reached drop boundary')
        def usb_keys(text):
            for c in text:
                code = 'ret' if c == '\n' else 'spc' if c == ' ' else c
                s.q.call('send-key', {'keys':[{'type':'qcode','data':code}], 'hold-time':100})
                end = time.monotonic()+.16
                while time.monotonic()<end: s.pump()
        offset = len(s.display.getvalue())
        usb_keys('echo hi\n')
        s.wait(lambda: s.display.getvalue()[offset:].endswith(b'sh> '), 5, 'USB post-RPC sh result')
        require(result(s.display.getvalue()[offset:].decode(), 'echo hi') == 'hi\n', 'USB post-RPC output')
        require('usb: HC dead' not in serial(s.a), 'healthy HC after RPC')
        # A LAN OPEN cancels the common line even when USB entered it.
        offset = len(s.display.getvalue())
        usb_keys('echo discarded')
        s.wait(lambda: s.display.getvalue()[offset:] == b'echo discarded',
               5, 'USB line echoed before LAN OPEN')
        s.terminal.open()
        s.wait(lambda: s.terminal.connected, 5, 'LAN reopen')
        # Drain cancellation before the LAN newline and next command.
        end=time.monotonic()+.2
        while time.monotonic()<end: s.pump()
        offset=len(s.display.getvalue())
        s.terminal.send_bytes(b'\n')
        s.wait(lambda: s.display.getvalue()[offset:].endswith(b'sh> '), 5, 'cancelled USB line prompt')
        require(b'\ndiscarded\n' not in s.display.getvalue()[offset:], 'LAN cancelled USB line')
        s.q.call('device_del', {'id':'kbd'})
        s.wait(lambda: 'USB RELEASE kind=0 src=1' in serial(s.a), 5, 'LAN USB unplug')
        s.drop = False
        s.check('echo alive', 'alive\n')
        validate(serial(s.a), serial(s.b), True)
        require('USB HC bdf=' in serial(s.a), 'HC startup marker')
        s.stop()
        print(('GREEN' if mode == 'normal' else 'RED')+' LAN '+mode+' '+cpu+
              ': RPC error, post-RPC verdict, live sh, QEMU rc=0 elapsed=%.3fs' %
              (time.monotonic()-started), flush=True)
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--lan', action='store_true', help='real hostfs RPC absence in FG1')
    args = parser.parse_args()
    require(stream_payload('log hi~USBSTREAM:00000001:hUSB KEY break\n~USBSTREAM:00000002:i\n') == 'hi\n',
            'framed stream reconstruction across Enter break log')
    cross = Path(os.environ.get('USB_PPC_CROSS_DIR', os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')))
    if not (cross/'bin/powerpc-elf-gcc').is_file() or any(
            shutil.which(t) is None for t in ('qemu-system-ppc', 'genisoimage')) or not ROM.is_file():
        missing = 'PPC toolchain' if not (cross/'bin/powerpc-elf-gcc').is_file() else 'QEMU/OpenBIOS/genisoimage'
        print('SKIP ppc-usb: ' + missing + ' unavailable')
        return 1 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
    if args.lan:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1', 0))
        except PermissionError:
            print('SKIP ppc-usb LAN: AF_INET unavailable')
            return 3 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
    base = Path(os.environ.get('TMPDIR', '/tmp'))/'usb-run'; base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    print('evidence='+str(work), flush=True)
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    if args.lan:
        for mode in ['normal']:
            opts = ['KBOOT_SH=1', 'PPC_CROSS_DIR='+str(cross)]
            elf = build(work/('lan-'+mode), *opts)
            for cpu in cpus if mode == 'normal' else cpus[:1]:
                lan_accept(elf, cpu, work/('lan-'+mode+'-'+cpu), mode)
        return 0
    for mode in ['normal'] + (['invert-key', 'no-forward'] if args.mutate else []):
        elf = build(work/('product-'+mode), 'PPC_CROSS_DIR='+str(cross), *product_settings(work, mode))
        for cpu in cpus if mode == 'normal' else cpus[:1]:
            product_accept(elf, cpu, work/('product-'+mode+'-'+cpu), mode)
    elf = build(work/'mouse-wait', 'PPC_CROSS_DIR='+str(cross),
                *product_settings(work, 'mouse-wait', late=True))
    for cpu in cpus:
        product_accept(elf, cpu, work/('mouse-wait-'+cpu), 'normal', late=True)
    for mode in ['normal'] + (['no-service', 'syscall-ee'] if args.mutate else []):
        elf = build(work/mode, 'PPC_CROSS_DIR='+str(cross), *settings(work, mode))
        for cpu in cpus if mode == 'normal' else cpus[:1]:
            if mode == 'syscall-ee':
                try:
                    accept(elf, cpu, work/(mode+'-'+cpu), mode)
                except CriterionFailure as exc:
                    require(str(exc).startswith("USB startup/reached markers:"),
                            "unexpected syscall EE verdict: "+str(exc))
                    evidence = serial(work/(mode+'-'+cpu)/'serial-a.log')
                    require('USB STALL ticks=0' in evidence, 'missing exact syscall EE regression')
                    print('RED syscall-ee: USB STALL ticks=0', flush=True)
                else:
                    raise RuntimeError('SURVIVED syscall-ee')
            else:
                accept(elf, cpu, work/(mode+'-'+cpu), mode)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
