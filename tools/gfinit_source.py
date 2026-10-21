"""S1 transcript reader, not an interpreter or a hardware permission table.

Input: envytools f102b823 init_scripts.txt supplied with DESIGN.final r3.
Only init-script DATA are reconstructed. Never inspect x86 code. CRCs use
IEEE CRC32 (zlib), over [start, start+length), including DONE and operands.
"""
import re
import struct


def le(value, width=4):
    return value.to_bytes(width, 'little')


def numbers(text):
    return [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)', text)]


def read_source(path):
    scripts, rows, script = [], [], None
    for line in path.read_text().splitlines():
        match = re.match(r'Init script (\d+) at (0x[0-9a-f]+)(?: \(.*\))?:', line)
        if match:
            script = int(match[1])
            scripts.append((script, int(match[2], 16)))
            if script == 4:
                inline = re.fullmatch(r'\s*([0-9a-f]{2}) DONE', line[match.end():])
                assert inline, line
                rows.append((script, int(match[2], 16), bytes.fromhex(inline[1])))
            continue
        if line.startswith('Subroutine at 0xd336'):
            script = 0xff
            scripts.append((script, 0xd336))
            continue
        match = re.match(r'(0x[0-9a-f]+): (.*)', line)
        if not match:
            continue
        pc, text = int(match[1], 16), match[2]
        if text.startswith('NV_REG'):
            # Three compressed lines, each exactly four 13-byte instructions.
            for part in text.split(' ; '):
                reg, value = numbers(part)
                keep = 0xffffffff
                if '&=' in part:
                    keep, value = keep ^ value, 0
                rows.append((script, pc, b'\x6e' + le(reg) + le(keep) + le(value)))
                pc += 13
            continue
        if text.startswith('IO_RESTRICT_PROG'):
            vals = numbers(text)
            if '×8' in text:
                vals = vals[:1] + vals[1:] * 8
            assert len(vals) == 9
            raw = bytes.fromhex('32 d4 03 3c 70 04 08') + b''.join(map(le, vals))
        else:
            prefix = re.match(r'((?:[0-9a-f]{2} )*[0-9a-f]{2})(?:\s|$)', text)
            assert prefix, line
            raw = bytes.fromhex(prefix[1])
            op = raw[0]
            desc = text[prefix.end():]
            if op == 0x58:
                pairs = re.findall(r'R\[0x[0-9a-f]+\]=(0x[0-9a-f]+)', desc)
                raw += b''.join(le(int(v, 16)) for v in pairs)
            elif op == 0x54:
                pairs = re.findall(r'C\[(0x[0-9a-f]+)\]=(0x[0-9a-f]+|0)(?= |$)', desc)
                raw += bytes(int(v, 16) for pair in pairs for v in pair)
            elif op == 0x51:
                # Expand explicit individual/range operands, checking coverage.
                data = {}
                pairs = re.findall(r'\[(0x[0-9a-f]+)(?:\.\.(0x[0-9a-f]+))?\]='
                                   r'(0x[0-9a-f]+|0)(?= |$)', desc)
                for first, last, value in pairs:
                    begin, end = int(first, 16), int(last or first, 16)
                    for index in range(begin, end + 1):
                        assert index not in data
                        data[index] = int(value, 16)
                assert sorted(data) == list(range(raw[3], raw[3] + raw[4]))
                raw += bytes(data[index] for index in sorted(data))
            elif op == 0x4a:
                khz = int(re.search(r'= (\d+)kHz', desc)[1])
                shown = re.search(r'\[ ((?:[0-9a-f]{2} ){4})=', desc)
                assert shown and bytes.fromhex(shown[1]) == le(khz)
                raw += le(khz) * raw[6]
        rows.append((script, pc, raw))
    assert len(rows) == 193 and len({r[2][0] for r in rows}) == 27
    for sid, start in scripts:
        end = start
        for _, pc, raw in (r for r in rows if r[0] == sid):
            assert pc == end, (sid, hex(pc), hex(end))
            end += len(raw)
        assert rows[[r[0] for r in rows].index(sid)][1] == start
        assert [r for r in rows if r[0] == sid][-1][2] == b'\x71'
    return scripts, rows


# Effect fields: pc, space, head, port, index, reg, width, keep, value,
# expression, flags. Values for non-CONSTANT expressions are parameters,
# not a fabricated hardware result. Every row has DENY_W until a shared
# descriptor can express the policy. This is an inventory, never authority.
MMIO, VGA = 0, 1
CONSTANT, RESTORE, COPY, PLL, SELECT, LATCH = range(6)
DENY_W, N1, DENY_DISPLAY = 1, 2, 4


