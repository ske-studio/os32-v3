"""T3a1 ELF destinations and C/ASM loader interval fixtures.

Called by test_vk32_crc.run_all, including its script/loader mutations.
VK32 v2 has no BSS descriptors: production BSS collisions are rejected by the
ELF-aware generator. To exercise the loaders' same interval check, the host-only
fixture represents a BSS reservation as an extra zero-filled entry. This does
not change the product format or claim the loaders can infer missing BSS data.
Link ASSERTs below are preparation fixtures; production linker changes are T3ab.
"""
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import zlib

import lz4.block

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_memmap as gen


def command(cmd, cwd=None, ok=True):
    r = subprocess.run([str(x) for x in cmd], cwd=cwd, capture_output=True,
                       env=dict(os.environ, PYTHONPATH=str(ROOT / 'tools')))
    if ok and r.returncode:
        raise AssertionError(r.stderr.decode(errors='replace'))
    return r


def fixture(work, chain=False, core_bss=32, sqlite_data=4, sqlite_bss=32,
            link_edit=None):
    work.mkdir()
    objects = []
    for rel, data in (
        ('kernel/kentry', f'global kentry\nsection .text\nkentry: nop\n'
         f'section .data align=4\ndd 0x12345678\nsection .bss align=4\nresb {core_bss}\n'),
        ('lib/sqlite3/sqlite3', f'section .text\nnop\nsection .rodata align=32\ndd 42\n'
         f'section .data align=4\ntimes {sqlite_data} db 7\n'
         f'section .bss align=4\nresb {sqlite_bss}\n'),
        ('lib/sqlite3/os32_sqlite_vfs', 'section .text\n'),
        ('lib/sqlite3/os32_sqlite_test', 'section .text\n')):
        p = work / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.with_suffix('.asm').write_text(data)
        command(['nasm', '-f', 'elf32', p.with_suffix('.asm'), '-o', p.with_suffix('.o')])
        objects.append(p.with_suffix('.o').relative_to(work))
    script = (ROOT / 'build/os32.ld').read_text()
    if chain:
        script = script.replace('. = 0x200000;', '. = ALIGN(__bss_end, 4096);')
    if link_edit:
        before, after = link_edit
        assert before in script
        script = script.replace(before, after)
        # Isolate a rodata LMA shift; keep the flat binary's end unchanged.
        script = script.replace('.sqlite_data : {',
                                '.sqlite_data : AT(ADDR(.sqlite_data)) {')
    (work / 'layout.ld').write_text(script)
    elf = work / 'kernel.elf'
    command(['ld', '-m', 'elf_i386', '-T', 'layout.ld', '-o', elf, *objects], cwd=work)
    for name, flags in (('kernel', ['--remove-section=.sqlite_' + s for s in ('text', 'rodata', 'data', 'bss')]),
                        ('sqlite', ['--only-section=.sqlite_' + s for s in ('text', 'rodata', 'data')])):
        command(['objcopy', '-O', 'binary', *flags, elf, work / (name + '.bin')])
    return elf


def generate(script, work, elf, *extra):
    return command([sys.executable, '-B', script, '--elf', elf,
                    '--kernel', work / 'kernel.bin', '--sqlite', work / 'sqlite.bin',
                    '--limit-header', ROOT / 'boot/boot_defs.h', '-o', work / 'image.lz4',
                    *extra], ok=False)


