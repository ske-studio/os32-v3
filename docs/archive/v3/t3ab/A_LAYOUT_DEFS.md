状態: **受入完了** (2026-10-17) — 6b4e37e で main へ。NP21/W 64MB の受入 (2026-10-16) と実機 Ra266 の回帰 T3bR (2026-10-17、全項目 PASS) が済んだ。

## 目的
[T3票](../TASK_T3_LAYOUT.md) §2-1〜§2-3・§4 T3ab の配置定義を準備する。
上位票 §2-1 の案5は確定。B〜Fが参照する記号を揃え、実配置の切替はGに残す。

## 触るファイルと関数
- `include/memmap.h:40` KHEAP起点、`:276` SQLite stack、`:317` metadata、`:337` shell/PT定義。
- `build/os32.ld:21` 絶対記号、`:71` core BSS、`:82` SQLite、`:128` 以降のASSERT。
- `boot/boot_defs.h:58`、`boot/loader_fat_new.asm:404` VK32展開窓の写し。
- `build/kernel.mk:320` R1像の写し・linker/ASMの共通生成定義・依存と別出力ビルド規則。
- `tools/gen_memmap.py` に既存生成規則を追加し、`build/config.mk` の旗・出力先も結線する。
- 触ってはいけないもの: B〜Fの実装、配置世代、生成器の作り直し（既存 `tools/gen_memmap.py` への規則追加は行う）、案5の番地・予算決定。

## 変えること
- **旧・新の両配置を実装し、`MEM_LAYOUT_V5` で選ぶ（既定0）**。`include/memmap.h` に唯一の切替マクロ `MEM_LAYOUT_V5`（0=今の配置、1=案5）を置く。`#ifndef MEM_LAYOUT_V5` で既定0を定義し、`-D` で上書きできる形にする。
- `__kernel_resident_end` とBSSゼロ化終端、SQLite stack上下端、PT使用終端と枠終端を区別する。
- SQLite連結・stack下guard・core BSS内のledger 2頁を新分岐へ実装。既定0では現配置を保つ。
- T3票 §2-1〜§2-3 の案5の**全部の値**をこの段で `MEM_LAYOUT_V5 == 1` 側へ定義する。DMA=0x7F0000/64KiB、0x3E0000〜0x3FFFFF=guard/PT枠26頁/guard/stack16KiB（1/26/1/4頁、PT使用11・予備15頁）。
- SLACK終端0x3E0000、KHEAP192KiB、KAPI4KiB、SHM224KiB、SQLite stack128KiB＋guard、ledger2頁、shell=[0x400000,0x500000)、pool下限0x500000、shell stack40KiB＋guard・exec256KiB・画像余裕128KiBも含む。Gで値を追加しない。
- `memmap.h` と選択マクロから `tools/gen_memmap.py` が **1ファイル** `$(BUILD_OUT)/memmap_layout.h` を生成する。ldとNASM入力はこれを共通に前処理して値を受け取り、別々の定義生成を持たない（B/Fも継承）。
- Makeにヘッダ・生成器・選択マクロの依存を張り、切替時は生成物と `.ld`・`.asm` 由来の成果物を再作成する。Cの旗にも同じ選択を渡し、通常/R1とASSERTを両配置で揃える。

## この段の検査
ホスト模型（T3b0の `tools/tests/layout_boot_fixture.h` など）でマクロ0/1の旧・新両配置を検査する。
実装時のみ、下記の直接試験を `MUTATE=0`、`make kernel`、`python3 tools/check_select.py --lint`。
変異は最後の Opus レビュー後の判断で実行し、一度に一欠陥、正常対照PASSと対応観測FAILを対にする。
- 新配置を `make kernel MEM_LAYOUT_V5=1 BUILD_OUT=build/out/v5` で別出力へ組み、リンクまで通す（配備しない。Aで上書き変数をC/生成規則へ結線し、中間成果物も分離）。
- 製品ソースを取り込むホスト試験（`memory_boot_host.c`・`memmap_boot_host.c` など、その段の直接試験）を `-DMEM_LAYOUT_V5=0` と `=1` の両方で作って回す。模型だけで代替しない。
- `test_memmap_gen.py`、`test_vmkernel_layout.py`、`test_vk32_crc.py`（いずれも `tools/tests/`）。
- ELF・共通生成物・`memmap.h` の選択値を照合し、旧新が混在した像を拒否する試験を追加。マクロ0→1→0の切替でld/ASM成果物が再生成されることも確認。
- 足す・直す観測: 旧配置の固定境界・展開先一致、CRC整合、新記号の同値、通常/R1の写し一致。
- 新配置fixtureではBSS両端4byte整列・全域包含、NOLOAD/guard非交差、鎖の空SLACK/逆転を区別。
- 代表変異: 写し1個のずれ、BSS末尾省略、枠終端と使用終端の取り違えを個別に検出。

## 段の終わりの条件
既定0で現配置を起動でき、段の前後で固定番地・manifestの固定区間・展開先とSQLiteの宛先・ASSERTは一致する。
浮く終端（`__bss_end`・KHEAPの始まりなど）は予算内の移動を許し、移動量を報告する。
固定部分の不一致や予算超過はこの段の完了に数えず、差と未確認を次段へ渡す。ゲスト未実施を合格扱いにしない。

## 次の段へ渡すもの・未確認
Bへ旧値に解決する記号一覧と通常/R1の比較根拠を渡す。
共通生成物と切替依存・別出力の規則をB/Fへ渡す。選択未対応をGへ持ち越さない（[索引](README.md)のPMの判断1〜4）。

## 段Aの実装記録 (2026-10-16)
- 基点 `9aaaf39`。既定0を維持し、案5の定義・常駐終端/ゼロ化終端・ledger BSS 2頁・SQLite guardを追加。
- `gen_memmap.py --layout {0,1} --layout-header PATH` の共通ヘッダをld/NASM/Cへ渡す。V5の中間物は `build/out/v5/` 内、通常/R1のldは同じ前処理入力。
- ELF/共通ヘッダ/選択したmemmapの照合を通常/R1の像生成に結線。両配置fixtureでNOLOAD・混在拒否・0→1→0のld/ASM/C再生成を検査。
- 既定0の固定シンボル・manifest固定区間・既存ASSERT 7本は一致。`__bss_end`/KHEAP/SQLite両端は移動0、`__bss_start`だけ−4B (直前paddingをゼロ化範囲へ包含)。
- 票と違う判断: `paging.c`/`memory_boot.c` は旧配置専用ASSERTのみ分岐 (実行時処理は未変更)。`boot.mk`・`mkvmkernel.py`・関連試験/検査登録も生成入力と切替依存へ追随。
- 証拠: `~/os32-tmp/run/t3ab/` の `final-*.log`、`a-build1.log`、`baseline-compare.log`、`layout-comparison.json`。初期のASSERT/fixture不整合は修正後に再検査。
- 統合時の未確認・未結線は [持越し台帳 T3AB-A](../../../tasks/DEFERRED_TESTS.md) に集約。段AのV5像はリンク検査用で、起動可能という判定はしない。
