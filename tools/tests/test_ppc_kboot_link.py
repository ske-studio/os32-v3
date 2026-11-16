#!/usr/bin/env python3
"""Recorded evidence for PPC_P5_REST_DESIGN §2 K・§5 (no guest/libc)."""
import argparse
import copy
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/ppc'), str(ROOT/'tools')]
import kboot_run as meter

CHECK = dict(name='ppc-kboot-link', tier=1, budget=20,
    covers=['tools/tests/test_ppc_kboot_link.py', 'tools/ppc/kboot_run.py',
            'tools/ppc/link_run.py', 'tools/ppc/link_diag_run.py', 'tools/ppc/run.py',
            'tools/ppc/kboot_mutations.py', 'tools/ppc_lan_host.py',
            'tools/host_agent.py', 'tools/host_operator_socket.py', 'tools/host_rshell.py',
            'tools/ppc_link_transport.py', 'tools/remote_transport.py', 'tools/*host*.py',
            'platform/powermac/**', 'arch/ppc/**', 'init/ppc_kernel.c', 'tools/lan_bridge.py',
            'build/ppc-kboot.mk', 'arch/ppc/kernel.ld'],
    basis=['PPC_P5_REST_DESIGN §2 K・§5'])


def fixture(elf):
    header = bytearray(256)
    header[:6] = b'\x7fELF\x01\x02'
    struct.pack_into('>II', header, 24, 0x100000, 52)
    struct.pack_into('>HH', header, 42, 32, 1)
    struct.pack_into('>IIIII', header, 52, 1, 256, 0x100000, 0, 256)
    elf.write_bytes(header + bytes(range(256)))
    lines = [b'OF-CAPTURE ram=134217728 admitted=67108864 tb_hz=100000000',
             b'OF-OFF calls=1', b'MMU-D root=2097152 hpt=1048576 bytes=1048576 ram_bat=0',
             b'MEMORY online=1 heap=1 shm=1', b'TRAP dsi=pass isi=pass guard=pass flush=pass',
             b'MPIC sources=64 priority=0 cpu=0 delivery=pass', b'CLOCK hz=100 tick=1 us=10000',
             b'READY stage=pre-user', b'ALIVE tick_delta=200 tb_us=2000000 of_calls_delta=0']
    prelude = b'Trying cd:,\\PPCKERN.ELF...\r\n'
    a = b''.join(b'KBOOT '+line+b'\r\n' for line in lines)
    memory = struct.pack('>8I', 0, 67108864, 0x100000, 0x10000,
                         1048576, 1048576, 0x200000, 0x1000)
    diag = bytearray(128)
    diag[72:104] = memory
    struct.pack_into('>I', diag, 108, 100000000)
    replies = dict(status=bytes(24), clock=struct.pack('>IIQQ', 201, 100000000, 300000000, 3000000),
                   memory=memory, **{'read-diag': bytes(diag), 'peek': bytes(range(256))})
    return dict(a=prelude+a, b=b'', logs=[dict(seq=i, drop_records=0, drop_bytes=0) for i in range(3)],
                payload=a.replace(b'\r\n', b'\n')+b'LINK HEAP used=1024\n', replies=replies,
                elf=elf, mapping=' .bss.pool 0x200000 0x1000 platform/powermac/gmac_dma.o\n'
                '0x100000 __kernel_image_start\n0x110000 __kernel_resident_end\n',
                result=dict(hello=1, alive=1, tick_samples=[dict(tick=100, tb=100000000),
                                                                       dict(tick=200, tb=200000000)]))


def reject(evidence, reason):
    try:
        meter.link_judge(**evidence)
    except meter.CriterionFailure as exc:
        assert str(exc) == reason, (str(exc), reason)
        return
    raise AssertionError('false acceptance: '+reason)


