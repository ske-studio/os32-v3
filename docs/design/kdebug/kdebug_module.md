# sys/kern/kdebug/kdebug_module.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: VER_INFO_DESIGN・HWDEV_CORE_API・HWDEV_DESIGN (D1 で移した)
ソース: `sys/kern/kdebug/kdebug_module.c`

kdebug_state_name()
state<0 / 配列上限以上→UNKNOWN
13〜16 は RECORD_FULL/BAD_DEPENDENCY/BUSY/QUARANTINED になる。

既存 19 口の順序は維持。

- kdebug の既存 import は未使用になっても削除・並替え禁止、必要分だけ末尾追加。内部 ops 版 / 契約 CRC も更新。

KDEBUG=1 のみロード、PAIR は kernel 像 CRC。
任意の装置部品を自動発見・交換する仕組みはない

いまは版 4・20 口 (`tools/module_schema.py`、08_build §8-2)。

## 読み込みと公開 (`kdebug_load`)

> 出所: TASK_DBGREF (§3-1・§3-4・§3-7) の契約を今のソースと照らして要約 (2026-11-18)

ソース: `sys/kern/kdebug/kdebug_module.c` (`kdebug_load`・`kdebug_start`・`kdebug_state_name`)・`sys/platform/pc98/kdebug/kdebug_main.c` (`kdebug_init`)・`sys/kern/module/kdebug_call.c` (`kdebug_call`)。読み込みの共通の手順は `sys/kern/module/module_runtime.c` (`docs/design/kernel/module/module_runtime.md`)、形式と拒否の条件は `docs/08_build.md` §8-2「起動時に読む部品の形式 (KMOD v1)」。

- `system.cfg` の `KDEBUG` が **1 のときだけ** `/boot/kdebug.bin` を読む。0 やそれ以外の値なら開かず、owner も取らず、状態は OFF (0)。読むのは 1 回だけで、試みると `kdebug.open_count` が 1 になる。
- 期待する ID・KAPI 版・layout 世代・`KDEBUG_ABI_GEN`・`KDEBUG_OPS_VERSION`・import の件数・本体の Image CRC (`boot_common_get` の `image_crc` / `image_valid`) は **本体の側** から渡す (像の自己申告で決めない)。
- 失敗は起動を止めない。`kdebug.state` に理由 (`kdebug_state_name` の名前: NOT_FOUND・BAD_HEADER・PAIR_MISMATCH・NO_IMAGE_CRC・ABI_MISMATCH・NO_PAGES・SHORT_READ・BAD_CRC・INIT_FAILED・RELOC_INVALID・IMPORT_INVALID・…) を残し、`[kdebug] rejected: <名前>` を 1 行出す。口の表 `kdebug.ops` は全部 NULL のまま。
- 成功したら公開表を `kdebug.ops` に写し、最後に `state = RUNNING` にする。本体の起動中の自己試験の呼び出しは `kdebug.state == KDEBUG_RUNNING` を条件にする (口が非 NULL かでは決めない)。
- KAPI `kdebug_call` は `dbg_call` の口が NULL なら `OS32_ERR_NOSYS`。

### 部品の `init` の契約

`kdebug_init(candidate)` は **候補表に自分の `kdebug_ops` を写して 0 を返すだけ**。import を 1 つも呼ばず、割当・登録・装置の操作をしない。だから init が失敗しても、ローダの回収 (owner 単位) だけで元に戻る。ローダは init の戻りが 0 で、候補表が像の export 表と一致したときだけ公開する。

検査: `make check-kdebug-init-host` (`test_kdebug_init.py (旧版 f63933516995)`)。実物の `kdebug_main.c` の init を ILP32 で呼んで候補表と import 呼び出し 0 を見る。実物の `kdebug_module.c`・`module_runtime.c`・`module_image.c` と実際に組んだ部品で、OFF・他の設定値・RUNNING・open / stat / ヘッダ読み / owner / 確保 / 本体読みの失敗・`image_valid` 0・ヘッダ / 対 / ABI / CRC / reloc / import / export の拒否・init の失敗を通す (import はすべて数える罠に結ぶ)。拒否と OFF の像は init の先頭を UD2 にして、init が呼ばれないことを見る。
