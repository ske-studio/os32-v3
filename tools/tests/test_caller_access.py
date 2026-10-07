"""T2d d1: real syscall dispatcher + shared caller/redirect identity.

Mutations compile then fail at runtime. Only the two C sources and fixture are
copied per variant; headers use the original read-only include paths.
"""
import argparse
import pathlib
import subprocess
import tempfile
from mutpar import run_ordered

ROOT = pathlib.Path(__file__).resolve().parents[2]
MUTATIONS = [
    ("access", "result.owner = caller.owner;", "result.owner = caller.owner + 1;", "identity wrong AS"),
    ("access", "result.generation = caller.generation;", "result.generation = caller.generation + 1;", "identity wrong generation"),
    ("dispatch", "frame[V86I_EFLAGS] & EFLAGS_VM", "0", "VM int80 bypasses KAPI"),
    ("access", "if (ring3_wm_depth > 0)", "if (0)", "WM trusted scope"),
    ("access", "if (user_only && a->origin != CALLER_USER)", "if (0 && user_only)", "saved USER only"),
    ("access", "caller_frame.valid = 0;", ";", "invalidation"),
    ("dispatch", "ring3_caller_reject_count++;", ";", "entry rejection counter"),
    ("wm", "ring3_wm_depth--;", ";", "WM trusted leakage"),
    ("access", " || a.pd_phys != paging_current_cr3()", "", "entry CR3"),
    ("access", "res_owner_get() != a.app_id", "0", "entry owner"),
    ("access", "a->pd_phys == paging_current_cr3()", "1", "current CR3"),
    ("access", "a->app_id == appslot_cur()", "1", "current slot"),
    ("access", "res_owner_get() == a->app_id", "1", "current owner"),
    ("access", "slot->as->generation == a->generation", "1", "generation"),
    ("access", "slot->as->owner == a->owner", "1", "AS owner"),
    ("access", "slot->as->pd_phys == a->pd_phys", "1", "AS PD"),
    ("access", " || slot->as != a->as", "", "AS identity"),
    ("access", "slot->state == APP_STATE_ABORT_PENDING ||", "", "abort pending"),
    ("access", "slot->state == APP_STATE_FAULT_PENDING", "0", "fault pending"),
    ("access", "caller_frame.valid && redir_live(a)", "redir_live(a)", "inactive frame"),
    ("access", "*previous = caller_frame;", "*previous = (CallerAccessFrame){0};", "nested save"),
    ("access", "caller_frame = *previous;", "(void)previous;", "normal restore"),
    ("access", "if (ok) *out = *a;", "*out = *a;", "rejected output"),
    ("access", "origin != CALLER_USER && origin != CALLER_TRUSTED", "0", "unknown origin"),
    ("dispatch", "caller_access_enter(&prev_caller, CALLER_USER)", "caller_access_enter(&prev_caller, CALLER_TRUSTED)", "dispatcher origin"),
    ("dispatch", "caller_access_leave(&prev_caller);", "(void)prev_caller;", "dispatcher leave"),
    ("dispatch", "ring3_in_syscall = prev_in_syscall;", "ring3_in_syscall = 0; (void)prev_in_syscall;", "nested guard"),
]


def function(source, name):
    start = source.index("void " + name + "(void)\n{")
    return source[start:source.index("\n}", start) + 2]

def sources():
    s = (ROOT / "exec/exec.c").read_text()
    a = s.index("void __cdecl ring3_syscall_dispatch(u32 *frame)")
    b = s.index("\n}", a) + 2
    return {"access": (ROOT / "exec/redir_access.c").read_text(),
            "dispatch": s[a:b],
            "wm": function(s, "ring3_wm_enter") + "\n" + function(s, "ring3_wm_leave"),
            "host": (ROOT / "tools/tests/caller_access_host.c").read_text()}


