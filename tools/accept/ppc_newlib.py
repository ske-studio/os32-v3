#!/usr/bin/env python3
"""Expectation origin: PPC_FP_NEWLIB_DESIGN §4 N3・§8 / astra レビュー 2026-11-18.

heap の回収は DEFERRED PPC-NEWLIB-HEAP-RECLAIM.

link-SH/RSH acceptance on 7450/7455. Only acceptance images contain the probe.
Runtime RED requires the exact output/status/reason; crashes are errors.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tools/ppc'), str(ROOT/'sdk/allocator')]
MUTATIONS = (
    ('init', 'sdk/crt/ppc/start.c', '    _init();', ''),
    # sbss: without the app.ld rule ld places an orphan .sbss after .bss, outside
    # __bss_end, and sdk/mkos32x.py rejects the image ("orphan alloc section")
    # before any guest runs. The host test (test_ppc_newlib, nano.sentinel)
    # keeps that mutant RED instead.
    ('float', 'build/ppc-user.mk', 'PPC_USER_FLOAT ?= -u _printf_float', 'PPC_USER_FLOAT ?='),
    ('adapter', 'sdk/allocator/nano_adapter.c',
     'void *malloc(size_t size) { return _malloc_r(_impure_ptr, size); }',
     'void *malloc(size_t size) { return size == 65535 ? NULL : _malloc_r(_impure_ptr, size); }'),
)
FLOAT = b'N3-float=1.25,-2.50,3.75\n'


def verdict(case, body, status, diagnostic):
    if 'FATAL' in diagnostic: return 'wrong-reason'
    code = {'normal': 0, 'init': 11, 'sbss': 12, 'adapter': 21, 'float': 0}[case]
    # shell appends a newline after success, as ppc_sh_output specifies.
    expected = (b'N3-float=,,\n' if case == 'float' else FLOAT)
    expected += b'N3-exit='+str(code).encode()+b'\n'+(b'\n' if code == 0 else b'')
    return ('GREEN' if case == 'normal' else 'RED-'+case) if (
        body == expected and status == str(code).encode()+b'\n') else 'wrong-reason'


def settings(work, case):
    user = work/'user'
    opts = ['KBOOT_SH=1', 'KBOOT_LINK=1', 'KB_SH_OUT='+str(user),
            'KB_ROOT_BINS='+str(user/'nlprobe.bin')]
    manifest = json.loads((ROOT/'build/ppc-ram-root.json').read_text())
    # The product manifest supplies /sys/shell.bin.
    manual=work/'hello.1'; manual.write_text('N3-help-found\n')
    # hello only prints a string. Keep the float formatter in nlprobe, where it is tested.
    hello = work/'helper/hello.bin'
    subprocess.run(['make', 'ARCH=ppc', 'PLATFORM=powermac', 'PPC_USER_OUT='+str(hello.parent),
                    'PPC_USER_FLOAT=', str(hello)], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    opts[-1] += ' '+str(hello)
    manifest['/usr/man/hello.1']=str(manual)
    path=work/'root.json'; path.write_text(json.dumps(manifest))
    opts += ['ROOT_MANIFEST='+str(path), 'ROOT_DEPS=assets/fonts/shino16.kcgfont assets/fonts/JF-Dot-Shinonome16-LICENSE.txt build/out/unicode.bin '+str(manual)+' '+str(hello)]
    if case != 'normal':
        _, source, old, new = next(m for m in MUTATIONS if m[0] == case)
        text=(ROOT/source).read_text()
        if text.count(old) != 1: raise RuntimeError('unique anchor '+case)
        path=work/('app.ld' if case == 'sbss' else 'mutant'+Path(source).suffix); path.write_text(text.replace(old,new))
        if case == 'init': opts += ['PPC_USER_START_SRC='+str(path)]
        elif case == 'sbss': opts += ['SDK_TARGET_APP_LINK='+str(path)]
        elif case == 'float': opts += ['PPC_USER_FLOAT=']
        # adapter is built after the normal USER closure, before the kernel.
    return opts


def observe(elf, cpu, out, case):
    from link_sh_run import Session
    from usb_run import rsh_command
    from run import require
    s=Session(elf,cpu,out,240)
    try:
        s.start(); s.wait(s.ready,'newlib cui READY',60)
        require(s.agent.operator.ready['mode'] == 'cui', 'newlib mode')
        def command(line):
            body=rsh_command(s,line)
            status=rsh_command(s,'echo $?')
            return body,status
        for repeat in range(2):
            before=(out/'serial-a.log').read_text(errors='replace')
            body,status=command('/bin/nlprobe.bin')
            serial=(out/'serial-a.log').read_text(errors='replace')[len(before):]
            result=verdict(case,body,status,serial)
            require(result != 'wrong-reason', str((case,body,status,serial)))
            print(result+' '+cpu+' repeat='+str(repeat),flush=True)
        if case == 'normal':
            body,status=command('/bin/nlprobe.bin crt-return')
            require(status == b'37\n', 'CRT return status')
            require(body == b'ACB', 'CRT return output')
            body,status=command('/bin/nlprobe.bin help-missing')
            require(body == FLOAT+FLOAT+b'N3-exit=0\nN3-exit=0\n\n' and status == b'0\n','missing CRT help delegates to main')
            body,status=command('/bin/nlprobe.bin exit')
            require(body == FLOAT+b'N3-exit=7\n' and status == b'7\n','exit status')
            body,status=command('/bin/nlprobe.bin help-found')
            require(b'N3-help-found' in body and status == b'0\n','found help skips main (hello normally exits 7)')
            body,status=command('/bin/hello.bin world')
            require(b'hello world\n' in body and status == b'7\n','external exec')
            body,status=command('echo N3-shell')
            require(body == b'N3-shell\n' and status == b'0\n','shell builtin')
            def capacity():
                body, status = command('/bin/nlprobe.bin resources')
                import re
                match = re.fullmatch(FLOAT + rb'N3-resources fd=([0-9]+)\nN3-exit=0\n\n', body)
                require(match is not None and status == b'0\n', 'resource baseline: '+repr((body, status)))
                return match.group(1)
            baseline = capacity()
            print('resources '+cpu+' baseline='+str(baseline), flush=True)
            body,status=command('/bin/nlprobe.bin retain')
            require(status == b'0\n' and b'N3-resources' in body, 'normal exit with live resources')
            require(capacity() == baseline, 'normal exit FD recovery')
            body,status=command('/bin/nlprobe.bin retain-fault')
            require(status == b'139\n' and b'N3-resources' in body and b'N3-exit=' not in body, 'fault with live resources')
            require(capacity() == baseline, 'fault FD recovery')
        s.q.call('quit'); s.p.wait(timeout=5)
        require(s.p.returncode == 0,'newlib QEMU exit')
    finally: s.close()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cpu',choices=('7450','7455','both'),default='both')
    ap.add_argument('--plan',action='store_true')
    ap.add_argument('--build-only',action='store_true')
    ap.add_argument('--mutate',action='store_true')
    ap.add_argument('--out',type=Path,default=Path(os.environ.get('TMPDIR','/tmp'))/'ppc-newlib')
    args=ap.parse_args()
    prefix=Path(os.environ.get('PPC_CROSS_DIR','/home/hight/opt/cross-ppc'))
    missing=not (prefix/'bin/powerpc-elf-gcc').is_file()
    if not (args.plan or args.build_only):
        missing |= any(not shutil.which(t) for t in ('qemu-system-ppc','genisoimage'))
        missing |= not Path('/usr/share/qemu/openbios-ppc').is_file()
        if not missing:
            try:
                for kind in (socket.SOCK_DGRAM,socket.SOCK_STREAM):
                    with socket.socket(socket.AF_INET,kind) as probe: probe.bind(('127.0.0.1',0))
            except OSError: missing=True
    if missing:
        required=os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP')+': PPC newlib dependencies/socket unavailable')
        return int(required)
    cases=['normal']+([m[0] for m in MUTATIONS] if args.mutate else [])
    if args.plan:
        print(json.dumps(dict(image='link-SH',transport='RSH',cpu=args.cpu,cases=cases,
                              basis='PPC_FP_NEWLIB_DESIGN §8 N2・N3'),ensure_ascii=False)); return 0
    args.out.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='run-',dir=args.out.resolve()))
    print('evidence='+str(work),flush=True)
    from kboot_accept import build
    from build_nano_target import build as build_nano
    cpus=('7450','7455') if args.cpu == 'both' else (args.cpu,)
    seen = set()
    for case in cases:
        folder=work/case; folder.mkdir()
        opts=settings(folder,case)
        if case == 'adapter':
            user=folder/'user'
            subprocess.run(['make','ARCH=ppc','PLATFORM=powermac','CROSS_DIR='+str(prefix),
                'PPC_USER_OUT='+str(user),
                str(user/'nlprobe.bin')],cwd=ROOT,check=True)
            build_nano(prefix,user/'nano',adapter=folder/'mutant.c',crt=user/'syscalls.o')
        elf=build(folder/'kernel',*opts)
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        assert digest not in seen, (case, digest)
        seen.add(digest)
        print(case, digest, flush=True)
        if args.build_only: continue
        for cpu in cpus:
            observe(elf,cpu,folder/cpu,case)
            if case != 'normal':
                observe(work/'normal/kernel/PPCKERN.ELF',cpu,folder/(cpu+'-control'),'normal')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
