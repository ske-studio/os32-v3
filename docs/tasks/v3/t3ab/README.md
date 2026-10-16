# T3ab 段の票
状態: **実装中** (2026-10-16、Aの実装・直接ホスト検査済み、B以降は未実装)。
[T3票](../TASK_T3_LAYOUT.md) §2-1〜§2-3・§4／[上位票](../TASK_MEMMAP_V3.md) §2-1 の決定を継承。
単一タスク・協調切替、攻撃者は想定せず、誤動作/バグで固まらない・壊さない・戻せることを守る。
同じworktreeで **A→B→C→D→E→F→G** を1段ずつastraが実装。並列化しない。
A〜Fは旧・新両配置を実装し `MEM_LAYOUT_V5`（既定0）で選択、Gで案5へ一体切替。a0/a1/b0は着地済み。
既定0では固定番地・manifestの固定区間・展開先とSQLiteの宛先・ASSERTは一致。浮く終端（`__bss_end`・KHEAPの始まりなど）は予算内の移動を許し、移動量を報告する。
| 順 | 票 | 状態 |
|---|---|---|
| A | [配置の定義](A_LAYOUT_DEFS.md) | 実装・直接ホスト検査済み (PMコミット待ち) |
| B | [スタックと入口](B_STACK_ENTRY.md) | 設計中 |
| C | [ページング](C_PAGING.md) | 設計中 |
| D | [台帳](D_LEDGER.md) | 設計中 |
| E | [DMA](E_DMA.md) | 設計中 |
| F | [シェルとexec](F_SHELL_EXEC.md) | 設計中 |
| G | [値の切替](G_SWITCH.md) | 設計中 |
実装段は直接試験のみMUTATE=0＋`make kernel` (F はシェルの像も)＋check_select.py --lint。最後にOpusがレビュー/検査、`make all` はGの後にOpusのレビューの前後で1回、全体検査はその判断 (ユーザー決定 2026-10-16)。
## 見直し案への PM の判断 (2026-10-16)
1. Aで `memmap.h` に `#ifndef MEM_LAYOUT_V5` による既定0を置き、`-D` で上書き可能にする。案5の全値（T3票 §2-1〜§2-3）はAのV5=1側に定義。A〜Fで両配置を実装し、Gは既定1への切替と旧分岐・マクロ撤去だけを行う。
   ld/NASMの値は既存 `tools/gen_memmap.py` に規則を足して `$(BUILD_OUT)/memmap_layout.h` の1ファイルから渡す。Makeに切替依存を張り、AでELF/生成物/ヘッダの混在検出を追加。B/Fもこの経路に揃え、Gへ未対応を残さない。
2. DMA 可否と自己診断/PCI 起動の結線 (`kernel/kernel.c:589`) とV5=1の新DMA分岐は **E** で扱う。`ledger_arena_freeze` と `sys_usable_mem_end` の説明更新は **D**。
3. `sdk/kapi.json` の世代は **G**、R1 写しの規則 (`build/kernel.mk:320`) は **A**、`tools/check_map.d/` の登録は各段の実装パックに含める。
4. C/D の試験 (`memmap_boot_host.c`・`memory_boot_host.c`・`test_memory_boot.py`・`layout_boot_fixture.h`) は各段で明示して足す。既定0での固定部分の一致と浮動終端の予算内移動・移動量報告を各段の完了条件にする。

各段で新配置を `make kernel MEM_LAYOUT_V5=1 BUILD_OUT=build/out/v5` で別出力へリンク（配備なし）。製品ソースを取り込むホスト試験も0/1両方で作って回す。Gは撤去前に両方、撤去後は新配置を再確認する。
GでC/ASM/ld/生成物の旧分岐残留を検出する。T3b0の `layout_boot_fixture.h` の旧地図入力は参照として保持可。
