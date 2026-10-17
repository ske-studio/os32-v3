状態: **受入完了** (2026-10-17) — 6b4e37e で main へ。NP21/W 64MB の受入 (2026-10-16) と実機 Ra266 の回帰 T3bR (2026-10-17、全項目 PASS) が済んだ。

## 目的
[T3票](../TASK_T3_LAYOUT.md) §2-3・§4 T3ab の常駐シェル最小境界結線を準備する。
Gを旧sbrk限界で起動させない。T3c1以降の詳細受入・性能測定は残す。

## 触るファイルと関数
- `exec/exec.c:1711` shell配置、`:1885` ctx限界、`:1998` 常駐heap初期化、`:1131` 子終了時復元。
- `exec/exec_heap.c:193` exec_heap_init_at、`:243` exec_heap_restore_state。
- `exec/appmem.c:246` appmem_init（USER経路の確認）、`sdk/allocator/nano_adapter.c:120` initialize、`:146` os32_nano_crt_sbrk。
- `sdk/link/app_sys.ld:8` 配置、`:29` _end、`build/programs.mk:39` resident分離、`:83`・`:144` shell/gshellリンク。
- 触ってはいけないもの: USER allocator方針、公開KAPI構造、F-9対策、Unicode、T3cの性能調整。

## 変えること
- **旧・新の両配置を実装し、`MEM_LAYOUT_V5` で選ぶ（既定0）**。旧shell/sbrk/exec各境界を定義参照へ揃え、既定0では現配置、新分岐では1MiB帯・256KiB分割を実装する。
- 新分岐の共通計算 `{image_end, sbrk_limit=exec_base, exec_base, exec_end=heap_ceiling}` を実装する。
- 新配置は40KiB stack＋guardを引き、ceiling直下256KiBをexecへ、画像末尾との余裕128KiB以上。
- 新配置のheap_szは0→256KiB、1〜256KiBは下詰め、超過は入口前拒否。余りをsbrkへ戻さない。
- resident CRTへ共通限界を渡し、子終了はrestore_state。USER mem_map経路とresidentビルド分離を保持。
- `app_sys.ld` もAの共通生成物 `$(BUILD_OUT)/memmap_layout.h` を前処理で読み、同じマクロの値を使う。生成物の依存も張る。

## この段の検査
ホスト模型（T3b0の `tools/tests/layout_boot_fixture.h` など）でマクロ0/1の旧・新両配置を検査する。
実装時のみ、下記の直接試験を `MUTATE=0`、`make kernel` と `make userland/shell.bin gshell sh shlib`、`python3 tools/check_select.py --lint`。
変異は最後の Opus レビュー後の判断で実行し、一度に一欠陥、正常対照PASSと対応観測FAILを対にする。
- 新配置を `make kernel MEM_LAYOUT_V5=1 BUILD_OUT=build/out/v5` で別出力へ組み、リンクまで通す（配備しない。Aで上書き変数をC/生成規則へ結線し、中間成果物も分離）。
- 製品ソースを取り込むホスト試験（`memory_boot_host.c`・`memmap_boot_host.c` など、その段の直接試験）を `-DMEM_LAYOUT_V5=0` と `=1` の両方で作って回す。模型だけで代替しない。
- `nano_adapter.c` はUSERのCRTの依存でもあるため、USER側のビルドは `sh.bin` で代表させる。
- `tools/tests/test_exec_budget.py`、`test_exec_r1.py`、`test_nano_adapter.py`、`test_appmem.py`。
- 足す・直す観測: 現境界不変、新配置模型のheap_sz=0/1/256KiB/+1、画像余裕128KiBの境界。
- sbrk上限+1・負方向下限超え・overflowでbreak/両heap不変、子正常/fault/STOP後の親sentinel保持。
- 代表変異: sbrk/exec限界混同、失敗後break更新、余り再供給を個別に検出。

## 段の終わりの条件
既定0で現配置を起動でき、段の前後で固定番地・manifestの固定区間・展開先とSQLiteの宛先・ASSERTは一致する。
浮く終端（`__bss_end`・KHEAPの始まりなど）は予算内の移動を許し、移動量を報告する。
固定部分の不一致や予算超過はこの段の完了に数えず、差と未確認を次段へ渡す。ゲスト未実施を合格扱いにしない。

## 次の段へ渡すもの・未確認
Gへ最小境界結線・旧値の同値根拠と新配置模型の結果を渡す。Gでマクロを1にして旧分岐とマクロを撤去する（[索引](README.md)のPMの判断1〜4）。
T3c1の詳細境界受入、c2の20回往復・個別peak、c3/c4のF-9測定は未確認のまま残す。

## 段 F の実装記録 (2026-10-16)
- 共通分割を入口前に確定し、resident CRTへsbrk上限を渡す。小さいheapの復元も初期化と同じ正規化で空/端数を保持する。
- `app_sys.ld`は共通生成ヘッダを使用。新配置のshell/gshellは`build/out/v5/resident/`へ分離し、heap要求0で256KiBを選ぶ。
- 票外: `memmap.h`の分割/整列定数、`mkos32x.py`のELF番地参照、`gen_memmap.py`の写し検査、関連ホスト試験/入力登録。既存USER方針・公開KAPIは維持。
- 証拠: `~/os32-tmp/run/t3ab/REPORT_F.md`、`f-final-*.log`、`f-comparison.json`。T3cのゲスト詳細受入・性能測定は未確認。
