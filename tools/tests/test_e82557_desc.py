#!/usr/bin/env python3
"""LB3 pure descriptors: byte oracles, target build, executable mutations.
Record: tools/tests/e82557_desc_tdd.md
"""
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
    ('e82557_wr32(p + 8, E82557_TBD_NONE);', 'e82557_wr32(p + 8, 0);', 'TBD zero'),
    ('(u16)len | E82557_TCB_EOF', '(u16)((len >> 8) | (len << 8)) | E82557_TCB_EOF', 'count endian'),
    ('copy_bytes((u8 *)cb + E82557_CB_HEADER, cfg', 'copy_bytes((u8 *)cb + 16, cfg', 'Configure offset 16'),
    ('0x01, 0x00, 0x2E', '0x00, 0x00, 0x2E', 'MII off'),
    ('out[10] = 0x6E', 'out[10] = 0x3E', 'loopback wrong bit'),
    ('0x00, 0x40, 0xF2', '0x00, 0x40, 0x72', 'padding off'),
    ('cb, E82557_CB_IA,', 'cb, E82557_CB_CONFIG,', 'IA opcode'),
    ('cb, E82557_CB_CONFIG,', 'cb, E82557_CB_IA,', 'Configure opcode'),
    ('stop->reset_seq != before', '1', 'old reset accepted'),
    ('stop->reset_seq != stop->start_seq', '1', 'Idle after CU Start accepted'),
    ('E82557_CUS_MASK | E82557_RUS_MASK', 'E82557_CUS_MASK', 'RU active accepted'),
]

MUTATIONS += [
    ('((u32)op << 26)', '((u32)(op == 2 ? 1 : op) << 26)', 'MDI read opcode'),
    ('(phy & E82557_PHY_MASK) << 21', '(phy & E82557_PHY_MASK) << 16', 'MDI phy field'),
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
        for rel in ('tools/tests/e82557_desc_host.c', 'drivers/e82557_desc.c', 'drivers/e82557_desc.h', 'drivers/e82557_regs.h'):
            target = tree / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            text = (ROOT / rel).read_text()
            if rel == 'drivers/e82557_desc.c':
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
