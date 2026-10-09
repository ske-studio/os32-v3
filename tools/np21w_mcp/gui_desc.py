"""GSHELL_DBG_DESC v1 reader; no HTTP calls, writes, or cached ELF addresses.

read_state(read, elf_path=...) calls read(address, length), which must return
bytes from /api/mem space=phys. The resident CPL0 shell uses identity mapping
(exec/exec.c shell load path; kernel/paging.c shell band; app_sys.ld).
The caller owns pausing/snapshot consistency; a running WM is not atomic.
"""
from pathlib import Path
import struct
import subprocess

DEFAULT_ELF = Path(__file__).resolve().parents[2] / "userland/gshell.elf"
MAGIC = int.from_bytes(b"GSDD", "little")
VERSION = 1
MEM_READ_MAX = 65536  # NP21/W aidebug_api.cpp handle_mem の上限。
FIELDS = (
    "magic version words gui_size gui_inner windows win_size win_count "
    "used gen owner x y w h title title_len visible minimized zorder z_count "
    "usize_size slots slot_size slot_count slot_used slot_owner "
    "sent_size sent_inner sent_count app_id_min"
).split()


class DescriptorError(ValueError):
    """Unsupported descriptor, missing/ambiguous symbols, or invalid image."""


def elf_symbols(elf_path):
    """Resolve the exact exported name and unique Rust v0 suffixes each time."""
    try:
        result = subprocess.run(["nm", "-S", "--defined-only", str(elf_path)],
                                capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise DescriptorError(f"cannot read ELF symbols: {elf_path}") from exc
    rows = []
    for line in result.stdout.splitlines():
        p = line.split()
        if len(p) == 4:
            rows.append((p[3], int(p[0], 16), int(p[1], 16)))
    symbols = {}
    for key, suffix in (("desc", "GSHELL_DBG_DESC"), ("gui", "gshell2wm3GUI"),
                        ("sent", "gshell4trim4SENT")):
        hits = [(addr, size) for name, addr, size in rows
                if (name == suffix if key == "desc" else
                    name.startswith("_R") and name.endswith(suffix))]
        if len(hits) != 1:
            raise DescriptorError(f"{key}: expected one ELF symbol ({suffix}), found {len(hits)}")
        symbols[key] = hits[0]
    return symbols


def _read(read, address, size):
    parts = []
    for offset in range(0, size, MEM_READ_MAX):
        length = min(size - offset, MEM_READ_MAX)
        blob = read(address + offset, length)
        if not isinstance(blob, bytes) or len(blob) != length:
            raise DescriptorError(f"short/non-bytes read at {address + offset:#x}: expected {length} bytes")
        parts.append(blob)
    return b"".join(parts)


def parse_descriptor(blob, symbols):
    if len(blob) < 12:
        raise DescriptorError("descriptor header too short")
    magic, version, words = struct.unpack_from("<III", blob)
    if magic != MAGIC:
        raise DescriptorError(f"descriptor magic mismatch: {magic:#x}")
    if version != VERSION:
        raise DescriptorError(f"unsupported descriptor version: {version}")
    if words != len(FIELDS) or len(blob) != len(FIELDS) * 4:
        raise DescriptorError(f"descriptor field count/size mismatch: {words}, {len(blob)} bytes")
    d = dict(zip(FIELDS, struct.unpack("<" + "I" * words, blob)))
    for key in ("gui", "sent"):
        if d[key + "_size"] != symbols[key][1]:
            raise DescriptorError(f"{key}: ELF symbol size differs from descriptor")
    if d["usize_size"] not in (4, 8):
        raise DescriptorError("unsupported usize width")
    for key in ("win_count", "slot_count", "sent_count", "title_len"):
        if not 0 < d[key] <= 256:
            raise DescriptorError(f"invalid {key}")
    if not 0 < d["gui_size"] <= 1024 * 1024 or not 0 < d["sent_size"] <= 1024:
        raise DescriptorError("invalid image size")
    def fits(offset, width, size, label):
        if offset + width > size:
            raise DescriptorError(f"{label}: field outside object")
    for field, width in (("used", 1), ("gen", 2), ("owner", 4), ("x", 4),
                         ("y", 4), ("w", 4), ("h", 4), ("title", d["title_len"]),
                         ("visible", 1), ("minimized", 1)):
        fits(d[field], width, d["win_size"], "Win." + field)
    for field, width in (("slot_used", 1), ("slot_owner", 4)):
        fits(d[field], width, d["slot_size"], field)
    for field, width in (("windows", d["win_size"] * d["win_count"]),
                         ("zorder", d["usize_size"] * d["win_count"]),
                         ("z_count", d["usize_size"]),
                         ("slots", d["slot_size"] * d["slot_count"])):
        fits(d["gui_inner"] + d[field], width, d["gui_size"], field)
    fits(d["sent_inner"], d["sent_count"], d["sent_size"], "SENT")
    return d


def read_state(read, elf_path=DEFAULT_ELF):
    """Return front owner (0 if none), windows, used slots, and SENT booleans.

    windows use complete generation/index IDs; z is back-to-front position
    or None for a used window outside zorder. Titles decode as UTF-8.
    trim_sent[n] corresponds to owner app_id_min+n (v1: owners 2..5).
    Raises DescriptorError with a reason instead of returning guessed data.
    """
    symbols = elf_symbols(elf_path)
    return read_snapshot(read, symbols)


def read_snapshot(read, symbols):
    """Same decoder with explicitly supplied (address, size) for host fixtures."""
    addr, size = symbols["desc"]
    # Read and validate the header before allowing an ELF-sized allocation.
    header = _read(read, addr, 12)
    magic, version, words = struct.unpack("<III", header)
    if magic != MAGIC or version != VERSION or words != len(FIELDS):
        parse_descriptor(header, symbols)  # raises a specific rejection reason
    if size != len(FIELDS) * 4:
        raise DescriptorError("descriptor ELF symbol size/field count mismatch")
    d = parse_descriptor(_read(read, addr, size), symbols)
    gui = _read(read, *symbols["gui"])
    sent = _read(read, *symbols["sent"])
    base = d["gui_inner"]
    def number(buf, offset, width, signed=False):
        return int.from_bytes(buf[offset:offset + width], "little", signed=signed)
    def boolean(buf, offset):
        value = buf[offset]
        if value not in (0, 1):
            raise DescriptorError(f"invalid bool byte: {value}")
        return bool(value)
    n = number(gui, base + d["z_count"], d["usize_size"])
    if n > d["win_count"]:
        raise DescriptorError("z_count exceeds window capacity (inconsistent snapshot)")
    zorder = [number(gui, base + d["zorder"] + z * d["usize_size"], d["usize_size"])
              for z in range(n)]
    if len(set(zorder)) != n or any(i >= d["win_count"] for i in zorder):
        raise DescriptorError("invalid zorder (inconsistent snapshot)")
    windows = []
    by_index = {}
    for i in range(d["win_count"]):
        start = base + d["windows"] + i * d["win_size"]
        if not boolean(gui, start + d["used"]):
            continue
        gen = number(gui, start + d["gen"], 2)
        title = gui[start + d["title"]:start + d["title"] + d["title_len"]]
        w = dict(id=(gen << 16) | i, owner=number(gui, start + d["owner"], 4, True),
                 rect=[number(gui, start + d[f], 4, True) for f in ("x", "y", "w", "h")],
                 title=title.split(b"\0", 1)[0].decode("utf-8", errors="replace"),
                 visible=boolean(gui, start + d["visible"]),
                 minimized=boolean(gui, start + d["minimized"]),
                 z=zorder.index(i) if i in zorder else None)
        windows.append(w)
        by_index[i] = w
    front = next((by_index[i]["owner"] for i in reversed(zorder)
                  if i in by_index and by_index[i]["visible"]), 0)
    slots = []
    for i in range(d["slot_count"]):
        start = base + d["slots"] + i * d["slot_size"]
        if boolean(gui, start + d["slot_used"]):
            slots.append(dict(n=i, owner=number(gui, start + d["slot_owner"], 4, True)))
    return dict(front=front, windows=windows, slots=slots,
                trim_sent=[boolean(sent, d["sent_inner"] + i) for i in range(d["sent_count"])])


def wm_state(machine):
    """gtool1 GuiTools hook: injected machine.mem and machine.gshell_elf.

    Used slots additionally carry the slot/used keys consumed by launch();
    n remains the descriptor reader's SHM slot number.
    """
    state = read_state(machine.mem, machine.gshell_elf)
    state["slots"] = [dict(item, slot=item["n"], used=True) for item in state["slots"]]
    return state