def effects(rows):
    out, head = [], 0
    for sid, pc, raw in rows:
        # Every main ends on head0 and SUB 6/7/8 are called on head0, so a new script starts on head0.
        if not out or sid != previous_sid:
            head = 0
        previous_sid = sid
        op = raw[0]
        u32 = lambda pos: struct.unpack_from('<I', raw, pos)[0]

        def mm(reg, keep, value, expr=CONSTANT):
            flags = DENY_W
            if reg in (0x1850, 0x1854) and (expr != CONSTANT or value != 0):
                flags |= DENY_DISPLAY
            out.append((pc, MMIO, 0, 0, 0, reg, 4, keep, value, expr, flags))

        def pll(reg, khz):
            # §5 names these internal write destinations. Coefficients, exact
            # masks and chip-specific ordering belong to S4, not S1. Zero
            # keep is UNKNOWN here, not a grant to overwrite all bits.
            regs = ([reg - 4, reg, 0x4038, 0x403c, 0x4600, 0xc040]
                    if reg < 0x680000 else
                    [reg, 0x680578, 0x680580, 0x1584, 0xc040])
            for dst in regs:
                mm(dst, 0, khz, PLL)

        def vg(port, index, keep, value, expr=CONSTANT):
            nonlocal head
            target_head = 0 if port == 0x3d4 and index == 0x44 else head
            base = (0x601000 if port == 0x3d4 else 0xc0000) + target_head * 0x2000
            # Index writes, including those needed by reads, are side effects.
            out.append((pc, VGA, target_head, port, index, base + port,
                        1, 0, index, CONSTANT, DENY_W))
            flags = DENY_W
            if port == 0x3c4 and index == 1 and not (keep | value) & 0x20:
                flags |= N1 if sid == 3 and pc == 0xe81e else DENY_DISPLAY
            out.append((pc, VGA, target_head, port, index, base + port + 1,
                        1, keep, value, expr, flags))
            if port == 0x3d4 and index == 0x44:
                head = 1 if value == 3 else 0

        def selector(port, index):
            base = (0x601000 if port == 0x3d4 else 0xc0000) + head * 0x2000
            out.append((pc, VGA, head, port, index, base + port, 1, 0,
                        index, CONSTANT, DENY_W))

        if op == 0x6e:
            mm(u32(1), u32(5), u32(9))
        elif op == 0x7a:
            mm(u32(1), 0, u32(5) | (1 if u32(1) == 0x200 else 0))
        elif op in (0x32, 0x4a):
            selector(int.from_bytes(raw[1:3], 'little'), raw[3])
            for i in range(raw[6]):
                if op == 0x32:
                    mm(u32(7), 0, u32(11 + 4*i), SELECT)
                else:
                    pll(u32(7), u32(11 + 4*i))
        elif op == 0x4b:
            pll(u32(1), u32(5))
        elif op == 0x58:
            for i in range(raw[5]):
                mm(u32(1) + 4*i, 0, u32(6 + 4*i))
        elif op == 0x65:
            mm(0x184c, 0xfffff0ff, 0)
            mm(u32(1), 0, u32(5))
            mm(u32(1), 0, u32(9))
            mm(0x184c, 0, 0, RESTORE)
            mm(0x1850, 0xfffffffe, 0)
        elif op == 0x6f:
            mm([0x1002d4, 0x10021c, 0x1002d0, 0x1002d8][raw[1]], 0, 1)
        elif op == 0x5f:
            mm(u32(14), u32(18), u32(1), COPY)
        elif op in (0x53, 0x52):
            vg(0x3d4, raw[1], raw[2] if op == 0x52 else 0, raw[-1])
        elif op == 0x54:
            for i in range(raw[1]):
                vg(0x3d4, raw[2+2*i], 0, raw[3+2*i])
        elif op == 0x51:
            for i in range(raw[4]):
                vg(0x3d4, raw[1], 0, raw[3] + i)
                vg(0x3d4, raw[2], 0, raw[5+i])
            vg(0x3d4, raw[1], 0, 0, RESTORE)
        elif op in (0x78, 0x62):
            vg(int.from_bytes(raw[1:3], 'little'), raw[3],
               raw[4] if op == 0x78 else 0, raw[-1])
        elif op == 0x37:
            vg(int.from_bytes(raw[7:9], 'little'), raw[9], raw[10], u32(1), COPY)
        elif op == 0x39:
            selector(0x3d4, 0x3c)
    return out
