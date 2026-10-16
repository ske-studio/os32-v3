状態: **設計中** (2026-10-16、実装・検査未実施)。

## 目的
[T3票](../TASK_T3_LAYOUT.md) §2-1・§2-2・§4 T3ab の台帳区間と供給下限を担当する。
b0の新旧模型を実装へ結ぶ準備をし、ページの二重供給と固定予約の取り落としを防ぐ。

## 触るファイルと関数
- `kernel/memory_boot.c:22` FIXED導出、`:211` 固定区間表、`:281` memory_boot_regions、`:307` memory_boot_init。
- `kernel/physmem.c:90` physmem_bootstrap_legacy、`:156` physmem_reserve_ram。
- `kernel/pgalloc.c:233` pgalloc_init_layout、`:635` ledger_register_region、`:1039` ledger_arena_freeze。
- 触ってはいけないもの: CのPTE実装、DMA内部allocator、配置値、owner/reclaimの一般契約。

## 変えること
- **旧・新の両配置を実装し、`MEM_LAYOUT_V5` で選ぶ（既定0）**。symbol由来区間はruntime構築し、byte半開→整列済PFN半開へ変換、空区間を省く。
- 新分岐でcoreをledger 2頁の前後で分割しmetadataとの重複を排除。ARENA_TOPでもcoreの2頁はkernel保持。
- SLACKはRAM/source=KERNEL_SLACKとして一度だけ供給。新分岐では旧SQLite/shell予約を除き、Gで残留・二重供給を検査する。
- DMAは一般池・metadata・BB配布より先にDMA/kernel/WBで恒久予約する。RAM不足と予約重複を区別。
- pool/workspace/BB下限を定義へ接続。ledger_arena_freezeの最初のPERSISTで止まる意味を維持し、V5=1側のDMA予約で低位連続上端が変わることを明記する。

## この段の検査
ホスト模型（T3b0の `tools/tests/layout_boot_fixture.h` など）でマクロ0/1の旧・新両配置を検査する。
実装時のみ、下記の直接試験を `MUTATE=0`、`make kernel`、`python3 tools/check_select.py --lint`。
変異は最後の Opus レビュー後の判断で実行し、一度に一欠陥、正常対照PASSと対応観測FAILを対にする。
- 新配置を `make kernel MEM_LAYOUT_V5=1 BUILD_OUT=build/out/v5` で別出力へ組み、リンクまで通す（配備しない。Aで上書き変数をC/生成規則へ結線し、中間成果物も分離）。
- 製品ソースを取り込むホスト試験（`memory_boot_host.c`・`memmap_boot_host.c` など、その段の直接試験）を `-DMEM_LAYOUT_V5=0` と `=1` の両方で作って回す。模型だけで代替しない。
- `tools/tests/memory_boot_host.c`・`memmap_boot_host.c`・`layout_boot_fixture.h` の両配置模型も追随。
- `tools/tests/test_memory_boot.py`、`test_memmap_boot.py`、`test_physmem.py`、`test_pgalloc_model.py`、`test_pgalloc_range.py`。
- 足す・直す観測: FIXED/ARENA_TOP、空SLACK/逆転、SLACK全頁の一度だけの割当・返却、固定頁混入0。
- 新配置模型でDMA16頁予約、8MiB未達の装置無効化用結果、予約重複停止、metadata/BB非交差を確認。
- 代表変異: metadata二重登録、旧pool下限残留、DMA予約漏れを一つずつ検出。

## 段の終わりの条件
既定0で現配置を起動でき、段の前後で固定番地・manifestの固定区間・展開先とSQLiteの宛先・ASSERTは一致する。
浮く終端（`__bss_end`・KHEAPの始まりなど）は予算内の移動を許し、移動量を報告する。
固定部分の不一致や予算超過はこの段の完了に数えず、差と未確認を次段へ渡す。ゲスト未実施を合格扱いにしない。

## 次の段へ渡すもの・未確認
EへDMA予約の成否・不足の区別と恒等写像確認の前提を渡す。
`sys_usable_mem_end` の低位連続上端の説明更新はDが担当。起動結線はE、実配置切替はG（[索引](README.md)のPMの判断1〜4）。