def run(src, quiet=False):
    with tempfile.TemporaryDirectory(prefix="os32-d1-") as folder:
        tmp = pathlib.Path(folder)
        (tmp / "exec").mkdir()
        (tmp / "tools/tests").mkdir(parents=True)
        (tmp / "exec/redir_access.c").write_text(src["access"])
        host = tmp / "tools/tests/caller_access_host.c"
        host.write_text(src["host"])
        (host.parent / "dispatcher.inc").write_text(src["dispatch"])
        (host.parent / "wm.inc").write_text(src["wm"])
        exe = tmp / "caller"
        result = subprocess.run([
            "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            "-Wno-int-to-pointer-cast", "-Wno-pointer-to-int-cast", "-D__cdecl=",
            *["-I" + str(ROOT / d) for d in ("include", "kernel", "exec", "fs", "lib", "sdk/include/os32")],
            str(host), "-o", str(exe),
        ], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("compile failure (not RED):\n" + result.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
        if not quiet:
            print(result.stdout + result.stderr, end="")
        if result.returncode == 0:
            compare_host_identity(result.stdout)
        return result.returncode


def compare_host_identity(output):
    # Feed the actual host observer an independent memory image of the two C
    # fixture ASes (app 2/3, owner 12/13, generation 22/23). Never build expected
    # memory from the kernel-returned tuple.
    import importlib.util
    import struct
    spec = importlib.util.spec_from_file_location('h3_identity', ROOT/'tools/h3_park_resume.py')
    h3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(h3)
    class Memory:
        def __init__(self): self.mem = bytearray(0x10000)
        def read(self, address, size): return bytes(self.mem[address:address+size])
        def word(self, address, value): self.mem[address:address+4] = struct.pack('<I', value)
    memory = Memory()
    o = dict(block_count=2, app_min=2, app_max=5, slot_size=16, slot_state=0,
             slot_as=4, free=0, as_owner=0, as_generation=4, ledger_count=64,
             ledger_size=16, ledger_kind=0, ledger_id=1, ledger_pages=4,
             ledger_as=2, shm_delta=4096, block_size=16384)
    symbols = dict(shm_state=0x100, shm_block_span=0x200, shm_block_owner=0x300,
                   g_slot=0x400, ledger_owners=0x1000, __bss_end=0x5000)
    observer = object.__new__(h3.Playbook)
    observer.o, observer.s, observer.emu = o, symbols, memory
    for index, app in enumerate((2, 3)):
        memory.mem[0x100+index] = 1
        memory.word(0x200+index*4, 1); memory.word(0x300+index*4, app)
        memory.word(0x400+app*16, 1); memory.word(0x404+app*16, 0x800+app*16)
        memory.word(0x800+app*16, app+10); memory.word(0x804+app*16, app+20)
        ledger = 0x1000+(app+10)*16
        memory.mem[ledger:ledger+2] = bytes((2, app)); memory.word(ledger+4, 1)
    seen = set()
    for line in output.splitlines():
        if not line.startswith('IDENTITY '): continue
        app, owner, generation = map(int, line.split()[1:])
        expected = observer.identity(app-2)
        assert (app, owner, generation) == tuple(expected[k] for k in ('app','owner','generation'))
        seen.add(app)
    assert seen == {2,3}, 'both current AS identities observed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutate", action="store_true")
    args = parser.parse_args()
    original = sources()  # Read actual source once, not a whole tree per mutant.
    if run(original):
        return 1
    if args.mutate:
        def mutant(item):
            key, old, new, name = item
            src = original.copy()
            assert src[key].count(old) == 1, (name, src[key].count(old))
            src[key] = src[key].replace(old, new)
            return name, run(src, quiet=True) != 0
        results = list(run_ordered(mutant, MUTATIONS))
        for name, red in results:
            print(f"{'RED (runtime)' if red else 'SURVIVED'}: {name}")
        print(f"MUTATIONS {sum(red for _, red in results)}/{len(results)} runtime RED")
        return 0 if all(red for _, red in results) else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