def link_asserts(work, elf):
    """Execute ASSERTs against real section extents, not a parsed script string."""
    _, syms = gen.read_elf(elf)
    m = gen.Macros(gen.read_defines(ROOT / 'include/memmap.h'), syms)
    assertions = []
    for lo, hi, section in (('__bss_start', '__bss_end', '.bss'),
                            ('__sqlite_data_end', '__sqlite_end', '.sqlite_bss')):
        assertions += [f'ASSERT((({lo} | {hi}) & 3) == 0, "BSS alignment")',
                       f'ASSERT({lo} <= ADDR({section}) && '
                       f'ADDR({section}) + SIZEOF({section}) <= {hi}, "BSS coverage")',
                       f'ASSERT({lo} <= {hi}, "BSS order")']
        for guard in ('MEM_SHM_GUARD_LO', 'MEM_SHM_GUARD_HI', 'MEM_STACK_GUARD', 'MEM_SHELL_GUARD'):
            a, n = m.get(guard), m.get('MEM_GUARD_SIZE')
            assertions.append(f'ASSERT({hi} <= {a} || {lo} >= {a+n}, "BSS guard")')
    (work / 'assert.ld').write_text('\n'.join(assertions))
    return command(['ld', '-m', 'elf_i386', '-T', 'layout.ld', '-T', 'assert.ld',
                    '-o', work / 'assert.elf', 'kernel/kentry.o',
                    'lib/sqlite3/sqlite3.o', 'lib/sqlite3/os32_sqlite_vfs.o',
                    'lib/sqlite3/os32_sqlite_test.o'], cwd=work, ok=False)


def edit_elf(elf, output, symbol=None, value=None, bss_end=None):
    """Change one linked input fact; the file remains a valid, readable ELF."""
    data = bytearray(elf.read_bytes())
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    sections = [struct.unpack_from('<10I', data, h[6] + i * 40) for i in range(h[12])]
    for i, s in enumerate(sections):
        if bss_end is not None and s[1] == 8 and s[3] < 0x200000:
            struct.pack_into('<I', data, h[6] + i * 40 + 20, bss_end - s[3])
            bss_end = None
        if s[1] == 2 and symbol:
            st = sections[s[6]]
            strings = data[st[4]:st[4]+st[5]]
            for off in range(s[4], s[4]+s[5], 16):
                name = struct.unpack_from('<I', data, off)[0]
                if strings[name:strings.index(0, name)].decode() == symbol:
                    struct.pack_into('<I', data, off + 4, value)
    output.write_bytes(data)
    return output


def check_real_bss(elf):
    manifest = gen.layout_manifest(str(ROOT), str(elf.with_suffix('.map')), str(elf))
    syms = manifest['symbols']
    zeros = [(syms[a], syms[b]) for a, b in (('__bss_start', '__bss_end'),
                                            ('__sqlite_data_end', '__sqlite_end'))]
    assert all(a <= b and not ((a | b) & 3) for a, b in zeros)
    for row in manifest['ranges']:
        if row.get('source') == 'elf' and row.get('noload'):
            assert any(a <= row['start'] <= row['end'] <= b for a, b in zeros)
        if row['purpose'] == 'guard':
            assert all(b <= row['start'] or row['end'] <= a for a, b in zeros)
    return True


def loader_reservation_cases(vk, exes, core, sqlite, bss_start, bss_end, sqlite_start):
    """Project the ELF reservation into VK32's existing interval vocabulary."""
    def image(sql_addr):
        parts = [(vk.LOAD_MIN, core), (bss_start, bytes(bss_end - bss_start)),
                 (sql_addr, sqlite)]
        hsz = 16 + 20 * len(parts) + 8
        hdr = struct.pack('<4I', 0x32334B56, hsz, 2, len(parts))
        payload, crcs = b'', b''
        for addr, raw in parts:
            data = lz4.block.compress(raw, store_size=False)
            hdr += struct.pack('<4I', addr, len(raw), hsz + len(payload), len(data))
            payload += data
            crcs += struct.pack('<I', zlib.crc32(raw) & 0xFFFFFFFF)
        return vk.reseal(hdr + crcs + struct.pack('<II', hsz + len(payload), 0) + payload)
    for exe in exes.values():
        for mode in (0, 1):
            # The adjacent boundary passes, including a reservation for the BSS.
            rc, _, win = vk.run(exe, mode, image(sqlite_start))
            assert rc == 0 and win[bss_start-vk.LOAD_MIN:bss_end-vk.LOAD_MIN] == bytes(bss_end-bss_start)
            for addr in (bss_start, bss_end - 1):
                rc, _, win = vk.run(exe, mode, image(addr))
                assert rc == vk.E['DST'] and win == bytes([0xCC]) * vk.WINDOW, 'BSS reservation overlap'
            # addr is in-band; raw length alone overflows u32. CRC is resealed.
            bad = vk.reseal(vk.put(image(sqlite_start), 16 + 32 + 4, '<I', 0xFFFFFFFF))
            rc, _, win = vk.run(exe, mode, bad)
            assert rc == vk.E['DST'] and win == bytes([0xCC]) * vk.WINDOW, 'destination length overflow'


