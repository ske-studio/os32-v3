#!/usr/bin/env python3
"""NP21/W AI debug MCP server (stdio, dependency-free).

Exposes the emulator's embedded HTTP debug API as MCP tools, adding
kernel.map symbol resolution: addresses may be given as symbol names
(e.g. "exec_run", "timer_handler+0x10") and returned EIPs are annotated
as symbol+offset.

Speaks the MCP stdio transport (newline-delimited JSON-RPC 2.0) using only
the Python standard library, so no `pip install` is required.
"""

import binascii
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import np21w_client as emu           # noqa: E402
from symbols import SymbolTable      # noqa: E402

MAP_PATH = os.environ.get(
    "OS32_KERNEL_MAP",
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "..", "..", "build", "out", "kernel.map"))
SYMS = SymbolTable(os.path.normpath(MAP_PATH))

PROTOCOL_VERSION = "2024-11-05"
SERVER_INFO = {"name": "np21w-aidebug", "version": "1.1.0"}


# --------------------------------------------------------------------------
# tool helpers
# --------------------------------------------------------------------------

def _json(body):
    return json.loads(body.decode("utf-8", "replace"))


def _annotate_eip(obj, keys=("eip", "prev_eip")):
    for k in keys:
        v = obj.get(k)
        if isinstance(v, str) and v.startswith("0x"):
            sym = SYMS.annotate(int(v, 16))
            if sym:
                obj[k + "_sym"] = sym
    return obj


def _resolve_or_error(token):
    addr = SYMS.resolve(token)
    if addr is None:
        raise emu.EmuError("unknown symbol: %s" % token)
    return addr


# --------------------------------------------------------------------------
# host objdump (the emulator's built-in disassembler only decodes mnemonics,
# never ModRM/operands, so it advances one byte at a time -- unusable)
# --------------------------------------------------------------------------

_OBJDUMP_CACHE = []


def _objdump():
    """Path to a usable objdump, or None."""
    if not _OBJDUMP_CACHE:
        for cand in (os.environ.get("OS32_OBJDUMP"),
                     "i686-elf-objdump", "objdump"):
            found = shutil.which(cand) if cand else None
            if found:
                _OBJDUMP_CACHE.append(found)
                break
        else:
            _OBJDUMP_CACHE.append(None)
    return _OBJDUMP_CACHE[0]


_OBJDUMP_LINE_RE = re.compile(r"^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(.*)$")
_OPERAND_ADDR_RE = re.compile(r"0x([0-9a-f]{4,8})\b")


def _annotate_operands(text):
    """Append <symbol> to branch/absolute targets that resolve in kernel.map."""
    def sub(m):
        sym = SYMS.annotate(int(m.group(1), 16))
        return m.group(0) + (" <%s>" % sym if sym else "")
    return _OPERAND_ADDR_RE.sub(sub, text)


# --------------------------------------------------------------------------
# tools
# --------------------------------------------------------------------------

def tool_status(_):
    return _annotate_eip(_json(emu.get("/api/status")))


def tool_regs(_):
    return _annotate_eip(_json(emu.get("/api/regs")))


def tool_prof_start(_):
    emu.post("/api/profile", "on=1")
    return "profiling started (counters cleared)"


def tool_prof_stop(_):
    emu.post("/api/profile", "on=0")
    return "profiling stopped"


def tool_prof(args):
    """Instruction/I-O profile with the V86 trap cost worked out."""
    top = int(args.get("top", 24))
    d = _json(emu.get("/api/profile?top=%d" % top))
    ms = d.get("elapsed_ms", 0) or 1
    out = ["window: %d ms  (profiling %s)"
           % (d.get("elapsed_ms", 0),
              "on" if d.get("enabled") else "off")]
    ins = d.get("instructions", 0)
    if ins:
        out.append("guest: %d instructions  (%.3f MIPS)"
                   % (ins, ins / 1000.0 / ms))
    out.append("")
    out.append("-- instructions that trap in V86 when IOPL<3 --")
    for k in ("int", "iret", "cli", "sti", "pushf", "popf"):
        n = d.get(k, 0)
        out.append("  %-6s %10d  (%8.0f/s)" % (k, n, n * 1000.0 / ms))
    t = d.get("traps_iopl0", 0)
    out.append("  %-6s %10d  (%8.0f/s)  <- total #GP rate at IOPL=0"
               % ("TOTAL", t, t * 1000.0 / ms))
    out.append("")
    out.append("-- I/O (trappable per-port via the TSS I/O bitmap) --")
    for k in ("in", "out"):
        n = d.get(k, 0)
        out.append("  %-6s %10d  (%8.0f/s)" % (k, n, n * 1000.0 / ms))
    out.append("")
    out.append("-- busiest ports --")
    out.append("  %-8s %10s %10s %10s" % ("port", "reads", "writes", "acc/s"))
    for p in d.get("ports", []):
        tot = p["reads"] + p["writes"]
        out.append("  %-8s %10d %10d %10.0f"
                   % (p["port"], p["reads"], p["writes"], tot * 1000.0 / ms))
    return "\n".join(out)


