"""kernel.map symbol resolution for the NP21/W AI debug MCP server.

Parses the GNU ld map file (build/out/kernel.map) and offers name<->address
lookups so tools can accept symbol names and annotate addresses as
`symbol+offset`.
"""

import os
import re

_LINE_RE = re.compile(r"^\s+0x([0-9a-fA-F]{6,})\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")


class SymbolTable:
    def __init__(self, map_path):
        self.map_path = map_path
        self._mtime = None
        self._by_name = {}          # name -> addr
        self._sorted = []           # list of (addr, name), ascending

    def _reload_if_needed(self):
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
