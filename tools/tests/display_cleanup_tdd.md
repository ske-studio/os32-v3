# Display cleanup: V86 CUI exits and planar pages

票: [TASK_T2D_T2H.md](../../docs/tasks/v3/TASK_T2D_T2H.md) の表示不具合の修正。

## Scope

`test_display_cleanup.py` extracts the real V86 exit functions and runs the
real splash, PC98 init/shutdown and `gfx_vram.c` through the banked host model.
Three V86 cases check normal CUI restoration, capture bypass and capture rebuild.
Eight splash cases cover four preferences and invalid-state retry. Two native
init cases seed every plane of both pages with nonzero bytes, call `gfx_init`
or `gfx_init_200`, and immediately check both pages' visible regions (32000 or
16000 bytes per plane). These are LP64 software checks; hardware, ILP32 ABI,
NP21/W, deployment and actual display timing are outside their scope.

## RED → GREEN (2026-10-03, P3)

Command (CROSS_DIR=/home/hight/opt/cross, TMPDIR=/home/hight/os32-tmp,
PYTHONPATH=, HOST32_RUNNERS=qemu):
`python3 tools/tests/test_display_cleanup.py --mutate`.

Normal controls: 3 V86 + 10 splash/init cases PASS. Sixteen mutants compiled
and exited with rc=1 and their expected `FAIL:` text, all runtime RED. Each
mutation carries its expected failure text; rc=1 alone, another failure,
compilation errors and signals do not count. V86 START, access-page,
display-page, display-enable and text-START failures have separate diagnostics.
Init-call omissions fail at `gfx_init residual page=0` or
`gfx_init_200 residual page=0` immediately after initialization. The original
fourteen mutations still check CUI/gcap restoration, old splash cleanup,
page selection, half-plane cleanup and each omitted plane.

During P3 setup, constant unification made a V86 anchor match twice and the
first run stopped with rc=1 before counting it as RED. The anchors now include
normal-exit context and match exactly once; the completed rerun returned rc=0.
Plane omissions retain their two-hit assertion. Mutations only edit a temporary
source copy, restored after every case.

## Record placement

The nearby tests use `tools/tests/<stem>_tdd.md`, discovered by the inventory's
stem convention. This separate record describes the new V86/init assertions
and sixteen mutations; `boot_splash_native_tdd.md` retains its native-dispatch
history and records the banked rendering extension. `TARGET_SRCS` exposes the
four mutation targets to the generated inventory without duplicating its table.

## T2e e6 の未結線橋 (2026-10-03)

boot_splash_native_host を使うこの実行器にも `--gc-sections` を付け、未結線の
`.text.gfx_fb_bridge` を kernel と同じく除去する。変異用の写しには新しい include
依存 `exec/appslot.h` も含める。変異の定義・置換文字列・期待 FAIL は変更しない。
正常13条件・16/16変異runtime RED、qemu指定の単独再検査 rc=0。