def tool_fault(_):
    """Latest unrecoverable core fault (triple fault etc.).

    generation is 0 until the first fault and increments on every one, so
    comparing it against a previously seen value tells a new fault from an
    old record. When paused is true the core is frozen at the fault site and
    emu_regs / emu_read_mem still describe it.
    """
    return _annotate_eip(_json(emu.get("/api/fault")))


def tool_gdc(_):
    """Graphics chip state the guest cannot read back.

    Ports 68h / 6Ah / A4h / A6h are write-only on real hardware and the
    uPD7220 parameter RAM (SYNC / PITCH / SCROLL) is not readable at all,
    so a monitor handing a guest its own leftover mode has no way to see
    the mismatch from inside. Here it is a diff between two machines.
    """
    d = _json(emu.get("/api/gdc"))
    d.pop("ok", None)
    return "\n".join("%-14s %s" % (k, v) for k, v in d.items())


def tool_pic(_):
    """8259A master/slave state: IMR/ISR/IRR/ICW.

    The mask register is write-only from the guest, so "which interrupts
    is this machine set up to take?" has no answer from inside. A monitor
    handing a guest the wrong default mask injects interrupts a real
    machine would block -- which looks like nothing at all going wrong.
    """
    d = _json(emu.get("/api/pic"))
    d.pop("ok", None)
    return "\n".join("%-10s %s" % (k, v) for k, v in d.items())


def tool_sound(_):
    """Whether sound is actually coming out, not just being written.

    peak is the absolute peak of the mixed output since the previous call
    (reading resets it), so it answers what a port count cannot: did
    anything reach the speakers. fm_playing is NOT reliable -- it only
    counts key-ons in this build. Use fm_ch_playing / keyreg for what the
    guest asked for, peak for what came out.
    """
    d = _json(emu.get("/api/sound"))
    d.pop("ok", None)
    return "\n".join("%-16s %s" % (k, v) for k, v in d.items())


def tool_tvram(_):
    d = _json(emu.get("/api/tvram"))
    return "\n".join(d.get("lines", []))


def tool_read_mem(args):
    addr = _resolve_or_error(args["addr"])
    length = int(args.get("len", 64))
    space = args.get("space", "phys")
    q = "/api/mem?addr=0x%x&len=%d&space=%s" % (addr, length, space)
    if space == "virt" and "seg" in args:
        q += "&seg=" + args["seg"]
    return _json(emu.get(q))


def tool_write_mem(args):
    addr = _resolve_or_error(args["addr"])
    space = args.get("space", "phys")
    q = "/api/mem?addr=0x%x&space=%s" % (addr, space)
    return _json(emu.post(q, args["hex"]))


def tool_put_file(args):
    """Place a host file at an arbitrary path inside the running guest.

    Replaces the old flow of "sync to HostDrv, then run hsync in the guest".
    The bytes go straight into guest memory over /api/deploy; the guest
    kernel agent writes the file when the shell is next responsive.
    """
    del args
    return tool_error(
        "retired 2026-09-09: the hotdeploy staging window was removed. "
        "Use `make deploy` (host -> C:\\os32) then `hsync` in the guest "
        "(/host -> /, sys excluded unless named). "
        "See docs/tasks/hotdeploy/DESIGN.md")


def tool_deploy_status(args):
    """Retired with the hotdeploy window (2026-09-09)."""
    del args
    return tool_error(
        "retired 2026-09-09: no guest-side file-placement agent any more. "
        "Delivery is `make deploy` + guest `hsync`.")


