#!/usr/bin/env python3
"""LB3 pure descriptors: byte oracles, target build, executable mutations.
Record: tools/tests/e82557_desc_tdd.md
"""
CHECK = dict(name='e82557-desc-host', tier=1,
    covers=['drivers/net/e82557/e82557_desc.c', 'drivers/net/e82557/e82557_desc.h', 'drivers/net/e82557/e82557_regs.h', 'drivers/net/e82557/e82557.c', 'drivers/net/e82557/e82557_probe_reset_base.inc', 'drivers/net/e82557/e82557_probe_loopback.inc', 'tools/tests/e82557_desc_host.c', 'tools/tests/test_e82557_drv.py', 'tools/tests/host32.py', 'drivers/net/e82557/e82557_eeprom.c', 'drivers/pci/pci_bind_match.c', 'drivers/pci/pci_decode.c', 'drivers/net/e82557/lan_module.c', 'tools/tests/e82557_drv_host.c', 'arch/x86/kernel/isr_handlers.c', 'platform/pc98/lan_match.c', 'tools/tests/kernel_includes.py'],
    needs=[], budget=2.35836)

import pathlib
import subprocess
import sys
import tempfile
import test_e82557_drv as drv

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = list(range(33)) + [42, 43] + list(range(50, 63))
MUTATIONS = [
    ('E82557_CB_EL : 0', 'E82557_CB_S : 0', 'EL position'),
    ('E82557_CB_S : 0', 'E82557_CB_EL : 0', 'S position'),
    ('E82557_CB_I : 0', '0 : 0', 'I omitted'),
    ('e82557_wr32(p + E82557_DESC_TBD, E82557_TBD_NONE);', 'e82557_wr32(p + E82557_DESC_TBD, 0);', 'TBD zero'),
    ('(u16)len | E82557_TCB_EOF', '(u16)((len >> 8) | (len << 8)) | E82557_TCB_EOF', 'count endian'),
    ('copy_bytes((u8 *)cb + E82557_CB_HEADER, cfg', 'copy_bytes((u8 *)cb + 16, cfg', 'Configure offset 16'),
    ('0x01, 0x00, 0x2E', '0x00, 0x00, 0x2E', 'MII off'),
    ('out[E82557_CFG_LB_BYTE] = E82557_CFG_LB_VALUE', 'out[E82557_CFG_LB_BYTE] = 0x3E', 'loopback wrong bit'),
    ('0x00, 0x40, 0xF2', '0x00, 0x40, 0x72', 'padding off'),
    ('cb, E82557_CB_IA,', 'cb, E82557_CB_CONFIG,', 'IA opcode'),
    ('cb, E82557_CB_CONFIG,', 'cb, E82557_CB_IA,', 'Configure opcode'),
    ('stop->reset_seq != before', '1', 'old reset accepted'),
    ('stop->reset_seq != stop->start_seq', '1', 'Idle after CU Start accepted'),
    ('E82557_CUS_MASK | E82557_RUS_MASK', 'E82557_CUS_MASK', 'RU active accepted'),
]

MUTATIONS += [
    ('((u32)op << E82557_MDI_OP_SHIFT)', '((u32)(op == 2 ? 1 : op) << E82557_MDI_OP_SHIFT)', 'MDI read opcode'),
    ('(phy & E82557_PHY_MASK) << E82557_MDI_PHY_SHIFT', '(phy & E82557_PHY_MASK) << 16', 'MDI phy field'),
    ('(phy & E82557_PHY_MASK)', '(phy)', 'MDI phy mask'),
    ('(reg & E82557_PHY_MASK)', '(reg)', 'MDI reg mask'),
    ('ctl & E82557_MDI_READY', 'ctl & (1u << 27)', 'MDI Ready bit'),
    ('return (u16)ctl;', 'return (u16)(ctl & 0xff);', 'MDI data truncated'),
    ('(an_done || !an_en)', '(an_done || (0 && !an_en))', 'forced mode ignored'),
]

def build(tmp, mutation=None):
    source = ROOT / 'tools/tests/e82557_desc_host.c'
    if mutation:
        tree = pathlib.Path(tmp) / 'tree'
        for rel in ('tools/tests/e82557_desc_host.c', 'drivers/net/e82557/e82557_desc.c', 'drivers/net/e82557/e82557_desc.h', 'drivers/net/e82557/e82557_regs.h'):
            target = tree / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            text = (ROOT / rel).read_text()
            if rel == 'drivers/net/e82557/e82557_desc.c':
                old, new, label = mutation
                # Mask expression also occurs in decode; mutate stop predicate only.
                if label == 'RU active accepted':
                    text = text.replace(old, new)
                else:
                    if text.count(old) != 1:
                        raise RuntimeError(f'anchor: {label}')
                    text = text.replace(old, new)
            target.write_text(text)
        source = tree / source.relative_to(ROOT)
    exe = pathlib.Path(tmp) / 'desc-host'
    subprocess.run(['gcc', *drv.FLAGS, '-Wno-unused-parameter', *drv.INCLUDES, str(source), '-o', str(exe)], check=True)
    return exe

def main():
    with tempfile.TemporaryDirectory(prefix='os32-desc-') as tmp:
        exe = build(tmp)
        if '--target' in sys.argv:
            drv.target_build(tmp)
        failed = 0
        for case in CASES:
            rc = drv.run_case(exe, str(case)).returncode
            print(f'CASE {case} rc={rc}', flush=True)
            failed += bool(rc)
        print(f'SUMMARY {len(CASES)-failed}/{len(CASES)} PASS', flush=True)
        if '--mutate' in sys.argv:
            for old, new, label in MUTATIONS:
                exe = build(tmp, (old, new, label))
                hits = [str(c) for c in CASES if drv.run_case(exe, str(c)).returncode]
                print(f'MUTATION {label}: {"RED " + ",".join(hits) if hits else "SURVIVED"}', flush=True)
                failed += not bool(hits)
        return bool(failed)

if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_e82557_desc.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
