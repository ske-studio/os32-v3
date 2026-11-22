#!/usr/bin/env python3
"""PPC_P5_REST_DESIGN §3/§5 TX0 directional capture fixtures (no guest)."""
import argparse
import json
from pathlib import Path
import sys
import subprocess
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/ppc'), str(ROOT/'tools')]
import kboot_run as meter

CHECK = dict(name='ppc-tx0', tier=1, budget=10,
    covers=['tools/tests/test_ppc_tx0.py', 'tools/ppc/**', 'tools/*.py', 'build/targets/ppc-powermac.mk',
            'build/*.mk', 'build/*.json'],
    basis=['PPC_P5_REST_DESIGN §3 TX0・§5'])


def exercise(work, mutate):
    original = (ROOT/'build/images/ppc-flags.mk').read_text()
    def selection(source):
        mk = work/'default.mk'; mk.write_text(source)
        rule = 'selection:;@echo $(KBOOT_LINK) $(KB_OUT)'
        for flags, suffix in [([], 'ppc-powermac'), (['KBOOT_SH=0'], 'ppc-kboot/link'), (['KBOOT_SH=1'], 'ppc-kboot/link-sh')]:
            result = subprocess.check_output(['make', '-s', 'ARCH=ppc', 'PLATFORM=powermac', 'PPC_FLAGS_MK='+str(mk), *flags,
                '--eval='+rule, 'selection'], cwd=ROOT, text=True).strip()
            assert result == '1 build/out/'+suffix, result
    selection(original)
    if mutate:
        anchor = '$(if $(KB_PRODUCT),powermac,$(KB_IMAGE))'
        assert original.count(anchor) == 1
        try:
            selection(original.replace(anchor, '$(KB_IMAGE)'))
        except AssertionError:
            print('RED legacy-default: actual make selection')
        else:
            raise AssertionError('legacy default survived')
        selection(original)
    # EtherType, not payload signatures or MAC address, selects PLN1.
    frame = bytes.fromhex('ffffffffffff52540012345688b5') + bytes(46)
    pln1 = frame[:12] + b'\x88\xb6' + frame[14:]
    good = frame.hex()+'\n> '+pln1.hex()+'\n'+frame.hex()+'\n'
    path = work/'frames.log'
    path.write_text(good)
    assert meter.link_tx0(path) == dict(guest_frames=2, host_frames=1, guest_pln1_tx=0)
    bad = [('guest-PLN1', good+pln1.hex()+'\n', 'TX0 guest PLN1 transmission'),
           ('empty', '', 'TX0 empty/incomplete capture'),
           ('unfinished', good[:-1], 'TX0 empty/incomplete capture'),
           ('short', good+'00\n', 'TX0 malformed/short frame'),
           ('broken', good+'xyz\n', 'TX0 malformed/short frame'),
           ('odd-hex', good+'0'*29+'\n', 'TX0 malformed/short frame'),
           ('blank-record', good+'\n', 'TX0 malformed/short frame'),
           ('host-only', '> '+pln1.hex()+'\n', 'TX0 no guest frames')]
    for name, text, reason in bad:
        path.write_text(text)
        try:
            meter.link_tx0(path)
        except meter.CriterionFailure as exc:
            assert str(exc) == reason, (name, str(exc))
            if mutate:
                print('RED '+name+': '+reason)
        else:
            raise AssertionError('accepted '+name)
    # Drive the real --link wrapper past acceptance: TX0 must run on its run directory.
    elf = work/'kernel.elf'
    elf.write_bytes(b'fixture')
    out = work/'accept'
    def accept(elf, cpu, folder, timeout, **kwargs):
        run = folder/'run'; run.mkdir()
        logs = run/'logs'; logs.mkdir()
        (logs/'boot.jsonl').write_text('{}\n')
        (logs/'boot.log').write_bytes(b'')
        for name in ('serial-a.log', 'serial-b.log'):
            (run/name).write_bytes(b'')
        (run/'result.json').write_text('{}')
        (run/'frames.log').write_text(good)
    import link_run
    with patch.object(meter, 'command', return_value='0 T link_hello_ok\n0 T pm_link_diag_poll'), \
         patch.object(meter, 'LinkDiagnostics'), patch.object(meter, 'link_judge'), \
         patch.object(link_run, 'accept', side_effect=accept):
        elf.with_suffix('.map').write_text('fixture')
        run = meter.link_accept(elf, '7450', 60, out)
        assert json.loads((run/'tx0.json').read_text())['guest_pln1_tx'] == 0
    path.write_text(good)
    assert meter.link_tx0(path)['guest_frames'] == 2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        exercise(Path(folder), args.mutate)
    print('PASS ppc-tx0')
    return 0


def run(ctx):
    import subprocess
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
