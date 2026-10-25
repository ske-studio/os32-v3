# TASK_SLIM — OS の肥大化したコードを削る (カーネル・hwdev・kdebug・boot)

> 上位: [ROADMAP](../../ROADMAP.md)・規則 [POLICY_DEV](../../POLICY_DEV.md) §1 (カーネル層の不具合が先)

> 状態: **実装中 (2026-10-25)** — S1 は設計なしで実装、S2 は設計確定、S3・S4 は設計の往復 (起草 astra → レビュー Opus) 中。

## 0. ユーザー指示 (2026-10-25)

- 「肥大化したコード群を探してください。OS レベルで。ユーザーランドはまだ大丈夫」
- 「カーネルの中心的な部分なので設計はいつも通りですが、コーダーをアストラに指定します」— **実装は全段 astra** (ROLES §0 のカウンターにより、レビューと後片付けは Opus)。

## 1. 洗い出しの結果 (2026-10-25、build/out は 2026-10-24 の成果物)

大きさ: カーネル 344KB (kernel 115K・fs 61K・exec 48K・drivers 48K・kapi 22K・FatFs 15K)、hwdev.mod 131KB (他の部品は 5〜24KB)、kdebug.bin 109KB。10 行以上の文の写しはほとんど無い — 原因は表の形・置き場・関数の長さ。

| # | 何か | 大きさ | 中身 |
|---|---|---|---|
| 1 | `kdebug/recover_gf.c` の表 | hwdev.mod の 82KB | `rules[]` 48.5KB (84B × 578、欄が全部 u32)、`fixed_layouts[]` 25KB (24B × 1039、substep の連番を展開)、`targets[]` 5.5KB |
| 2 | カーネル常駐の自己試験 | 約 14KB (42 関数) | `lease_selftest`・`con_sink_selftest`・`kbd_inject_selftest`・`exec_tramp_user_selftest`・`appslot_*_selftest`・`launch_selftest`・`paging_*_selftest` など。呼ぶのは kdebug の kselftest だけ |
| 3 | `boot/loader_fat.asm` | 458 行 | KERNEL.BIN を読む旧ローダ。`make boot` で組むがどの像にも入らない。文書 2 か所と `tools/np21w_ini.py`・`tools/tests/test_np21w_ini.py` のコメントが行番号を指す |
| 4 | FD ローダと HDD ローダの二重実装 | `loader_fat_new.asm` 1050 行 | VK32 の検査・CRC32・LZ4 を asm で、HDD 側は C (`vk32_boot.c`・`lz4_mini.c`) で同じ仕様 |
| 5 | 長い関数 (120 行超が 27 本) | — | `kernel_main` 682・`exec_launch` 512・`ext2_format_range` 307・`ime_process_key` 259・`serial_init_ex` 211・`disk_read_common` 203 (`disk_write` 128 と同型の疑い) |
| 6 | `gfinit_decode_rom` | 11.8KB (ソース 104 行) | インライン展開の疑い |
| 7 | `kdebug/kselftest.c` | 2494 行・35KB | kdebug.bin の 1/3 (起動 FD の空きに効く) |

## 2. 段

| 段 | 中身 | 設計 | 実装 | 状態 |
|---|---|---|---|---|
| S1 | #3 の死んだローダを消し、行番号の参照を正しい所へ直す | 不要 | astra | 実装中 |
| S2 | #1 の表を詰める (+ #6: 原因は -O2 の展開、gfinit_decode だけ -Os)。hwdev の中で論理表へ展開し、recover.c・recover.h・HWDEV の契約・kdebug.bin は変えない。S2a (旧表の oracle) → S2c (展開 API・inspect で先に確保・試験の移し) → S2d (詰めた表) → S2e (-Os・総量) を直列 | [SLIM_HWDEV_DESIGN](../../decisions/realhw/SLIM_HWDEV_DESIGN.md) (確定 2026-10-25) | astra | 実装待ち |
| S3 | #2・#7 の自己試験をカーネルの外へ (+ #4 を調べる) | SLIM_KERNEL (astra 起草) | astra | 設計中 |
| S4 | #5 の長い関数を分ける (機械語の比べで確かめられる純粋な分割から) | SLIM_KERNEL | astra | 設計中 |

段の細分 (S2a… など) は確定設計で決める。各段の完了条件は ROLES §0 のとおり。大きさの減りは段ごとに像の大きさ (カーネル・hwdev.mod・kdebug.bin・起動 FD の空き) で記録する。
