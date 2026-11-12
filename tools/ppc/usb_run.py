#!/usr/bin/env python3
"""PowerMac USB acceptance over serial only (no LAN socket)."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from kboot_accept import build
from kboot_run import ROOT, ROM, QMP, command, require, serial, validate, PREFIX

CHECK = dict(name='ppc-usb', tier=2, budget=900, needs=['make:all'],
             covers=['tools/ppc/usb_run.py', 'platform/powermac/usb*', 'build/ppc-kboot.mk', 'init/ppc_kernel.c',
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
                     'tools/serialfs_host.py'])

PROBE = r"""
#include "kernel_tick.h"
#include "kprintf.h"
static int usb_probe_input(void)
{
    static u32 start;
    if (!start) start = tick_count;
    if ((u32)(tick_count - start) >= 1200) {
        kprintf(7, "USB ACCEPT END\n");
        __asm__ volatile("mfmsr 3; rlwinm 3,3,0,17,15; mtmsr 3; isync" ::: "r3", "memory");
        __asm__ volatile(".global usb_accept_stop\nusb_accept_stop: b usb_accept_stop");
        __builtin_unreachable();
    }
    return -1;
}
"""


def settings(work, mode):
    # As in sc_run's serial mode, substitute only the external LAN connection
    # and byte-input boundaries. USER sh and the USB product closure are real.
    boot = (ROOT/'platform/powermac/shell_boot.c').read_text()
    boot = boot.replace('while (!pm_term_connected()) pm_terminal_poll();',
                        '/* Serial acceptance supplies the terminal connection. */')
    inp = (ROOT/'platform/powermac/input_boot.c').read_text()
    inp = inp.replace('static PmInputDecoder', PROBE+'\nstatic PmInputDecoder')
    inp = inp.replace('pm_terminal_trygetkey()', 'usb_probe_input()')
    result = ['KBOOT_SH=1']
    for slot, src, content in [('', 'platform/powermac/shell_boot.c', boot),
                                ('EXTRA_', 'platform/powermac/input_boot.c', inp)]:
        dest = work/(mode+'-'+Path(src).name)
        dest.write_text(content)
        result += ['KB_MUT_'+slot+'SRC='+src, 'KB_MUT_'+slot+'FILE='+str(dest)]
    if mode != 'normal':
        src = 'platform/powermac/usb_platform.c'
        content = (ROOT/src).read_text()
        before, after = {
            'no-tick': ('return tick_register(PM_TICK_USB, fn);', '(void)fn; return 0;'),
            'invert-key': ('src, sc, down);', 'src, sc, !down);'),
        }[mode]
        require(content.count(before) == 1, 'unique USB mutation anchor')
        dest = work/(mode+'-usb_platform.c')
        dest.write_text(content.replace(before, after))
        result += ['KB_MUT_THIRD_SRC='+src, 'KB_MUT_THIRD_FILE='+str(dest)]
    return result


def accept(elf, cpu, out, mode):
    out.mkdir()
    cd = out/'cd'; cd.mkdir()
    shutil.copyfile(elf, cd/'PPCKERN.ELF')
    iso = out/'boot.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    a, b = out/'serial-a.log', out/'serial-b.log'
    args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-smp', '1',
            '-accel', 'tcg', '-bios', str(ROM), '-drive', f'file={iso},media=cdrom,id=bootcd',
            '-boot', 'd', '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
            '-prom-env', 'auto-boot?=true', '-nographic', '-serial', 'file:'+str(a),
            '-serial', 'file:'+str(b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot',
            '-nic', 'none', '-usb', '-device', 'usb-kbd,id=kbd', '-device', 'usb-mouse,id=mouse']
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
                ready = 'sh> ' in text
                keyboard = re.search(r'USB DEV addr=(\d+) vid:pid=0627:0001 class=03 subclass=01 protocol=01', text)
                mouse = re.search(r'USB DEV addr=(\d+) vid:pid=0627:0001 class=03 subclass=01 protocol=02', text)
                if phase == 0 and ready and ((keyboard and mouse) or mode == 'no-tick'):
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
                    require(pc and int(pc[1], 16) == address, 'USB exact stop PC')
                    validate(text, serial(b), True)
                    require(ready and phase == 6 and 'USB HC bdf=' in text and 'USB TAKEOVER quiet' in text,
                            'USB startup/reached markers: '+text)
                    keys = [(int(sc, 16), int(down)) for sc, down in
                            re.findall(r'USB KEY src=1 sc=([0-9a-f]+) down=(\d)', text)]
                    expected = [(0x1d, 1), (0x1d, 0), (0x70, 1), (0x1d, 1),
                                (0x1d, 0), (0x70, 0), (0x1c, 1), (0x1c, 0), (0x1d, 1)]
                    if mode == 'no-tick':
                        require(not keyboard and not mouse and not keys and 'USB MOUSE' not in text,
                                'no-tick exact missing enumeration/input verdict')
                    else:
                        require(keyboard and mouse, 'HID identities: '+text)
                        if mode == 'invert-key': expected = [(sc, 1-down) for sc, down in expected]
                        require(keys == expected, 'PC98 make/break order: '+repr(keys))
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross/'bin/powerpc-elf-gcc').is_file() or any(
            shutil.which(t) is None for t in ('qemu-system-ppc', 'genisoimage')) or not ROM.is_file():
        print('SKIP ppc-usb: PPC toolchain/QEMU/OpenBIOS unavailable')
        return 0
    base = Path(os.environ.get('TMPDIR', '/tmp'))/'usb-run'; base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    print('evidence='+str(work), flush=True)
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    for mode in ['normal'] + (['no-tick', 'invert-key'] if args.mutate else []):
        elf = build(work/mode, 'PPC_CROSS_DIR='+str(cross), *settings(work, mode))
        for cpu in cpus if mode == 'normal' else cpus[:1]:
            accept(elf, cpu, work/(mode+'-'+cpu), mode)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
