#!/usr/bin/env python3
"""Expectation origin: PPC_FP_NEWLIB_DESIGN §8 F1〜F5・B1.

link-SH USER acceptance. F3 is outside this runner (test_ppc_fp covers the
kernel guard); B1 is enforced by the existing build audit. The FP probe uses
raw KAPI output; USER startup links the shared newlib closure.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tools/ppc')]
MUTATIONS = (
    ('F1-entry', 'sys/arch/ppc/exec_enter.S', 'PPC_MSR_ME|PPC_MSR_FP|PPC_MSR_IR', 'PPC_MSR_ME|PPC_MSR_IR'),
    ('F2-fpr17', 'sys/arch/ppc/fp_context.S', '    stfd 17,136(3)', '    nop'),
    ('F2-fpscr', 'sys/arch/ppc/fp_context.S', '    stfd 0,PPC_FP_FPSCR_OFFSET(3)', '    nop'),
    ('F4-retry', 'sys/arch/ppc/kernel_trap.c', '    if (!f || !translated) goto fatal;',
     '    if (!f || !translated) goto fatal;\n    if (f->vector == 0x800) return;'),
    ('F5-init', 'sys/arch/ppc/fp_context.S', '    b arch_fp_restore', '    blr'),
)

STIMULUS = ('F4-resume', 'sys/arch/ppc/kernel_vectors.S',
            '    ori 3,3,PPC_MSR_FP', '    nop')
# Every return to USER (IRQ/syscall via kernel_vectors.S, park via exec_resume.S)
# sets MSR[FP] again, so an entry-only mutation is masked by the first DEC tick.
# F1-entry therefore removes FP from all three USER entries; RED = 0x800 kill
# before the F1 output (F4 instead fails after F4-before).
RESUME = ('F1-resume', 'sys/arch/ppc/exec_resume.S', '    ori 0,0,PPC_MSR_FP', '    nop')


def settings(work, name):
    user = work/'user'
    opts = ['KBOOT_SH=1', 'KBOOT_LINK=1', 'KB_SH_OUT='+str(user),
            'KB_ROOT_BINS='+str(user/'fp_probe.bin')]
    edits = []
    if name in ('F4', 'F4-retry'):
        edits = [MUTATIONS[0], STIMULUS]
    if name == 'F1-entry':
        edits = [MUTATIONS[0], RESUME, STIMULUS]
    elif name not in ('normal', 'F4'):
        edits.append(next(m for m in MUTATIONS if m[0] == name))
    for slot, (tag, source, old, new) in zip(('', 'EXTRA_', 'THIRD_'), edits):
        text = (ROOT/source).read_text()
        if text.count(old) != 1: raise RuntimeError('unique anchor '+tag)
        dest = (work/(tag+Path(source).suffix)).resolve()
        dest.write_text(text.replace(old, new))
        opts += ['KB_MUT_'+slot+'SRC='+source, 'KB_MUT_'+slot+'FILE='+str(dest)]
    if len(edits) > 3: raise RuntimeError('mutation slots')
    return opts


def verdict(case, body, status, diagnostic, timeout=False):
    """Pure observation classifier: unrelated faults/timeouts never count RED."""
    # sh adds one '\n' after an external program that exits with success.
    if status == b'0\n' and body.endswith(b'\n\n'): body = body[:-1]
    fault = re.search(r'FATAL trap vector=0x800 reason=0x11\b', diagnostic) is not None
    if 'FATAL' in diagnostic and (not fault or diagnostic.count('FATAL') != 1):
        return 'wrong-reason'
    if case == 'F4-retry':
        return 'timeout' if timeout and not body and status is None and not fault else 'wrong-reason'
    if timeout: return 'wrong-reason'
    if case == 'F1-entry':
        return 'kill' if fault and b'F1=' not in body and b'FP-exit=' not in body and status != b'0\n' else 'wrong-reason'
    if case == 'F4':
        return 'kill' if fault and b'F4-before\n' in body and b'F4-after' not in body and b'FP-exit=' not in body and status != b'0\n' else 'wrong-reason'
    codes = {'F2-fpr17': 17, 'F2-fpscr': 33, 'F5-init': 5}
    if case in codes:
        code = str(codes[case]).encode()
        return code.decode() if not fault and body.endswith(b'FP-exit='+code+b'\n') and status == code+b'\n' else 'wrong-reason'
    return 'GREEN' if not fault and body.endswith(b'FP-exit=0\n') and status == b'0\n' else 'wrong-reason'


def observe(media, cpu, out, case):
    from link_sh_run import Session
    from usb_run import rsh_command
    from run import require
    s = Session(media, cpu, out, 240)
    try:
        s.start()
        s.wait(s.ready, 'FP cui READY', 60)
        require(s.agent.operator.ready['mode'] == 'cui', 'FP mode')
        modes = ('1', '2', '5', '4') if case == 'normal' else (
            '1' if case == 'F1-entry' else '2' if case.startswith('F2') else
            '5' if case == 'F5-init' else '4',)
        for mode in modes:
            line = '/bin/fp_probe.bin '+mode
            before = (out/'serial-a.log').read_text(errors='replace')
            body, status, timeout = b'', None, False
            try:
                body = rsh_command(s, line)
            except RuntimeError as exc:
                if case != 'F4-retry' or not str(exc).startswith('deadline RSH result '+line+' evidence='):
                    raise
                require(s.p.poll() is None, 'retry QEMU alive')
                timeout = True
            if not timeout: status = rsh_command(s, 'echo $?')
            serial = (out/'serial-a.log').read_text(errors='replace')[len(before):]
            result = verdict(case, body, status, serial, timeout)
            if case == 'normal':
                # sh prints one '\n' after an external program exits with success
                # (userland/shell/sh_exec.inc run_cmd_report), as ppc_sh_output expects.
                expected = {'1': b'F1=2\nFP-exit=0\n\n',
                            '2': b'FP-exit=0\nFP-exit=0\n\n',
                            '5': b'FP-exit=0\nFP-exit=0\n\n',
                            '4': b'F4-before\nF4-after\nFP-exit=0\n\n'}
                require(body == expected[mode], 'FP normal output '+mode)
            require(result != 'wrong-reason', case+' wrong-reason: '+repr((body, status, serial, timeout)))
            print(('GREEN ' if case in ('normal', 'F4') else 'RED ')+case+' '+cpu+' '+mode+': '+result, flush=True)
        s.q.call('quit'); s.p.wait(timeout=5)
        require(s.p.returncode == 0, 'FP QEMU exit')
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cpu', choices=('7450', '7455', 'both'), default='both')
    ap.add_argument('--plan', action='store_true')
    ap.add_argument('--emit-mutants', type=Path)
    ap.add_argument('--out', type=Path, default=Path(os.environ.get('TMPDIR', '/tmp'))/'ppc-fp')
    ap.add_argument('--build-only', action='store_true')
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
    missing = not shutil.which(str(cc))
    if not (args.plan or args.emit_mutants or args.build_only):
        missing |= any(not shutil.which(t) for t in ('qemu-system-ppc', 'genisoimage'))
        missing |= not Path('/usr/share/qemu/openbios-ppc').is_file()
    if missing:
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP')+': PPC FP dependencies unavailable')
        return int(required)
    names = ['normal', 'F4'] + ([m[0] for m in MUTATIONS] if args.mutate or args.emit_mutants else [])
    if args.plan:
        print(json.dumps(dict(cpu=args.cpu, cases=names, F3='対象外: host test_ppc_fp F3',
                              image='link-SH'), ensure_ascii=False))
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.out.resolve()))
    print('evidence='+str(work)+'; F3 対象外: host test_ppc_fp F3', flush=True)
    if not (args.emit_mutants or args.build_only):
        try:
            for kind in (socket.SOCK_DGRAM, socket.SOCK_STREAM):
                with socket.socket(socket.AF_INET, kind) as probe: probe.bind(('127.0.0.1', 0))
        except OSError as exc:
            print(('FAIL' if os.environ.get('OS32_REQUIRE_PPC') == '1' else 'SKIP')+': FP socket '+str(exc))
            return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    from kboot_accept import build_media
    cpus = ('7450', '7455') if args.cpu == 'both' else (args.cpu,)
    if args.emit_mutants:
        work = args.emit_mutants.resolve(); work.mkdir(parents=True, exist_ok=True)
    seen = set()
    for name in names:
        folder = work/name; folder.mkdir()
        opts = settings(folder, name)
        (folder/'settings.json').write_text(json.dumps(opts, indent=2))
        if args.emit_mutants: continue
        media = build_media(folder/'kernel', *opts)
        if name == 'normal':
            normal_media = media
        digest = hashlib.sha256(media.elf.read_bytes()).hexdigest()
        assert digest not in seen, (name, digest)
        seen.add(digest)
        print(name, digest, flush=True)
        if args.build_only: continue
        for cpu in cpus:
            observe(media, cpu, folder/cpu, name)
            # Each mutant has the original image as a following normal control.
            if name not in ('normal', 'F4'):
                observe(normal_media, cpu, folder/(cpu+'-control'), 'normal')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
