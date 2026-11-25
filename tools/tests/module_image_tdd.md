# KMOD image validator host regression record

契約: `docs/design/kernel/module/module_image.md`、拒否する条件の一覧は `docs/08_build.md` §8-2「起動時に読む部品の形式 (KMOD v1)」。出所は票 TASK_DBGREF の §3-2・§4。
実行: `make check-module-image-host MUTATE=1 HOST32_RUNNERS=qemu`。

The host harness calls the unconnected production validator with an independently
encoded container and a trusted import/export policy. Valid fixtures exercise
unaligned and unsorted relocations, adjacent write intervals, one-past pointer
values, and a relocation at the final payload word. Target C also compiles with
GNU11 and warnings treated as errors.

The fourteen required rejection groups are future version, header size, image
size, table range, table overlap, relocation overlap, relocation range, import
slot, export slot, name hash, CRC, pair, image-valid, and ABI/ops mismatch.
Additional fixtures exercise header, section, name and import metadata bounds.
RED means a malformed fixture returns the specified rejection status, not a
compiler failure. `--mutate` additionally changes fourteen validator predicates
in isolated copies and requires the fixture suite to detect each change.

No claim of a test-first implementation sequence is made. These are regression
and mutation checks. Final run results belong in the task completion report.
Native execution is not used in the Codex sandbox; qemu runs the ILP32 harness.
Emulator, real hardware, loader integration and product linkage are outside S0.

## T4a-1 common schema / ID policy

`tools/module_schema.py` is consumed by mkmod and generates
`sys/kern/module/module_schema.h` through `tools/gen_module_schema.py`. The independent
wire fixture above keeps its explicit byte offsets. Caller-selected IDs must
match the image and an admitted policy row (`MODULE_COMPONENT_POLICIES`, generated
from `COMPONENTS`; SQLITE and the later components have rows).
KDEBUG still requires POST_ROOT, PAIR, no stop, ops v2 / 18 slots, and the existing
image/body and table limits. Old ops also reject when the caller claims v1.

The harness checks both header-only and whole-image validation, including NULL
outputs: input bytes stay unchanged, failed outputs retain their sentinel, and
successful plans match independently decoded words and load_span. Added source
mutants remove ID, component ops, image-size and body-size predicates; the
normal suite includes counterexamples for each. Implementation-stage execution
uses `MUTATE=0`; mutation execution belongs to the PM after review.

T4a-2: 共通 `module_image_relocate` の検証後書込は
`check-sqlite-module-proto-host` で非整列/末尾/書込0/base overflowを確認する。
ILP32 harness の末尾 guard と out の不変確認は両試験で共用する。
