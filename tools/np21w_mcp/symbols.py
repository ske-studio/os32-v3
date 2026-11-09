"""kernel.map symbol resolution for the NP21/W AI debug MCP server.

Parses the GNU ld map file (build/out/kernel.map) and offers name<->address
lookups so tools can accept symbol names and annotate addresses as
`symbol+offset`.
"""

import os
import re
import struct
import subprocess
import zlib
from pathlib import Path

_LINE_RE = re.compile(r"^\s+0x([0-9a-fA-F]{6,})\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")


class SymbolTable:
    def __init__(self, map_path):
        self.map_path = map_path
        self._mtime = None
        self._frozen = False
        self._by_name = {}          # name -> addr
        self._sorted = []           # list of (addr, name), ascending

    def _reload_if_needed(self):
        if self._frozen:
            return
        try:
            mtime = os.path.getmtime(self.map_path)
        except OSError:
            self._by_name = {}
            self._sorted = []
            self._mtime = None
            return
        if mtime == self._mtime:
            return
        by_name = {}
        with open(self.map_path, "r", errors="replace") as f:
            for line in f:
                m = _LINE_RE.match(line)
                if not m:
                    continue
                addr = int(m.group(1), 16)
                name = m.group(2)
                # keep the first definition; map files repeat names in
                # cross-reference sections
                if name not in by_name and addr != 0:
                    by_name[name] = addr
        self._by_name = by_name
        self._sorted = sorted((a, n) for n, a in by_name.items())
        self._mtime = mtime

    def resolve(self, token):
        """Resolve 'name', 'name+0x10', or a hex/dec address to an int.

        Returns None if a symbol name is unknown.
        """
        self._reload_if_needed()
        token = token.strip()
        try:
            return int(token, 0)
        except ValueError:
            pass
        off = 0
        base = token
        for sep in ("+", "-"):
            if sep in token:
                base, offs = token.split(sep, 1)
                base = base.strip()
                offs = offs.strip()
                try:
                    off = int(offs, 0) * (1 if sep == "+" else -1)
                except ValueError:
                    return None
                break
        addr = self._by_name.get(base)
        if addr is None:
            return None
        return addr + off

    def annotate(self, addr):
        """Return 'symbol+0xNN' for an address, or '' if none is below it."""
        self._reload_if_needed()
        if not self._sorted:
            return ""
        lo, hi = 0, len(self._sorted)
        while lo < hi:
            mid = (lo + hi) // 2
            if self._sorted[mid][0] <= addr:
                lo = mid + 1
            else:
                hi = mid
        if lo == 0:
            return ""
        base_addr, name = self._sorted[lo - 1]
        delta = addr - base_addr
        if delta > 0x10000:
            return ""
        if delta == 0:
            return name
        return "%s+0x%x" % (name, delta)


def elf_image(path):
    """Reconstruct objcopy's resident binary from ELF32 allocated sections."""
    data = Path(path).read_bytes()
    if data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected little-endian ELF32')
    shoff = struct.unpack_from('<I', data, 32)[0]
    entsize, count = struct.unpack_from('<HH', data, 46)
    if entsize != 40 or not count or shoff + count * entsize > len(data):
        raise ValueError('invalid ELF section table')
    sections = []
    for index in range(count):
        _, kind, flags, address, offset, size, _, _, _, _ = struct.unpack_from(
            '<10I', data, shoff + index * entsize)
        if flags & 2 and kind != 8 and size:
            if offset + size > len(data):
                raise ValueError('truncated ELF section')
            sections.append((address, data[offset:offset + size]))
    if not sections:
        raise ValueError('ELF has no resident sections')
    base = min(a for a, _ in sections)
    end = max(a + len(b) for a, b in sections)
    if end - base > 4 * 1024 * 1024:
        raise ValueError('ELF resident image too large')
    binary = bytearray(end - base)
    for address, blob in sections:
        binary[address - base:address - base + len(blob)] = blob
    return base, bytes(binary)


class ElfValues(dict):
    def __getitem__(self, name):
        value = super().__getitem__(name)
        if value is None:
            raise ValueError('ambiguous ELF symbol: ' + name)
        return value

    def get(self, name, default=None):
        return self[name] if name in self else default


class ElfSymbols:
    """Resolve every observation from fresh nm -S output, including local names."""
    def __init__(self, path, runner=subprocess.run):
        self.path, self.runner = str(path), runner

    def read(self):
        nm = os.environ.get('OS32_NM') or os.path.join(
            os.environ.get('CROSS_DIR', '/home/hight/opt/cross'), 'bin/i386-elf-nm')
        result = self.runner([nm, '-S', '--defined-only', self.path],
                             capture_output=True, text=True, check=True)
        table = ElfValues()
        for line in result.stdout.splitlines():
            fields = line.split()
            if len(fields) not in (3, 4):
                continue
            addr = int(fields[0], 16)
            size = int(fields[1], 16) if len(fields) == 4 else 0
            name = fields[-1]
            value = (addr, size)
            # Assemblers repeat absolute constants; unrelated local symbols
            # may collide. Reject a differing definition only when requested.
            if name in table and dict.__getitem__(table, name) != value:
                table[name] = None
            else:
                table[name] = value
        return table