def tool_disasm(args):
    n = max(1, min(int(args.get("n", 8)), 256))

    if "addr" in args:
        addr = _resolve_or_error(args["addr"])
    else:
        addr = int(_json(emu.get("/api/status"))["eip"], 16)

    od = _objdump()
    if od is None:
        # no binutils on the host: fall back to the emulator, warts and all
        q = "/api/disasm?n=%d&addr=0x%x" % (n, addr)
        d = _json(emu.get(q))
        d["warning"] = ("host objdump not found; used the emulator's built-in "
                        "disassembler, which does not decode operands and "
                        "reports wrong instruction lengths")
        return d

    bits = args.get("bits")
    if bits is None:
        st = _json(emu.get("/api/status"))
        bits = 32 if st.get("protected_mode") and not st.get("vm86") else 16
    bits = int(bits)

    # x86 instructions are at most 15 bytes; over-read so the n-th one is whole
    space = args.get("space", "linear")
    nbytes = min(n * 15 + 15, 4096)
    mem = _json(emu.get("/api/mem?addr=0x%x&len=%d&space=%s"
                        % (addr, nbytes, space)))
    raw = binascii.unhexlify(mem["hex"])

    fd, path = tempfile.mkstemp(prefix="np21w_dis_", suffix=".bin")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(raw)
        p = subprocess.run(
            [od, "-D", "-b", "binary",
             "-m", "i386" if bits == 32 else "i8086",
             "--adjust-vma=0x%x" % addr, path],
            capture_output=True, timeout=20)
    finally:
        os.unlink(path)
    if p.returncode != 0:
        raise emu.EmuError("objdump failed: %s"
                           % p.stderr.decode("utf-8", "replace")[:200])

    lines = []
    emitted = 0
    cur_sym = None
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        m = _OBJDUMP_LINE_RE.match(line)
        if not m:
            continue
        at = int(m.group(1), 16)
        sym = SYMS.annotate(at)
        name, _, off = sym.partition("+")
        if name and name != cur_sym:
            lines.append("%08x <%s>:" % (at - (int(off, 16) if off else 0),
                                         name))
            cur_sym = name
        lines.append("  %6x:\t%-21s\t%s"
                     % (at, m.group(2), _annotate_operands(m.group(3))))
        emitted += 1
        if emitted >= n:
            break

    return "\n".join(lines) if lines else "(no instructions decoded)"


def tool_cmd(args):
    return emu.post("/api/cmd", args["command"], timeout=30).decode(
        "utf-8", "replace")


def tool_key(args):
    body = []
    if "seq" in args:
        body.append("seq=" + args["seq"])
    if "text" in args:
        body.append("text=" + args["text"])
    return _json(emu.post("/api/key", "&".join(body)))


def tool_pause(_):
    return _json(emu.post("/api/pause"))


def tool_resume(_):
    return _json(emu.post("/api/resume"))


def tool_reset(_):
    return _json(emu.post("/api/reset"))


def tool_break_add(args):
    addr = _resolve_or_error(args["addr"])
    q = "/api/break/add?addr=0x%x" % addr
    if "cs" in args:
        q += "&cs=" + args["cs"]
    return _json(emu.post(q))


def tool_break_del(args):
    if args.get("all"):
        return _json(emu.post("/api/break/del?addr=*"))
    addr = _resolve_or_error(args["addr"])
    return _json(emu.post("/api/break/del?addr=0x%x" % addr))


def tool_break_list(_):
    d = _json(emu.get("/api/break"))
    for bp in d.get("breakpoints", []):
        sym = SYMS.annotate(int(bp["eip"], 16))
        if sym:
            bp["eip_sym"] = sym
    return d


def tool_step(args):
    d = _json(emu.post("/api/step?n=%d" % int(args.get("n", 1))))
    return _annotate_eip(d)


