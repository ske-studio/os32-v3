"""Fixed C allow rows, GF6200_4C_DESIGN §3; no execution-plan construction.

Compiled C evaluates all constants/offsetof, like the existing frozen-oracle
extractor. gen_recover_tables.py emits separate tables so default ordinals,
CRC and expansion stay intact. References use nouveau c03c1a1 MIT dispnv04/.
"""
import pathlib
import struct
import subprocess
import tempfile

from test_recover_tables import FLAGS
import host32

ROOT = pathlib.Path(__file__).resolve().parents[1]
FULL = '0xffffffffU'
NONE = 'REC_GF_C_NO_INDEX'
# Registers nouveau only writes (no readback) and DESIGN §4 does not list as
# required CHECKs: record the write, do not compare the readback. Ra266
# 2026-10-29: 680A24 wrote 000FFFFF and read 0 on NV44A.
NOUVEAU_WRITE_ONLY = ('0x680404', '0x680630', '0x680634', '0x680700', '0x680848',
                      '0x6808c0', '0x680a20', '0x680a24', '0x680a34', '0x600860',
                      '0x8704', '0x8140', '0x1588', '0x600810', '0x600830',
                      '0x600834', '0x680900')


def initializers():
    rows = []

    def add(kind, reg, index=NONE, expr='GF_C_LITERAL', source='0', keep='0',
            bits='0', verify=FULL, count='1', flags='0'):
        width = 4 if kind == 'MMIO' else 1
        if kind == 'MMIO' and reg in NOUVEAU_WRITE_ONLY:
            verify = '0'
        if width == 1:
            verify = f'({verify}) & GF_MODE_BYTE_MASK'
        rows.append([f'GF_C_{kind}', str(reg), str(index), str(width), expr,
                     str(source), str(keep), str(bits), str(verify), str(count), str(flags)])

    def literal(kind, reg, bits, **kw):
        add(kind, reg, bits=str(bits), **kw)

    def cr(index, bits=None, **kw):
        if bits is None:
            state('CR', '0x6013d4', f'cr[{index}]', index=index, **kw)
        else:
            literal('CR', '0x6013d4', bits, index=index, **kw)

    def sr(index, bits=None, **kw):
        if bits is None:
            state('SR', '0xc03c4', f'sr[{index}]', index=index, **kw)
        else:
            literal('SR', '0xc03c4', bits, index=index, **kw)

    def state(kind, reg, field, **kw):
        add(kind, reg, expr='GF_C_STATE',
            source=f'__builtin_offsetof(struct gfinit_mode, {field})', **kw)

    def live(kind, reg, keep, bits, **kw):
        add(kind, reg, expr='GF_C_LIVE', keep=keep, bits=bits, **kw)

    def mm(reg, field, **kw):
        state('MMIO', reg, field, **kw)

    # Q: dac.c:364-373,426-472; dfp.c:93-110. FP write conditional ON.
    live('MMIO', '0x68052c', '~1U', '0', verify='0x101')
    mm('0x680848', 'fp_control', flags='REC_GF_C_FP_ON')
    # P: crtc.c:175-234,708-728. Delays/checks belong to stage 2.
    cr('0x1f', '0x57', verify='0')
    cr('0x44', '0')
    sr(0, 1)
    live('SR', '0xc03c4', '0xffU & ~0x20U', '0x20', index=1, verify='0xff')
    live('CR', '0x6013d4', '0x7f', 0, index='0x17', verify='0xff')
    sr(0, 3)
    live('CR', '0x6013d4', '0xff', '0xc0', index='0x1a', verify='0xff')
    # NVBlankScreen: hw.c:110-124, includes owner and sequencer reset.
    cr('0x44', 0); sr(0, 1)
    live('SR', '0xc03c4', '0xff', '0x20', index=1, verify='0xff')
    sr(0, 3)
    literal('MMIO', '0x600804', 1, verify=3)
    live('MMIO', '0x680900', '~0x10000U', 0)
    # S: crtc.c:659-667,63-88,600; cursor.c:38-59.
    live('CR', '0x6013d4', '0xff', '0xfa', index='0x21', verify='0xff')
    mm('0x680524', 'sel_clk')
    cr('0x45'); mm('0x680634', 'ramdac634')
    for i in ('0x30', '0x31', '0x2f'):
        cr(i)
    live('MMIO', '0x680300', FULL, 0, verify=0)
    # D: dac.c:375-409, OR0=head A; other analog on opposite head.
    mm('0x68052c', 'dacclk', verify='0x101')
    live('MMIO', '0x68252c', '~0x100U', '0x100', verify='0x100')
    mm('0x680608', 'test_control', verify='0x100000')
    # L1 protect: hw.h:238-255; hw.c:468-536; devinit/nv04.c:197-272.
    sr(0, 1)
    live('SR', '0xc03c4', '0xff', '0x20', index=1, verify='0xff')
    literal('AR_GATE', '0x6013c0', 0, verify=0)
    mm('0x680404', 'cursync')
    for i, reg in enumerate(('0xc040', '0x680580', '0x680578', '0x680508', '0xc040')):
        add('MMIO', reg, expr='GF_C_PLL', source=i,
            verify=('0xffffffffU', '0x100', '0', '0', '0xffffffffU')[i])
    for reg, field in [('0x68050c', 'pllsel'), ('0x680524', 'sel_clk'),
                       ('0x680600', 'general'), ('0x680630', 'ramdac630'),
                       ('0x680634', 'ramdac634'), ('0x680700', 'tv_setup'),
                       ('0x680848', 'fp_control'), ('0x6808c0', 'ramdac8c0'),
                       ('0x680a20', 'ramdaca20'), ('0x680a24', 'ramdaca24'),
                       ('0x680a34', 'ramdaca34')]:
        # 50C bit28 (VCLK_RATIO_DB2) reads back 0 on NV44A (Ra266 2026-10-29,
        # wrote 10000204, read 00000204); nouveau writes it and never reads it.
        mm(reg, field, verify='~0x10000000U' if field == 'pllsel' else FULL)
    # Analog-only omissions: no TV/FP timing or CTV, DESIGN §3 stopping values.
    # L2: hw.c:665-772; no NV11/20 retrace waits on NV44A.
    mm('0x600860', 'engine_ctrl')
    literal('MMIO', '0x8704', 1)
    literal('MMIO', '0x8140', 0)
    literal('MMIO', '0x1588', 0)
    for reg, field in [('0x600810', 'cursor_cfg'), ('0x600830', 'crtc830'),
                       ('0x600834', 'crtc834'), ('0x60081c', 'gpio_ext'),
                       ('0x600850', 'crtc850')]:
        mm(reg, field, verify=0 if field in ('gpio_ext', 'crtc850') else FULL)
    live('MMIO', '0x680900', FULL, '0x10000')
    mm('0x600804', 'config', verify=3)
    for i in ('0x19','0x1a','0x25','0x28','0x33','0x2d','0x1c','0x1b',
              '0x20','0x47','0x9f','0x49','0x30','0x31','0x2f'):
        cr(i, verify='0' if i in ('0x1c','0x33','0x49') else '0xff')
    live('MMIO', '0x680300', FULL, 0, verify=0)
    cr('0x39')
    for i in ('0x3b','0x3c','0x41','0x45','0x4b','0x52','0x42','0x53','0x54'):
        cr(i, verify=0 if i in ('0x3b','0x3c','0x4b','0x52') else '0xff')
    for i in range(16):
        cr('0x57', i, verify=0); cr('0x58', 0, verify=0)
    for i in ('0x59','0x5b','0x85','0x86'):
        cr(i, verify=0 if i in ('0x59','0x5b') else '0xff')
    mm('0x600800', 'start')
    # L3: hw.c:794-807,564-586. Typed write-only palette / FF-reset AR.
    literal('DAC_MASK', '0x6813c6', '0xff', verify=0)
    literal('DAC_INDEX', '0x6813c8', 0, verify=0)
    add('DAC_DATA', '0x6813c9', expr='GF_C_PALETTE', verify=0,
        count='REC_GF_C_PALETTE_BYTES')
    state('MISC', '0xc03c2', 'misc', verify='0xff')
    for i in range(5):
        sr(i)
    live('CR', '0x6013d4', '0x7f', 0, index='0x11', verify='0xff')
    for i in range(25):
        cr(i)
    live('CR', '0x6013d4', '0xff', '0x80', index='0x11', verify='0xff')
    for i in range(9):
        state('GR', '0xc03ce', f'gr[{i}]', index=i, verify='0xff')
    for i in range(21):
        state('AR', '0x6013c0', f'ar[{i}]', index=i, verify=0)
    literal('AR_GATE', '0x6013c0', '0x20', verify=0)
    live('SR', '0xc03c4', '0xdf', 0, index=1, verify='0xff')
    sr(0, 3)
    literal('AR_GATE', '0x6013c0', '0x20', verify=0)
    # C: crtc.c:863-903,175-234; dac.c:412-419.
    cr('0x28'); mm('0x680600', 'general')
    for i in ('0x19','0x13','0x42'):
        cr(i)
    mm('0x600800', 'start')
    for i in ('0x1b','0x20','0x47'):
        cr(i)
    sr(0, 1); sr(1, 1)
    cr('0x17', '0xc3'); sr(0, 3); cr('0x1a', 4)
    live('MMIO', '0x68052c', FULL, 1, verify='0x101')
    # R: OS32-only PC-98 route callback, DESIGN §3 (not a generic MMIO target).
    literal('ROUTE', '0xfac', 1, verify=3)
    return rows