R1_SUBDIR = 'r1'  # build/kernel-objects.mk: R1_OUT := $(BUILD_OUT)/r1


class VerifiedSymbols:
    """Select a map/ELF pair by the running kernel's commit and image CRC.

    No successful selection survives a tool call: reset() also handles resets,
    state loads and rebuilding a candidate while the MCP process stays alive.
    Explicit maps restrict discovery, but never bypass identity validation.
    """
    def __init__(self, read, explicit=None, root=None, home=None):
        self.read = read
        self.explicit = explicit
        self.root = Path(root or Path(__file__).resolve().parents[2])
        self.home = Path(home or Path.home())
        self.reset()

    def reset(self):
        self.selected = None
        self.info = None
        self.elf_symbols = None

    def candidates(self):
        explicit = self.explicit or os.environ.get('OS32_KERNEL_MAP')
        if explicit:
            return [Path(explicit).expanduser().resolve()]
        outs = [self.root / 'build' / 'out', self.home / 'os32-v3' / 'build' / 'out']
        outs.extend(sorted((self.home / 'wt/os32-v3').glob('*/build/out')))
        # The R1 image (make kernel-r1) has its own map/ELF/VK32 in out/r1/;
        # identity validation below picks whichever image the guest booted.
        paths = [out / sub / 'kernel.map' for out in outs for sub in ('.', R1_SUBDIR)]
        return list(dict.fromkeys(p.resolve() for p in paths))

    def select(self):
        if self.selected is not None:
            return self.selected
        if self.info is not None:
            raise ValueError('no matching kernel.map: ' + '; '.join(self.info['candidates']))
        errors = []
        for path in self.candidates():
            try:
                table = SymbolTable(str(path))
                table._reload_if_needed()
                elf = ElfSymbols(path.with_name('kernel.elf')).read()
                if not table._by_name or any(
                        elf.get(name, (None, 0))[0] != addr
                        for name, addr in table._by_name.items()):
                    raise ValueError('map/ELF symbol mismatch')
                image = path.with_name('vmkernel.lz4').read_bytes()
                magic, header, version, count = struct.unpack_from('<4I', image)
                if (magic != 0x32334b56 or version != 2 or not 1 <= count <= 4
                        or header != 24 + 20 * count or len(image) < header):
                    raise ValueError('invalid VK32 header')
                size, crc = struct.unpack_from('<II', image, header - 8)
                # Bind the ELF/map to this image, including dirty builds that
                # share a commit string. VK32 stores the uncompressed CRC.
                binary_addr, binary = elf_image(path.with_name('kernel.elf'))
                load, raw_size = struct.unpack_from('<II', image, 16)
                raw_crc = struct.unpack_from('<I', image, 16 + 16 * count)[0]
                if (count != 1 or load != binary_addr or raw_size != len(binary)
                        or raw_crc != zlib.crc32(binary)):
                    raise ValueError('ELF/VK32 payload mismatch')
                sealed = image[:header - 4] + b'\0' * 4 + image[header:]
                if size != len(image) or zlib.crc32(sealed) != crc:
                    raise ValueError('invalid VK32 CRC')
                # bootinfo.h's i386 struct bootinfo: status/magic/version/source/
                # ndrives, two 12-byte drives, img_valid+padding, crc, size.
                boot_addr, boot_size = elf['s_bootinfo']
                if boot_size != 48:
                    raise ValueError('unsupported struct bootinfo size')
                boot = self.read(boot_addr, boot_size)
                if (len(boot) != boot_size or struct.unpack_from('<i', boot)[0] != 0
                        or boot[36] != 1
                        or struct.unpack_from('<II', boot, 40) != (crc, size)):
                    raise ValueError('guest Image CRC/size mismatch or unavailable')
                commit_addr, commit_size = elf['os32_build_commit']
                source = path.with_name('build_id.c').read_text()
                match = re.search(r'os32_build_commit\[[^]]+\]\s*=\s*"([A-Za-z0-9._-]+)"', source)
                if match is None or match[1] == 'unknown' or commit_size != 24:
                    raise ValueError('build commit unavailable')
                commit = match[1].encode().ljust(commit_size, b'\0')
                if (binary[commit_addr - binary_addr:commit_addr - binary_addr + commit_size] != commit
                        or self.read(commit_addr, commit_size) != commit):
                    raise ValueError('guest build commit mismatch')
                table._frozen = True
                self.selected = table
                self.elf_symbols = elf
                self.info = dict(path=str(path), commit=match[1], image_crc='%08x' % crc)
                return table
            except (OSError, ValueError, KeyError, TypeError, struct.error,
                    subprocess.SubprocessError) as exc:
                errors.append('%s: %s' % (path, exc))
        self.info = dict(path=None, error='no matching kernel.map', candidates=errors)
        raise ValueError('no matching kernel.map: ' + '; '.join(errors))

    def resolve(self, token):
        try:
            return int(token.strip(), 0)
        except ValueError:
            return self.select().resolve(token)

    def annotate(self, addr):
        try:
            return self.select().annotate(addr)
        except ValueError:
            return ''
