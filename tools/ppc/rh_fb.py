#!/usr/bin/env python3
"""RH1: real mac99 display through OF capture, detach and plan-D aliases."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from run import ROOT, ROM, QMP, command, require
import kboot_boot as boot
from kboot_vectors import symbols




def guest(work, elf, mutant):
    syms = symbols(elf)
    cd = work/'cd'; cd.mkdir(exist_ok=True)
    shutil.copyfile(elf, cd/'BOOT.ELF')
    iso = work/'boot.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', '7450', '-m', '128M',
            '-accel', 'tcg', '-bios', ROM, '-cdrom', iso, '-boot', 'd',
            '-prom-env', 'boot-device=cd:,\\BOOT.ELF', '-prom-env', 'auto-boot?=true',
            '-display', 'none', '-serial', 'file:'+str(work/'serial.log'),
            '-monitor', 'none', '-qmp', 'stdio', '-no-reboot']
    q = None
    with (work/'stderr.log').open('w') as err:
        p = subprocess.Popen([str(a) for a in args], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            deadline = time.monotonic()+45
            q = QMP(p, deadline)
            stable = 0
            while time.monotonic() < deadline:
                q.call('stop')
                regs = q.call('human-monitor-command', {'command-line': 'info registers'})
                pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                stable = stable+1 if pc and int(pc[1], 16) == syms['fixture_done'] else 0
                if stable == 2: break
                q.call('cont'); time.sleep(0.1)
            (work/'registers.log').write_text(regs)
            if stable != 2:
                raise RuntimeError('guest did not reach designated fixture_done stop')
            serial = (work/'serial.log').read_bytes()
            require(b'PAGING:MAIN' in serial and b'BOOT:PASS' in serial,
                    'missing reached paging/boot marks')
            shot = work/'screen.ppm'
            q.call('screendump', {'filename': str(shot)})
            data = shot.read_bytes()
            magic, size, maximum, pixels = data.split(b'\n', 3)
            require(magic == b'P6' and maximum == b'255', 'PPM encoding')
            width, height = map(int, size.split())
            require(width >= 640 and height >= 64, 'display geometry')
            # Independent raster of the large white digit 1 in slot 1.
            pattern = ['00100','01100','00100','00100','00100','00100','01110']
            cell = width//16; scale = min(cell//6, 4)
            good = True
            for y in range(7*scale):
                for x in range(6*scale):
                    white = x < 5*scale and pattern[y//scale][x//scale] == '1'
                    off = ((y*width)+cell+x)*3
                    good &= pixels[off:off+3] == (b'\xff'*3 if white else b'\0'*3)
            require(good != mutant, 'framebuffer stage raster verdict mismatch')
            verdict = ('RED missing stage raster: fixture_done + paging/boot marks + pixel mismatch'
                       if mutant else 'GREEN real OF display -> detach -> I/G plan D -> framebuffer digit 1')
            print(verdict, flush=True)
            q.call('quit'); p.wait(timeout=2)
            require(p.returncode == 0, 'unexpected QEMU exit status')
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    if not (cross/'bin/powerpc-elf-gcc').is_file() or not shutil.which('qemu-system-ppc'):
        print('SKIP ppc-rh-fb: PPC compiler/QEMU unavailable'); return 0
    started = time.monotonic()
    out = Path(os.environ['TMPDIR'])/'rh-fb'; out.mkdir(parents=True, exist_ok=True)
    source = (ROOT/'tools/ppc/kboot_boot_guest.c').read_text().replace(
        '#include "paging.h"', '#include "paging.h"\n#include "rh_observe.h"').replace(
        '    say("PAGING:MAIN', '    pm_rh_bind(ppc_boot_devices()->fb_va);\n    pm_rh_stage(1);\n    say("PAGING:MAIN')
    fixture = out/'guest.c'; fixture.write_text(source)
    boot.PRODUCT = [str(fixture) if p == 'tools/ppc/kboot_boot_guest.c' else p for p in boot.PRODUCT]
    boot.CROSS = str(cross/'bin/powerpc-elf-')
    control = out/'control'; control.mkdir(exist_ok=True)
    guest(control, boot.build(control), False)
    if args.mutate:
        mutation = ('rh-stage-write', 'platform/powermac/rh_observe.c',
                    '    if (!pixels) return;\n    u32 cell', '    return;\n    u32 cell')
        work = out/'no-stage'; work.mkdir(exist_ok=True)
        guest(work, boot.build(work, mutation=mutation), True)
    print(f'PASS ppc-rh-fb {time.monotonic()-started:.2f}s')
    return 0


if __name__ == '__main__':
    sys.exit(main())