def exercise(mutate, work):
    good = fixture(work/'fixture.elf')
    meter.link_judge(**good)
    bad = []
    def case(name, change, reason):
        evidence = copy.deepcopy(good)
        change(evidence)
        # Preserve later ALIVE progress to rule out a generic incomplete boot.
        assert b'KBOOT ALIVE' in evidence['a']
        bad.append((name, evidence, reason))
    case('READY-missing', lambda e: e.update(a=e['a'].replace(b'KBOOT READY stage=pre-user\r\n', b'')),
         'milestone 8 order/format')
    case('milestone-order', lambda e: e.update(a=e['a'].replace(b'KBOOT OF-OFF calls=1\r\n', b'')+
                                             b'KBOOT OF-OFF calls=1\r\n'), 'milestone 2 order/format')
    case('tick-stopped', lambda e: e['result']['tick_samples'][-1].update(tick=100),
         'link_platform_now outside 90..110Hz')
    case('drop-report-erased', lambda e: e.update(logs=e['logs'][:1]+e['logs'][2:]), 'link LOG sequence')
    case('tail-missing', lambda e: e.update(payload=e['payload'][:-1]), 'link LOG tail')
    case('PEEK-byte', lambda e: e['replies'].update(peek=b'\xff'+e['replies']['peek'][1:]), 'link peek')
    case('clock-mismatch', lambda e: e['replies'].update(clock=struct.pack('>IIQQ', 201, 99, 300000000, 3000000)),
         'link clock')
    for name, evidence, reason in bad:
        reject(evidence, reason)
        if mutate: print('RED '+name+': '+reason)
    evidence = copy.deepcopy(good); evidence['replies']['status'] = bytes(4)+b'BADPHY!!'+bytes(12)
    reject(evidence, 'link status identity')
    for field, reason in [('drop_records', 'link LOG drops'), ('drop_bytes', 'link LOG drops')]:
        evidence = copy.deepcopy(good); evidence['logs'][1][field] = 1
        reject(evidence, reason)
    evidence = copy.deepcopy(good); evidence['b'] = b'noise'
    reject(evidence, 'link serial B output')
    evidence = copy.deepcopy(good); evidence['payload'] = evidence['payload'].replace(b'KBOOT READY stage=pre-user\n', b'')
    reject(evidence, 'link LOG milestones')
    for names, link in [({'link_hello_ok'}, False), ({'kboot_tick'}, False), ({'pm_lan_poll'}, True)]:
        try: meter.check_image(names, link)
        except meter.CriterionFailure: pass
        else: raise AssertionError('unknown/mismatched image accepted')
    meter.check_image({'link_hello_ok'}, True)
    meter.check_image({'pm_lan_poll'}, False)

    # Exercise the inherited Diagnostics submit/result path with recorded bytes.
    diag = meter.LinkDiagnostics(good['elf'])
    class Operator:
        ready = dict(mode='diag')
        pending = None
        def submit(self, mode, line, unused):
            assert mode == 'diag' and line == diag.cases[diag.index][0]
            self.pending = dict(id=diag.index+1, state='DONE')
            return diag.index+1
        def take_result(self):
            return dict(status=0, result=good['replies'][diag.cases[diag.index][0].split()[0]])
    class Agent:
        established = True
        operator = Operator()
        counts = dict(ack=1)
    for i in range(5):
        assert not diag(Agent(), work)
        assert diag(Agent(), work) == (i == 4)
    assert diag.raw == good['replies']
    assert json.loads((work/'diagnostic-bytes.json').read_text())['peek'] == bytes(range(256)).hex()

    with patch.object(sys, 'argv', ['kboot_run', '--elf', str(good['elf']), '--link']), \
         patch.object(meter.shutil, 'which', return_value=None):
        with patch.dict(os.environ, {'OS32_REQUIRE_PPC': '0'}): assert meter.main() == 0
        with patch.dict(os.environ, {'OS32_REQUIRE_PPC': '1'}): assert meter.main() == 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as work:
        exercise(args.mutate, Path(work))
    print('PASS ppc-kboot-link')
    return 0


def run(ctx):
    import subprocess
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