def generate(default):
    """Compile-evaluate numeric fields; reuse matching default target keys."""
    rows = initializers()
    source = '#include "kdebug/recover_gf_c.h"\n'
    source += 'static const u32 rows[][11] = {\n'
    source += ''.join('{' + ','.join(row) + '},\n' for row in rows) + '};\n'
    source += '''void _start(void) { u32 rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(4), "b"(1),
                     "c"(rows), "d"(sizeof(rows)) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); __builtin_unreachable(); }
'''
    with tempfile.TemporaryDirectory(prefix='gf-mode-tables-') as td:
        unit, exe = pathlib.Path(td)/'rows.c', pathlib.Path(td)/'rows'
        unit.write_text(source)
        subprocess.run(['gcc', *FLAGS, '-I'+str(ROOT), '-nostdlib', '-static',
                        '-no-pie', str(unit), '-o', str(exe)], check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
        if result.returncode or len(result.stdout) != len(rows)*11*4:
            raise RuntimeError('C allow-row extraction failed')
    numeric = list(struct.iter_unpack('<11I', result.stdout))
    targets = {}
    for table, row, field, slot, value in default['records']:
        if table == 'targets' and slot == 0:
            targets.setdefault(row, {})[field] = value
    identities = {}
    for key, t in targets.items():
        if t['space'] == 1 and t['bar'] == 0:  # BAR0 MMIO
            identities[(0, t['offset'], 0xffffffff, t['width'])] = key
        if t['space'] == 5 and t['head'] == 0:  # indexed CRTC/SR/GR
            kind = {0x3d4: 1, 0x3c4: 2, 0x3ce: 3}.get(t['port'])
            if kind:
                reg = {1: 0x6013d4, 2: 0xc03c4, 3: 0xc03ce}[kind]
                identities[(kind, reg, t['index'], t['width'])] = key
    new_targets, numbered = [], []
    for ordinal, row in enumerate(numeric):
        kind, reg, index, width, expr, offset, keep, bits, verify, count, flags = row
        identity = kind, reg, index, width
        if identity not in identities:
            key = len(targets) + len(new_targets)
            identities[identity] = key
            # All mode data COLD. Index saves remain the existing contract.
            # AR/DAC/gates/route never masquerade as read_safe MMIO.
            new_targets.append((key, kind, reg, index, width, 1, int(kind <= 3 or kind == 5)))
        numbered.append((578 + ordinal, identities[identity], 9, *row))
    # 510 is read-only observation, never appears as a C write row.
    identity = (0, 0x680510, 0xffffffff, 4)
    key = identities.get(identity, len(targets)+len(new_targets))
    new_targets.append((key, *identity, 1, 1))
    out = ['/* Generated by tools/gen_recover_tables.py; do not edit. */',
           'const struct recover_gf_c_row recover_gf_c_rows[] = {']
    out += ['    {' + ', '.join(f'0x{v:x}U' for v in row) + '},' for row in numbered]
    out += ['};', 'const struct recover_gf_c_target recover_gf_c_targets[] = {']
    out += ['    {' + ', '.join(f'0x{v:x}U' for v in row) + '},' for row in new_targets]
    out += ['};', f'const u32 recover_gf_c_row_count = {len(numbered)}U;',
            f'const u32 recover_gf_c_target_count = {len(new_targets)}U;']
    return '\n'.join(out)+'\n'
