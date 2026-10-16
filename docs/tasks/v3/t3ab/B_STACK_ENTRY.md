状態: **実装中** (2026-10-16、旧/新の入口・スタック直接ホスト検査を実装、ゲスト受入待ち)。

## 目的
[T3票](../TASK_T3_LAYOUT.md) §2-1・§2-2・§4 T3ab の入口・ゼロ化・stack参照を揃える。
Aの記号を使い、現在の起動順と復帰経路を保ったままCへ渡す。

## 触るファイルと関数
- `kernel/kentry.asm:32` BSSゼロ化、`:82` 初期ESP。
- `kernel/kstack_hw.c:11` kstack_hw_init、`:31` kstack_high_water。
- `kernel/isr_handlers.c:240` exception_handler、`:339` page_fault_handler（分類境界・復帰参照）。
- `kernel/kernel.c:243` TSS、`:731` SQLite stack引数。
- 触ってはいけないもの: paging/台帳/ドライバ本体、実配置、Unicode、割り込み・fault回収契約。

## 変えること
- **旧・新の両配置を実装し、`MEM_LAYOUT_V5` で選ぶ（既定0）**。初期ESP・TSS.esp0・例外経路・高水位計測を同じstack定義へ接続する。
- core/SQLite BSSの別範囲ゼロ化は既存（`kentry.asm:33-46`）を維持し、新配置のguardを対象から外す準備をする。stack引数はAの記号へ。
- `kentry.asm` はAの共通生成物 `$(BUILD_OUT)/memmap_layout.h` を前処理で読み、同じマクロの値を使う。生成物の依存も張る。
- SQLite連結後にもfault分類が旧0x200000境界へ依存しない形を準備する。
- DMA予約→自己診断→PCI起動とRAM不足時のDMA装置無効化の結線はEだけが担当する。

## この段の検査
ホスト模型（T3b0の `tools/tests/layout_boot_fixture.h` など）でマクロ0/1の旧・新両配置を検査する。
実装時のみ、下記の直接試験を `MUTATE=0`、`make kernel`、`python3 tools/check_select.py --lint`。
変異は最後の Opus レビュー後の判断で実行し、一度に一欠陥、正常対照PASSと対応観測FAILを対にする。
- 新配置を `make kernel MEM_LAYOUT_V5=1 BUILD_OUT=build/out/v5` で別出力へ組み、リンクまで通す（配備しない。Aで上書き変数をC/生成規則へ結線し、中間成果物も分離）。
- 製品ソースを取り込むホスト試験（`memory_boot_host.c`・`memmap_boot_host.c` など、その段の直接試験）を `-DMEM_LAYOUT_V5=0` と `=1` の両方で作って回す。模型だけで代替しない。
- `tools/tests/test_vmkernel_layout.py`、`test_vk32_crc.py` に入口・BSS範囲の観測を足す。
- stack記号照合と高水位範囲の直接fixtureを追加する場合は `tools/check_map.d/` 登録も実装パックへ。
- 観測: 初期ESP/TSS一致、BSS両範囲の全byteゼロ・guard非書込み、正常/fault後の復帰先。
- 代表変異: SQLite BSSゼロ化漏れ、ESPだけ旧記号、guardを含むゼロ化を別々に検出。

## 段の終わりの条件
既定0で現配置を起動でき、段の前後で固定番地・manifestの固定区間・展開先とSQLiteの宛先・ASSERTは一致する。
浮く終端（`__bss_end`・KHEAPの始まりなど）は予算内の移動を許し、移動量を報告する。
固定部分の不一致や予算超過はこの段の完了に数えず、差と未確認を次段へ渡す。ゲスト未実施を合格扱いにしない。

## 次の段へ渡すもの・未確認
Cへ入口・BSS・stackの共通記号と起動順を渡す。SQLite DB/FEPと例外復帰のゲスト観測は未実施。
DMA可否の起動結線はE（[索引](README.md)のPMの判断1〜4）。TSSへは既にMEM_KSTACK_TOPを渡しており、不要な変更を足さない。

## 実装記録 (2026-10-16)
- 入口ESPは生成ヘッダの `LAYOUT_MEM_KSTACK_TOP`、fault表示境界は `__sqlite_start`。TSS/SQLite引数/高水位は既存の共通マクロを維持。
- 製品入口ASM・TSS/高水位C・SQLite trampoline・setjmp/longjmpを0/1で直接実行。core/SQLite全byteゼロ、guard不変、引数/ESP/TSS、正常復帰とfault復帰分岐を観測。CR0/LTRとexec teardownはホスト代替。
- 検査と固定部/浮動端の比較は `~/os32-tmp/run/t3ab/REPORT_B.md` と `b-*.log`。持越しは [台帳](../../DEFERRED_TESTS.md) T3AB-B。