def tool_trace(args):
    """Compact, bounded view of the ring buffer.

    The raw endpoint returns up to 4096 entries; rendering all of them as
    JSON is ~300 KB, which no caller can read. Default to the tail, collapse
    runs of the same address, and emit one line per entry.
    """
    limit = max(1, min(int(args.get("limit", 100)), 4096))
    collapse = args.get("collapse", True)
    entries = _json(emu.get("/api/trace")).get("trace", [])
    total = len(entries)

    if collapse:
        rows = []
        for e in entries:
            key = (e.get("cs"), e.get("eip"))
            if rows and rows[-1][0] == key:
                rows[-1][1] += 1
            else:
                rows.append([key, 1])
    else:
        rows = [[(e.get("cs"), e.get("eip")), 1] for e in entries]

    shown = rows[:limit] if args.get("head") else rows[-limit:]

    lines = []
    for (cs, eip), count in shown:
        sym = SYMS.annotate(int(eip, 16))
        row = "%s:%s" % (cs, eip)
        if sym:
            row += " " + sym
        if count > 1:
            row += "  x%d" % count
        lines.append(row)

    head = "trace: %d entries" % total
    if collapse:
        head += ", %d after collapsing runs" % len(rows)
    head += "; showing %s %d\n" % ("first" if args.get("head") else "last",
                                   len(shown))
    return head + "\n".join(lines)


def tool_trace_start(_):
    return _json(emu.post("/api/trace/start"))


def tool_trace_stop(_):
    return _json(emu.post("/api/trace/stop"))


# statsave.h: SUCCESS=0, FAILURE=-1; everything else is a non-fatal warning
_STATFLAGS = ((0x0001, "disk image changed since the save"),
              (0x0002, "emulator version differs"),
              (0x0080, "loaded with warnings"),
              (0x0100, "state file version mismatch"))


def _state_result(d):
    """Re-derive success from the raw statsave flags.

    The emulator reports ok=0 for any non-zero result, but only -1 is a real
    failure -- 0x80/0x01/0x02 are warnings and the state did load.
    """
    r = d.get("result", 0)
    if r < 0:
        return {"ok": False, "result": r, "error": "statsave reported failure"}
    out = {"ok": True, "result": r}
    if r:
        out["warnings"] = [msg for bit, msg in _STATFLAGS if r & bit] or \
            ["unknown statsave flags 0x%x" % r]
    return out


def tool_state_save(args):
    # writes a host file at a caller-chosen path: the emulator wants the
    # per-process token (np21w_client.token_headers)
    return _state_result(
        _json(emu.post("/api/state/save", "file=" + args["file"],
                       headers=emu.token_headers())))


def tool_state_load(args):
    return _state_result(
        _json(emu.post("/api/state/load", "file=" + args["file"])))


def tool_screenshot(args):
    path = args.get("path") or os.path.join(tempfile.gettempdir(),
                                            "np21w_shot.bmp")
    emu.get_to_file("/api/screenshot", path)
    return {"saved": path, "note": "BMP written to the host filesystem"}


# --------------------------------------------------------------------------
# tool registry (name -> (handler, description, input schema))
# --------------------------------------------------------------------------

def _obj(props, required=None):
    return {"type": "object", "properties": props, "required": required or []}


_ADDR = {"type": "string",
         "description": "hex address or kernel.map symbol (e.g. exec_run, "
                        "timer_handler+0x10, 0x100000)"}

