#!/usr/bin/env python3
"""Independent PPC acceptance meter. No CHECK registration or BSD source reads.

All generated files, including mutation source copies, stay below make's OUT.
Each emulator has an absolute parent deadline and is reaped on every exit path.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import select
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
ROM = Path('/usr/share/qemu/openbios-ppc')
OF_LINE = b'OF: os32 ppc-boot\r\n'
ESCC_LINE = b'ESCC-A: os32 ppc-boot\r\n'
CASES = ('normal ac2 real screen bad-stdin bad-stdout bad-chosen bad-root bad-sc '
         'bad-memory bad-mmu bad-ac map-ci map-catch open-zero open-ci tree-empty '
         'bad-reg bad-bar high-bar wrap-addr bad-path legacy-parent tx-timeout '
         'drain-timeout delay-ready options-absent screen-fail').split()


def command(args, cwd=ROOT, timeout=30):
    p = subprocess.run([str(a) for a in args], cwd=cwd, capture_output=True,
                       timeout=timeout, text=True)
    if p.returncode:
        raise RuntimeError(f'{args}: rc={p.returncode}\n{p.stdout}{p.stderr}')
    return p.stdout + p.stderr


def paths(root):
    values = command(['make', '-s', '-f', 'build/ppc.mk', 'paths'], root).split()
    return [root / p for p in values]


def host(root, runner, cases=CASES):
    out = paths(root)[0]
    results = []
    for case in cases:
        p = subprocess.run((['qemu-i386'] if runner == 'qemu' else []) +
                           [str(out/'flow'), case], capture_output=True, timeout=10)
        results.append(f'{case}: rc={p.returncode}')
        if p.returncode:
            raise RuntimeError('host ' + '\n'.join(results))
    return '\n'.join(results) + '\n'


def static(root, cross):
    out, elf, *_ = paths(root)
    prefix = cross / 'bin' / 'powerpc-elf-'
    header = command([str(prefix)+'readelf', '-hln', elf])
    dis = command([str(prefix)+'objdump', '-d', elf])
    syms = command([str(prefix)+'nm', '-n', elf])
    undefined = command([str(prefix)+'nm', '-u', elf])
    (out/'readelf.log').write_text(header)
    (out/'objdump.log').write_text(dis)
    (out/'symbols.log').write_text(syms)
    assert not undefined.strip(), 'undefined symbols'
    data = elf.read_bytes()
    assert len(data) <= 24*1024, 'ELF budget'
    assert data[:7] == b'\x7fELF\x01\x02\x01', 'ELF32 big endian'
    h = struct.unpack_from('>HHIIIIIHHHHHH', data, 16)
    assert h[0:2] == (2, 20), 'EXEC / EM_PPC'
    symbols = {m[2]: int(m[0], 16) for line in syms.splitlines()
               if len(m := line.split()) == 3}
    assert h[3] == symbols['_start'], 'entry'
    ph = [struct.unpack_from('>8I', data, h[4]+i*h[8]) for i in range(h[9])]
    loads = [p for p in ph if p[0] == 1]
    notes = [p for p in ph if p[0] == 4]
    assert len(loads) == len(notes) == 1 and len(ph) == 2, 'LOAD/NOTE count'
    load, note = loads[0], notes[0]
    assert load[2:4] == (0x20000,0x20000) and load[6] == 7, 'LOAD address/flags'
    assert load[5] < 64*1024 and load[7] == 4, 'LOAD extent/alignment'
    assert note[1] >= load[1] and note[1]+note[4] <= load[1]+load[4], 'NOTE in LOAD'
    expected = struct.pack('>III8s5I',8,20,0x1275,b'PowerPC\0',0,*([0xffffffff]*4))
    assert data[note[1]:note[1]+note[4]] == expected, 'NOTE content'
    assert symbols['__stack_top'] % 16 == 0, 'stack alignment'
    assert symbols['__stack_top']-symbols['__stack_bottom'] == 8192, 'stack size'
    assert symbols['_edata'] <= symbols['__stack_bottom'] < symbols['_end'], 'BSS stack'
    instructions = re.findall(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} ){4}\s*(.*)$',dis,re.M)
    for ins in instructions:
        op = ins.split()[0]
        assert not re.match(r'^(f|v|xv|lfs|lfd|stfs|stfd|lv|stv|lxv|stxv)',op), f'FP/VMX {ins}'
    entry = dis.split('<_start>:\n',1)[1].split('<ppc_load8>:',1)[0]
    # Exact BSD non-601/non-970 sequence, allowing only address relocation.
    expected_ops = ['hwsync','isync','lis','addi','mfmsr','li','mtmsr','isync',
                    *(['mtibatu']*4),*(['mtdbatu']*4),'li','mtibatl','mtdbatl',
                    'li','mtibatu','mtdbatu','isync','mtmsr','isync','li',
                    'lis','addi','lis','addi','cmpw','bge','stb','stb','stb','stb',
                    'addi','b','stwu','mr','bl','b']
    entry_ins = re.findall(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} ){4}\s*(.*)$',entry,re.M)
    assert [x.split()[0] for x in entry_ins] == expected_ops, 'BAT/MSR/BSS instruction order'
    assert 'r9,18' in entry and 'r9,8190' in entry, 'BAT values'
    for offset in range(4):
        assert f'r0,{offset}(r8)' in entry, 'BSS byte stores'
    return symbols['ppc_stop_loop']


class QMP:
    def __init__(self, process, deadline):
        self.process = process
        self.deadline = deadline
        self.buffer = b''
        self.read()
        self.call('qmp_capabilities')

    def read(self):
        while b'\n' not in self.buffer:
            remaining = self.deadline-time.monotonic()
            if remaining <= 0 or not select.select([self.process.stdout],[],[],remaining)[0]:
                raise RuntimeError('QMP parent deadline')
            data = os.read(self.process.stdout.fileno(),4096)
            if not data:
                raise RuntimeError('QMP EOF')
            self.buffer += data
        line, self.buffer = self.buffer.split(b'\n',1)
        return json.loads(line)

    def call(self, name, arguments=None):
        request = {'execute':name}
        if arguments is not None:
            request['arguments'] = arguments
        self.process.stdin.write(json.dumps(request).encode()+b'\n')
        self.process.stdin.flush()
        while True:
            response = self.read()
            if 'error' in response:
                raise RuntimeError(str(response))
            if 'return' in response:
                return response['return']

    def close(self):
        self.process.stdin.close()
        self.process.stdout.close()


def qemu(root, timeout, stop_pc, expected_fail=None):
    out, elf, iso, a, b, qmp_path = paths(root)
    cd = out/'cd'
    cd.mkdir(exist_ok=True)
    shutil.copyfile(elf, cd/'PPCBOOT.ELF')
    command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
    qmp_path.unlink(missing_ok=True)
    deadline = time.monotonic()+timeout
    args = ['qemu-system-ppc','-M','mac99','-cpu','7450','-m','128M','-smp','1',
            '-accel','tcg','-bios',str(ROM),'-cdrom',str(iso),'-boot','d',
            '-prom-env','boot-device=cd:,\\PPCBOOT.ELF','-prom-env','auto-boot?=true',
            '-nographic','-serial','file:'+str(a),'-serial','file:'+str(b),
            '-monitor','none','-qmp','stdio','-no-reboot']
    q = None
    snapshots = []
    with (out/'qemu.stderr').open('w') as err:
        # QMP stdio avoids sandbox-denied AF_UNIX bind; serial still uses files.
        p = subprocess.Popen(args, cwd=root, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p,deadline)
            stable = 0
            while time.monotonic() < deadline:
                q.call('stop')
                registers = q.call('human-monitor-command', {'command-line':'info registers'})
                pc = re.search(r'NIP\s+([0-9a-fA-F]+)',registers)
                msr = re.search(r'MSR\s+([0-9a-fA-F]+)',registers)
                if pc and msr and int(pc[1],16) == stop_pc and not int(msr[1],16)&0x8000:
                    snapshots.append(registers)
                    stable += 1
                    if stable == 2:
                        break
                q.call('cont')
                time.sleep(min(0.1,max(0,deadline-time.monotonic())))
            assert stable == 2, f'stop PC/EE not observed twice; last registers:\n{registers}'
            (out/'qmp.log').write_text('\nRESTART/RESTOP\n'.join(snapshots))
            # OF serial write expands LF to CRLF; keep raw logs, normalize only
            # duplicated CR for comparison to the caller's CRLF message.
            serial_a = a.read_bytes().replace(b'\r\r\n',b'\r\n')
            serial_b = b.read_bytes().replace(b'\r\r\n',b'\r\n')
            assert serial_a.count(OF_LINE) == 1, 'OF stdout line count'
            assert OF_LINE not in serial_b, 'OF line on B'
            if expected_fail:
                assert expected_fail in serial_a, 'timeout FAIL/stop'
                if expected_fail.startswith(b'FAIL:tx:'):
                    assert ESCC_LINE not in serial_a, 'tx timeout wrote data'
                else:
                    assert serial_a.count(ESCC_LINE) == 1, 'drain timeout lost data'
            else:
                assert serial_a.count(ESCC_LINE) == 1, 'ESCC-A line count'
                assert b'FAIL:' not in serial_a, 'unexpected failure'
            assert ESCC_LINE not in serial_b, 'ESCC line on B'
            q.call('quit')
            p.wait(timeout=max(0.01,deadline-time.monotonic()))
            return f'PC=0x{stop_pc:x}, EE=0, observed twice\n'
        finally:
            if q:
                q.close()
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
            qmp_path.unlink(missing_ok=True)


# Each anchor is unique; replacement must compile. Runtime RED is mandatory.
MUTATIONS = [
    ('note','arch/ppc/ofw_note.S','PM_NOTE_NAMESZ, PM_NOTE_DESCSZ, PM_NOTE_TYPE',
     'PM_NOTE_NAMESZ, PM_NOTE_DESCSZ, PM_NOTE_TYPE+1','static'),
    ('method-order','platform/powermac/ofw.c','a[2+ni-1-i] = cells[i]',
     'a[2+i] = cells[i]','host'),
    ('data-before-rr0','platform/powermac/escc.c','unsigned wait = 0, rr0;',
     'unsigned wait = 0, rr0; ppc_store8(s->data,(unsigned char)p[i]);','host'),
    ('screen','platform/powermac/ofw.c','if (e->out == 0) e->out = ofw_open(PM_SCREEN);',
     'if (e->out == -99) e->out = ofw_open(PM_SCREEN);','host'),
    ('real-mode','platform/powermac/escc_tree.c','if (!e->real_mode &&',
     'if (e->real_mode &&','host'),
    ('map-catch','platform/powermac/ofw.c','if (a[ni+2] != 0)',
     'if (a[ni+2] == 0)','host'),
    ('of-line','platform/powermac/boot.c','"OF: os32 ppc-boot\\r\\n"',
     '"OF: os33 ppc-boot\\r\\n"','qemu'),
    ('data-suppress','platform/powermac/escc.c','ppc_store8(s->data,(unsigned char)p[i]);',
     '(void)s->data; (void)p[i];','qemu'),
    ('rr0','platform/powermac/ppc_layout.h','#define PM_RR0_TX_READY 0x04',
     '#define PM_RR0_TX_READY 0x00','tx'),
    ('rr1','platform/powermac/ppc_layout.h','#define PM_RR1_ALL_SENT 0x01',
     '#define PM_RR1_ALL_SENT 0x00','drain'),
    ('stop','arch/ppc/entry.S','ppc_stop_loop:\n    b ppc_stop_loop',
     'ppc_stop_loop:\n    b ppc_stop_other','qemu'),
]


def build(root, cross):
    return command(['make','-f','build/ppc.mk',f'CROSS_DIR={cross}'], root,120)


def acceptance(root, args, cases=CASES):
    result = host(root,args.runner,cases)
    pc = static(root,args.cross_dir)
    return result + qemu(root,args.timeout,pc)


def mutate(args):
    out = paths(ROOT)[0]
    results = []
    # Controls execute the same detector before and after each independent copy.
    for name, filename, before, after, detector in MUTATIONS:
        print(f'mutation {name}: GREEN control',flush=True)
        if detector == 'host': host(ROOT,args.runner)
        elif detector == 'static': static(ROOT,args.cross_dir)
        else: qemu(ROOT,args.timeout,static(ROOT,args.cross_dir))
        dest = out/'mutations'/name
        dest.mkdir(parents=True,exist_ok=True)
        for directory in ('arch/ppc','platform/powermac','tools/ppc','include','lib'):
            shutil.copytree(ROOT/directory,dest/directory,dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns('__pycache__'))
        (dest/'build').mkdir(exist_ok=True)
        shutil.copyfile(ROOT/'build/ppc.mk',dest/'build/ppc.mk')
        product = dest/filename
        source = product.read_text()
        assert source.count(before) == 1, f'nonunique mutation anchor: {name}'
        product.write_text(source.replace(before,after))
        (dest/'mutation.json').write_text(json.dumps((name,filename,before,after,detector),indent=2))
        (dest/'build.log').write_text(build(dest,args.cross_dir))
        try:
            if detector == 'host': host(dest,args.runner)
            elif detector == 'static': static(dest,args.cross_dir)
            elif detector in ('tx','drain'):
                try:
                    host(dest,args.runner,['normal'])
                except RuntimeError as exc:
                    (dest/'host-red.log').write_text(str(exc))
                else:
                    raise RuntimeError('mask mutation host unexpectedly GREEN')
                pc = static(dest,args.cross_dir)
                # First require the normal criterion to go RED. Then observe the
                # intended deadline failure and the correct stopped PC explicitly.
                try:
                    qemu(dest,args.timeout,pc)
                except AssertionError as exc:
                    (dest/'normal-red.log').write_text(str(exc))
                else:
                    raise RuntimeError('mask mutation QEMU unexpectedly GREEN')
                qemu(dest,args.timeout,pc,f'FAIL:{detector}:-1\r\n'.encode())
                raise AssertionError('normal criterion RED; FAIL and stop observed')
            else: qemu(dest,args.timeout,static(dest,args.cross_dir))
        except (AssertionError,RuntimeError) as exc:
            if detector in ('tx','drain') and str(exc) != 'normal criterion RED; FAIL and stop observed':
                raise
            (dest/'red.log').write_text(str(exc)+'\n')
        else:
            raise RuntimeError(f'mutation survived: {name}')
        if detector == 'host': host(ROOT,args.runner)
        elif detector == 'static': static(ROOT,args.cross_dir)
        else: qemu(ROOT,args.timeout,static(ROOT,args.cross_dir))
        results.append((name,filename,before,after,detector,'GREEN/RED/GREEN'))
        print(f'mutation {name}: GREEN/RED/GREEN (build rc=0)',flush=True)
    (out/'mutations.json').write_text(json.dumps(results,indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--timeout',type=float,default=30)
    p.add_argument('--mutate',action='store_true')
    p.add_argument('--cross-dir',type=Path,default=Path('/home/hight/opt/cross-ppc'))
    p.add_argument('--runner',choices=('qemu','native'),default=os.environ.get('HOST32_RUNNERS','qemu'))
    args = p.parse_args()
    assert args.timeout > 0
    out = paths(ROOT)[0]
    out.mkdir(parents=True,exist_ok=True)
    metadata = command(['qemu-system-ppc','--version'])
    metadata += 'ROM SHA256 '+hashlib.sha256(ROM.read_bytes()).hexdigest()+'\n'
    (out/'environment.log').write_text(metadata)
    print(metadata,flush=True)
    (out/'build.log').write_text(build(ROOT,args.cross_dir))
    result = acceptance(ROOT,args)
    (out/'acceptance.log').write_text(result)
    print(result,flush=True)
    if args.mutate:
        mutate(args)
        # Restore the last observation to the normal image.
        result = acceptance(ROOT,args)
        (out/'acceptance.log').write_text(result)
    print('PASS ppc-boot',flush=True)


if __name__ == '__main__':
    try:
        main()
    except (AssertionError,RuntimeError,subprocess.TimeoutExpired,OSError) as exc:
        print(f'FAIL ppc-boot: {exc}',file=sys.stderr,flush=True)
        sys.exit(1)
