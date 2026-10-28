# KMOD producer and private import ABI host regression record

票: `docs/archive/v3/TASK_DBGREF.md` §3-2・§3-6・§4、依頼パック `~/os32-tmp/run/dbgref/PACK_s0.md`。
実行: `make check-mkmod-host MUTATE=1 HOST32_RUNNERS=qemu`。

`test_mkmod.py` links a real i386 ELF with generated 211 import slots and stubs,
an unaligned absolute relocation, an internal PC32 call and two ops exports.
It produces a container, passes it to the C validator, checks the embedded
VK32 pair CRC and container CRC, and compares CLI output with the API result.
Malformed ELF, unsupported/unresolved relocation inputs, invalid import metadata
and a corrupt VK32 image must fail. `--mutate` checks isolated producer copies
with missing absolute relocations, retained PC32, a zero pair, and a wrong export
slot. Expected assertion/rejection detects a mutant; compilation is not the RED.

`test_kdebug_imports.py` compares actual original object undefined-symbol sets
against S1/S2/S3 definitions, accounting explicitly for internal math symbols,
the link-generation marker, retained-kernel `__bss_end`, data accessors, and six
new operations. It compiles generated kernel exports/accessors, slots and stubs
at each stage, compares stub names and slot byte counts, and compiles a macro
consumer including the structure-pointer and array cases. Negative probes for
an altered import set and invalid index always run, as do ABI-hash sensitivity
and authoritative declaration-extraction checks; its `--mutate` does not add
source mutants. Most types come from headers; four existing local externs are
read from their original C files because no header declares them.

No test-first sequence is claimed. The measured prefix is a fixture result with
211 imports and two exports, not a measurement of the future complete S3 body.
The full body relocation count and its final prefix remain unmeasured in S0.
Final GREEN/RED execution results belong in the task completion report.
No emulator/hardware acceptance, deployment or product integration is exercised.

## T4a-1

The current fixture uses all 235 generated S4 imports and two exports against
the unchanged ops v2 / 18-slot contract. It checks the checked-in C schema against
`gen_module_schema.render()` and passes current explicit KAPI/layout generations
from `sdk/kapi.json` to both producer and validator. Omitting either generation
from the CLI rejects without changing the existing output; omitting API
generations raises TypeError. Unknown module IDs, old ELF ops, duplicate names
and a fixed FNV collision (`costarring` / `liquid`) reject during generation.
S4 contract regeneration remains byte-for-byte identical in the loader test.
The implementation run uses `MUTATE=0`; PM runs the maintained mutants later.