def run_cases(vk, exes, script, work, real=False):
    work = pathlib.Path(work)
    lines = []
    for name, edit, diagnostic in (
            ('lma-shift', ('.sqlite_rodata : {',
                           '.sqlite_rodata : AT(ADDR(.sqlite_rodata) - 4) {'), b'VMA/LMA'),
            ('core-shift', ('. = 0x100000;', '. = 0x101000;'), b'core start'),
            ('entry-shift', ('ENTRY(kentry)',
                             'ENTRY(shifted_entry)\nshifted_entry = kentry + 4;'), b'ELF entry')):
        d = work / name
        elf = fixture(d, link_edit=edit)
        r = generate(script, d, elf)
        assert r.returncode != 0 and diagnostic in r.stderr, name + ' accepted: ' + r.stderr.decode()
    lines.append('[layout] VMA/LMA, core start and ELF entry mismatch rejection PASS')
    # Both product recipes must consume their own ELF, including R1.
    recipes = (ROOT / 'build/kernel.mk').read_text()
    for out in ('$(BUILD_OUT)', '$(R1_OUT)'):
        rule = recipes.split(out + '/vmkernel.lz4:', 1)[1].split('\n\n', 1)[0]
        assert '--elf ' + out + '/kernel.elf' in rule and '--sqlite-addr' not in rule
    # Same inputs and compressor through legacy and ELF paths: byte-for-byte CRC
    # invariance, independently of this worktree's current build timestamp.
    if real:
        real = ROOT/'build/out'
        d = work/'real-layout'
        d.mkdir()
        for name in ('kernel.bin', 'sqlite.bin'):
            shutil.copy(real/name, d/name)
        r = generate(script, d, real/'kernel.elf')
        assert r.returncode == 0, r.stderr.decode()
        img = (d/'image.lz4').read_bytes()
        legacy = vk.make_image(script, work, (d/'kernel.bin').read_bytes(),
                               (d/'sqlite.bin').read_bytes(), 'legacy-layout')
        assert img == legacy == (real/'vmkernel.lz4').read_bytes(), 'current image/CRC changed'
        assert len(img) <= vk.MAX_IMAGE, 'compressed budget'
        assert check_real_bss(real/'kernel.elf'), 'real ELF BSS/guard constraints'
        lines.append('[layout real] destinations/bytes/CRC unchanged, {} bytes image_crc={:08x}'.format(
            len(img), vk.parse(img)['icrc']))
    for chain in (False, True):
        d = work / ('chain' if chain else 'fixed')
        elf = fixture(d, chain)
        r = generate(script, d, elf)
        assert r.returncode == 0, r.stderr.decode()
        img = (d / 'image.lz4').read_bytes()
        _, syms = gen.read_elf(elf)
        assert vk.parse(img)['ents'][1][0] == syms['__sqlite_start'], 'SQLite destination != ELF'
        assert link_asserts(d, elf).returncode == 0, 'link ASSERT control'
        for exe in exes.values():
            lines += vk.check_good(exe, img, (d/'kernel.bin').read_bytes(),
                                   (d/'sqlite.bin').read_bytes(), d.name)
        # NOLOAD must never contribute file bytes. Include both ends and padding.
        assert vk.parse(img)['ents'][1][1] == syms['__sqlite_data_end'] - syms['__sqlite_start']
        loader_reservation_cases(vk, exes, (d/'kernel.bin').read_bytes(),
                                 (d/'sqlite.bin').read_bytes(), syms['__bss_start'],
                                 syms['__bss_end'], syms['__sqlite_start'])
        r = generate(script, d, elf, '--sqlite-addr', hex(syms['__sqlite_start'] + 4))
        assert r.returncode != 0 and b'conflicting' in r.stderr, 'explicit address conflict accepted'
        # A destination inside the other image's BSS must fail at generation time.
        bad = edit_elf(elf, d/'overlap.elf', bss_end=syms['__sqlite_start'] + 4)
        bad = edit_elf(bad, bad, '__bss_end', syms['__sqlite_start'] + 4)
        r = generate(script, d, bad)
        assert r.returncode != 0 and b'overlap' in r.stderr, 'BSS collision accepted'
    for name, opts in (('unaligned-start', dict(sqlite_data=3)),
                       ('unaligned-end', dict(core_bss=33))):
        d = work / name
        elf = fixture(d, **opts)
        r = generate(script, d, elf)
        assert r.returncode != 0 and b'BSS zero range' in r.stderr, name
        r = link_asserts(d, elf)
        assert r.returncode != 0 and b'BSS alignment' in r.stderr, name + ' link ASSERT'
    d = work / 'fixed'
    elf = d/'kernel.elf'
    _, syms = gen.read_elf(elf)
    for name in ('__bss_start', '__bss_end', '__sqlite_data_end', '__sqlite_end'):
        bad = edit_elf(elf, d/'unaligned.elf', name, syms[name] + 1)
        r = generate(script, d, bad)
        assert r.returncode != 0 and b'BSS zero range' in r.stderr, name + ' alignment'
    bad = edit_elf(elf, d/'omitted.elf', '__bss_end', syms['__bss_end'] - 4)
    r = generate(script, d, bad)
    assert r.returncode != 0 and b'outside zero range' in r.stderr, 'BSS omission accepted'
    # Shift zeroing (not file bytes) across the existing stack guard.
    m = gen.Macros(gen.read_defines(ROOT / 'include/memmap.h'), syms)
    guard_end = m.get('MEM_STACK_GUARD') + m.get('MEM_GUARD_SIZE')
    bad = edit_elf(elf, d/'guard.elf', '__sqlite_end', guard_end)
    r = generate(script, d, bad)
    assert r.returncode != 0 and b'guard' in r.stderr, 'BSS guard intersection accepted'
    # Relink with the same bad symbol definitions: require the specific ASSERT,
    # rather than counting an assembler/compiler failure as detection.
    ld_script = (d/'layout.ld').read_text()
    for before, after, diagnostic in (
            ('__bss_end = .;', '__bss_end = . - 4;', b'BSS coverage'),
            ('__sqlite_end = .;', f'__sqlite_end = {guard_end};', b'BSS guard')):
        (d/'layout.ld').write_text(ld_script.replace(before, after))
        r = link_asserts(d, elf)
        assert r.returncode != 0 and diagnostic in r.stderr, diagnostic.decode()
    (d/'layout.ld').write_text(ld_script)
    bad = edit_elf(elf, d/'overflow.elf', bss_end=0x100000004)
    r = generate(script, d, bad)
    assert r.returncode != 0 and b'overflow' in r.stderr, 'ELF section overflow accepted'
    # A file padded to include NOLOAD is rejected, rather than compressed as data.
    with (d/'sqlite.bin').open('ab') as f:
        f.write(bytes(32))
    r = generate(script, d, elf)
    assert r.returncode != 0 and b'file size mismatch' in r.stderr, 'NOLOAD bytes accepted'
    lines.append('[layout] fixed/chain ELF -> identical generator; BSS alignment/coverage/guard and NOLOAD rejection PASS')
    return lines