TOOLS = {
    "emu_status": (tool_status,
                   "Execution state, CS:EIP (symbol-annotated), CPU mode, "
                   "display state (scrn_xmax/ymax, text_disp, grph_disp) and "
                   "fault_generation (non-zero once the core has faulted).",
                   _obj({})),
    "emu_prof_start": (tool_prof_start,
                       "Start guest profiling (clears counters). Counts the "
                       "instructions that trap in V86 mode on a 386 and every "
                       "I/O access per port.",
                       _obj({})),
    "emu_prof_stop": (tool_prof_stop,
                      "Stop guest profiling.",
                      _obj({})),
    "emu_prof": (tool_prof,
                 "Guest profile: per-class counts and rates for INT/IRET/CLI/"
                 "STI/PUSHF/POPF (the V86 #GP sources at IOPL<3), plus I/O "
                 "totals and the busiest ports -- the ports that must be "
                 "passed through via the TSS I/O permission bitmap.",
                 _obj({"top": {"type": "integer", "default": 24,
                               "description": "how many ports to list"}})),
    "emu_fault": (tool_fault,
                  "Latest unrecoverable core fault (triple fault etc.): "
                  "reason text with full register dump, CS:EIP, and a "
                  "generation counter. The core freezes at the fault site, "
                  "so emu_regs/emu_read_mem still show it until emu_resume.",
                  _obj({})),
    "emu_gdc": (tool_gdc,
                "GDC / mode F/F / CRTC state: mode1 (68h), mode2 (6Ah), "
                "display and access page (A4h/A6h), display enable, GRCG, "
                "CRTC, and the uPD7220 parameter RAM (SYNC/CSRFORM/SCROLL/"
                "PITCH) for both GDCs. All of these are write-only or "
                "unreadable from the guest, so this is the only way to "
                "compare 'what mode is the machine in' between two runs.",
                _obj({})),
    "emu_pic": (tool_pic,
                "8259A master/slave IMR/ISR/IRR/ICW. The mask register is "
                "write-only from the guest, so this is the only way to see "
                "which interrupts a machine is actually set up to take -- "
                "use it to check what mask a real BIOS leaves behind before "
                "handing a guest to a V86 monitor.",
                _obj({})),
    "emu_sound": (tool_sound,
                  "Audio output level and FM/SSG state: peak of the mixed "
                  "output since the last call (0 = silence, regardless of "
                  "synthesizer), key-on bitmask and key-on register, the "
                  "full OPN register file, and the SSG mixer/volumes. Use "
                  "this instead of I/O port counts to tell 'the guest is "
                  "writing to the chip' from 'sound is coming out'.",
                  _obj({})),
    "emu_regs": (tool_regs,
                 "All CPU registers, segment bases, CR0-4, GDTR/IDTR; EIP "
                 "annotated with symbol+offset.",
                 _obj({})),
    "emu_tvram": (tool_tvram,
                  "Text VRAM as UTF-8 (the on-screen text; screenshot "
                  "replacement).",
                  _obj({})),
    "emu_read_mem": (tool_read_mem,
                     "Read guest memory as hex.",
                     _obj({"addr": _ADDR,
                           "len": {"type": "integer", "default": 64},
                           "space": {"type": "string",
                                     "enum": ["phys", "linear", "virt"],
                                     "default": "phys"},
                           "seg": {"type": "string",
                                   "description": "segment for space=virt"}},
                          ["addr"])),
    "emu_write_mem": (tool_write_mem,
                      "Write guest memory (hex string).",
                      _obj({"addr": _ADDR,
                            "hex": {"type": "string"},
                            "space": {"type": "string", "default": "phys"}},
                           ["addr", "hex"])),
    "emu_put_file": (tool_put_file,
                     "Place a host file at an arbitrary path inside the "
                     "RUNNING guest, without a reboot or an NHD rewrite. "
                     "Use this instead of the HostDrv+hsync round trip. "
                     "guest_path may be omitted for a file the deploy "
                     "manifests already know (e.g. apps/x/x.bin).",
                     _obj({"local_path": {"type": "string",
                                          "description": "path on the host"},
                           "guest_path": {"type": "string",
                                          "description": "absolute path in the "
                                                         "guest, e.g. "
                                                         "/usr/bin/x.bin"}},
                          ["local_path"])),
    "emu_deploy_status": (tool_deploy_status,
                          "State of the guest-side file-placement agent "
                          "(present? staging window? last request).",
                          _obj({})),
    "emu_disasm": (tool_disasm,
                   "Disassemble n instructions of live guest memory via host "
                   "objdump, with kernel.map symbols on labels and branch "
                   "targets. Defaults to the current EIP.",
                   _obj({"addr": _ADDR,
                         "n": {"type": "integer", "default": 8,
                               "description": "1..256"},
                         "space": {"type": "string",
                                   "enum": ["phys", "linear", "virt"],
                                   "default": "linear"},
                         "bits": {"type": "integer", "enum": [16, 32],
                                  "description": "default: from CPU mode"}})),
    "emu_cmd": (tool_cmd,
                "Run a shell command in the OS32 rshell over the embedded "
                "serial bridge; returns its output.",
                _obj({"command": {"type": "string"}}, ["command"])),
    "emu_key": (tool_key,
                "Inject PC-98 keys. seq is comma-separated chords "
                "(SHIFT+SPACE,A,ENTER); text is literal ASCII.",
                _obj({"seq": {"type": "string"},
                      "text": {"type": "string"}})),
    "emu_pause": (tool_pause, "Pause emulation (frame granularity).", _obj({})),
    "emu_resume": (tool_resume,
                   "Resume from a pause or a breakpoint.", _obj({})),
    "emu_reset": (tool_reset, "Hard-reset the emulated machine.", _obj({})),
    "emu_break_add": (tool_break_add,
                      "Set a breakpoint at an address/symbol.",
                      _obj({"addr": _ADDR, "cs": {"type": "string"}},
                           ["addr"])),
    "emu_break_del": (tool_break_del,
                      "Delete a breakpoint (or all=true).",
                      _obj({"addr": _ADDR, "all": {"type": "boolean"}})),
    "emu_break_list": (tool_break_list,
                       "List breakpoints (EIP symbol-annotated).", _obj({})),
    "emu_step": (tool_step,
                 "Single-step n instructions; returns the new EIP. Requires a "
                 "breakpoint (trap) pause -- emu_pause alone is not enough, "
                 "so set a breakpoint and wait for trap_pause=1 first.",
                 _obj({"n": {"type": "integer", "default": 1}})),
    "emu_trace": (tool_trace,
                  "CS:EIP execution trace, symbol-annotated, one entry per "
                  "line. Returns the tail by default and collapses repeated "
                  "addresses to 'xN'.",
                  _obj({"limit": {"type": "integer", "default": 100,
                                  "description": "max lines, 1..4096"},
                        "head": {"type": "boolean", "default": False,
                                 "description": "oldest entries instead of "
                                                "newest"},
                        "collapse": {"type": "boolean", "default": True}})),
    "emu_trace_start": (tool_trace_start,
                        "Start recording the CS:EIP trace.", _obj({})),
    "emu_trace_stop": (tool_trace_stop,
                       "Stop recording the CS:EIP trace.", _obj({})),
    "emu_state_save": (tool_state_save,
                       "Save emulator state to a file (Windows path). Sends "
                       "the aidebug token from np21w_aidebug_<port>.token.",
                       _obj({"file": {"type": "string"}}, ["file"])),
    "emu_state_load": (tool_state_load,
                       "Load emulator state from a file.",
                       _obj({"file": {"type": "string"}}, ["file"])),
    "emu_screenshot": (tool_screenshot,
                       "Capture the screen to a BMP on the host filesystem "
                       "(prefer emu_tvram for text screens).",
                       _obj({"path": {"type": "string",
                                      "description": "output BMP path; "
                                                     "default: a temp file"}})),
}


