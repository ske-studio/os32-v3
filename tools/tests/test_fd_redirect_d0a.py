"""T2d d0b: actual redirect and saved-AS copy, same VA / different backing.
MMU, live slots and IRQ primitives are instrumented host boundaries.
Mutants must compile successfully and then fail at runtime (never compile RED).
"""
import argparse
import pathlib
import subprocess
import tempfile
from mutpar import run_ordered

ROOT = pathlib.Path(__file__).resolve().parents[2]
# Read only the mutated implementation and fixture once, not a tree per variant.
SOURCE_FILES = ("exec/redir_access.c", "fs/fd_redirect.c",
                "tools/tests/fd_redirect_d0a_host.c")
MUTATIONS = [
    ("exec/redir_access.c", "if (ring3_call_from_user()) return caller_access_get_user(out);", "if (ring3_call_from_user()) return access_capture(out, CALLER_USER);", "registration recapture"),
    ("exec/redir_access.c", " && a->generation != 0", "", "zero generation"),
    ("exec/redir_access.c", " && a->pd_phys != 0", "", "zero PD"),
    ("exec/redir_access.c", "slot->state == APP_STATE_ABORT_PENDING ||", "", "abort pending"),
    ("exec/redir_access.c", "!slot || !slot->cpl3 || !slot->as || slot->as != a->as", "!slot || !slot->as || slot->as != a->as", "non-CPL3 registrant"),
    ("exec/redir_access.c", " || a.pd_phys != paging_current_cr3()", "", "capture CR3 mismatch"),
    ("exec/redir_access.c", "(va > MEM_APP_BAND_BASE || len > MEM_APP_BAND_BASE - va)", "0", "trusted range"),
    ("fs/fd_redirect.c", "if (rc < 0 || (u32)rc < count) redir_refuse_count++;", "if (0) redir_refuse_count++;", "refusal counter"),
    # These mutate the substitute as_access_page, not the production walk.
    # Production registrant-PD mutations live in test_access_walk.py (d3).
    ("tools/tests/fd_redirect_d0a_host.c", "as_va_to_pa(as->pd_phys, va, pa)", "as_va_to_pa(paging_current_cr3(), va, pa)", "substitute current PD write"),
    ("tools/tests/fd_redirect_d0a_host.c", "as_va_to_pa_read(as->pd_phys, va, pa)", "as_va_to_pa_read(paging_current_cr3(), va, pa)", "substitute current PD read"),
    ("exec/redir_access.c", "kmemcpy(P2V(pa), bytes + done, n)", "kmemcpy((void *)(uptr)va, bytes + done, n)", "VA write"),
    ("exec/redir_access.c", "kmemcpy(bytes + done, P2V(pa), n)", "kmemcpy(bytes + done, (void *)(uptr)va, n)", "VA read"),
    ("exec/redir_access.c", "slot->as->generation == a->generation", "1", "generation reuse"),
    ("exec/redir_access.c", "slot->as->owner == a->owner", "1", "owner mismatch"),
    ("exec/redir_access.c", "slot->as->pd_phys == a->pd_phys", "1", "PD mismatch"),
    ("exec/redir_access.c", "!slot || !slot->cpl3 || !slot->as || slot->as != a->as", "!slot || !slot->cpl3 || !slot->as", "AS identity"),
    ("exec/redir_access.c", "if (!redir_live(a)) return 0;", "if (0 && !redir_live(a)) return 0;", "dead registrant"),
    ("exec/redir_access.c", "as_access_page(a->as, va, write, pa)", "as_access_page(a->as, va, write ? 0 : 0, pa)", "RO output"),
    ("exec/redir_access.c", "if (!redir_access_check(a, va, len, write)) return -1;", "if (0 && !redir_access_check(a, va, len, write)) return -1;", "no preflight"),
    ("exec/redir_access.c", "irq_restore(flags);\n        done += n;", "irq_restore(1);\n        done += n;", "IF forced on"),
    ("exec/redir_access.c", "while (done < len) {\n        u32 pa, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));", "while (done < len) {\n        u32 pa, n = len - done;", "unbounded IRQ copy"),
    ("fs/fd_redirect.c", "out->fd[fd] = redir_table[fd];", "out->fd[fd] = redir_table[fd]; out->fd[fd].access = (RedirAccess){0};", "save loses identity"),
    ("fs/fd_redirect.c", "redir_table[fd] = in->fd[fd];", "redir_table[fd] = in->fd[fd]; redir_table[fd].access = (RedirAccess){0};", "restore loses identity"),
    ("fs/fd_redirect.c", "redir_table[fd].access = access;", "redir_table[fd].access = access; redir_table[fd].access.origin = REDIR_TRUSTED;", "origin lost"),
]


def run(root, tmp, quiet=False):
    exe = tmp / "d0b"
    subprocess.run([
        "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-D__cdecl=",
        *["-I" + str(ROOT / d) for d in ("include", "kernel", "exec", "fs", "lib", "sdk/include/os32")],
        str(root / "tools/tests/fd_redirect_d0a_host.c"), "-o", str(exe),
    ], check=True, capture_output=True, text=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    if not quiet:
        print(result.stdout + result.stderr, end="")
    return result.returncode


def mutant(item, sources):
    rel, old, new, name = item
    with tempfile.TemporaryDirectory(prefix="os32-d0b-mut-") as tmp:
        tmp = pathlib.Path(tmp)
        for path, content in sources.items():
            dst = tmp / path
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(content)
        src = tmp / rel
        text = src.read_text()
        assert text.count(old) == 1, (name, text.count(old))
        src.write_text(text.replace(old, new))
        rc = run(tmp, tmp, quiet=True)  # Compilation failure raises: never counted RED.
        return name, rc != 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutate", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="os32-d0b-") as tmp:
        if run(ROOT, pathlib.Path(tmp)) != 0:
            return 1
    if args.mutate:
        sources = {path: (ROOT / path).read_text() for path in SOURCE_FILES}
        results = list(run_ordered(lambda item: mutant(item, sources), MUTATIONS))
        for name, red in results:
            print(f"{'RED (runtime)' if red else 'SURVIVED'}: {name}")
        print(f"MUTATIONS {sum(red for _, red in results)}/{len(results)} runtime RED")
        return 0 if all(red for _, red in results) else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