# --------------------------------------------------------------------------
# JSON-RPC / MCP plumbing
# --------------------------------------------------------------------------

def _send(msg):
    sys.stdout.write(json.dumps(msg) + "\n")
    sys.stdout.flush()


def _result(req_id, result):
    _send({"jsonrpc": "2.0", "id": req_id, "result": result})


def _error(req_id, code, message):
    _send({"jsonrpc": "2.0", "id": req_id,
           "error": {"code": code, "message": message}})


def _handle(msg):
    method = msg.get("method")
    req_id = msg.get("id")

    if method == "initialize":
        _result(req_id, {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": SERVER_INFO,
        })
    elif method == "notifications/initialized":
        pass
    elif method == "tools/list":
        tools = [{"name": n, "description": d, "inputSchema": s}
                 for n, (_, d, s) in TOOLS.items()]
        _result(req_id, {"tools": tools})
    elif method == "tools/call":
        params = msg.get("params", {})
        name = params.get("name")
        args = params.get("arguments", {}) or {}
        entry = TOOLS.get(name)
        if entry is None:
            _error(req_id, -32601, "unknown tool: %s" % name)
            return
        handler = entry[0]
        try:
            out = handler(args)
        except emu.EmuError as e:
            _result(req_id, {"content": [{"type": "text",
                                          "text": "error: %s" % e}],
                             "isError": True})
            return
        except Exception as e:  # noqa: BLE001
            _result(req_id, {"content": [{"type": "text",
                                          "text": "internal error: %s" % e}],
                             "isError": True})
            return
        text = out if isinstance(out, str) else json.dumps(
            out, indent=2, ensure_ascii=False)
        _result(req_id, {"content": [{"type": "text", "text": text}]})
    elif req_id is not None:
        _error(req_id, -32601, "method not found: %s" % method)


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        _handle(msg)


if __name__ == "__main__":
    main()
