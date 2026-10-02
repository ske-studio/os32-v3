# v1.3 メモリ予算 — ホスト側測定

> 状態: **完了記録 (v1.3)** — ホストビルドの静的測定。ゲスト空き容量・ピーク使用量・断片化は未測定。
親: [PLAN.md](../../tasks/gui/v13/PLAN.md)、判定条件: [REVIEW_T0_T1.md](../gui_v13_reviews/REVIEW_T0_T1.md) R3。

## 測定対象

`make gshell` は成功。現行ソースからRust releaseビルドと再リンクを実行した。
clean/full buildは行っていない。Cライブラリは既存のMake依存関係で使用した。
配備・エミュレータ操作は未実施。

- ELF: `userland/gshell.elf`
- ELF SHA-256: `8d247c9e3916f33bae6cebadf6181de66dd3e2579622a3eb122d65c452853a69`
- BIN SHA-256: `0c447320650e208d06c86f900764bb1f82c8793c83f13d3b56105d1d69a89409`
- linker警告: libc_a-sbrkr.oのGNU-stack note欠如、およびLOAD segmentのRWX属性。
  ビルドはexit 0だが、警告なしとは扱わない。

## 結果

`i386-elf-size` と `i386-elf-nm -n` で測定した。

| 項目 | 値 |
|---|---:|
| ELF text | 103206 B |
| ELF data | 49796 B |
| ELF bss | 17924 B |
| __bss_start | 0x3255e0 |
| __bss_end / _end | 0x329be4 |
| MEM_SHELL_GUARD | 0x375000 |
| _endからguardまでの静的差 | 308252 B |
| shell exec_heapの定義容量 | 524288 B |

帯域定義は `include/memmap.h:242-248`。リンカ配置は `sdk/link/app_sys.ld`。
308252 Bはnewlib sbrk等と競合する空間の上限差であり、端末専用の利用可能量ではない。
exec_heapは別帯域だが、524288 Bは総容量であり実行時の空きではない。

## T1案の算術評価 (採用未決)

| 項目 | 計算結果 |
|---|---:|
| stdin 4KiB + stdout/stderr各16KiB | 36864 B |
| 各8192セル × 8B × 2ストリーム | 131072 B |
| 上記合計 | 167936 B |

この合計には管理情報、アロケータヘッダ、追加コード、行索引、番兵等を含まない。
ホスト上の計算が帯域内でも、同時稼働時の確保成功は保証できない。

T4のCellはhost試験でsize 8B / align 4Bと確認（x86_64-unknown-linux-gnu）。
`userland/libos32term/tests/model.rs` の `cell_host_layout_measurement` が根拠。
上の8B仮定とは一致するが、guest実寸・Rust ABI保証・ゲスト確保成功を意味しない。

## カーネル帯の静的計上 (K6C、2026-09-12)

`kernel/con_sink.c` の console シンク。`kmalloc` せず **カーネル .bss の静的配列**なので、
シェル帯・アプリ帯・exec_heap のどれも減らさない (上の T1 案の算術とは別勘定)。

| 項目 | 値 | 出所 |
|---|---:|---|
| リング `g_ring[]` | 8192 B | `CON_SINK_RING_SIZE` (`include/con_sink.h`、票 §2-1 の決定) |
| 自己診断の作業域 `g_self_buf` + `g_self_src` | 403 B | `CON_SINK_REC_MAX` (203) + `CON_SINK_PRINT_MAX` (200) |
| head / tail / count / enabled / reader / drop_count | 24 B | `u32` × 4 + `int` × 2 |
| **合計** | **8619 B** | |

`i386-elf-gcc -O2 -c kernel/con_sink.c` の実測は text 2937 B / data 4 B / **bss 8672 B**
(整列込み。`con_sink_drop_count` は `.data`)。カーネル全体を
リンクしての実測とゲストの空き容量は未測定 (`make` は未実施)。端末アプリ側の受け皿
(K6C-A) はここには含まない。

## カーネル帯の静的計上 (K7-K、2026-09-12)

`kernel/kbd_inject.c` の注入リングと、`AppSlot` に増えた印。どちらも `kmalloc` せず
**カーネル .bss の静的領域**なので、シェル帯・アプリ帯・exec_heap のどれも減らさない。
測定は `i386-elf-gcc -O2 -c` + `i386-elf-size` / `i386-elf-nm -S` (カーネル全体のリンクと
ゲストの空き容量は未測定 — `make` は未実施)。

| 項目 | 値 | 出所 |
|---|---:|---|
| 注入リング `g_inj_ring[]` | 256 B | `KBD_INJECT_RING_SIZE` (`include/kbd_inject.h`、票 §1 メモリ) |
| head / tail / count / drop_count | 16 B | `u32` × 4 |
| 整列込みの実測 `kbd_inject.o` .bss | **288 B** | text 1489 B / data 0 B |
| 印 `parked_from_kbd` (AppSlot 1 本 4 B × 6 スロット) | 24 B | `g_slot` が 0x408 → 0x420 |
| カウンタ `ring3_kbd_park_count` | 0 B (実質) | 既存の整列の隙間に入り、`appslot.o` の .bss は 1064 → 1088 B = **+24 B** |
| **合計** | **312 B** | 288 + 24 |

票 §1 の見積り (「注入リング 256B + AppSlot の印 1 語 × 5」= 276 B) との差は 36 B:
リングの head/tail/count/drop_count の 16 B (整列で +16 B) と、印がシェル帯を含む
6 スロット分 (ID 0 と 1 も表にある) であることによる 4 B。

`kbd_inject_selftest()` は受け皿の静的配列を持たない (`inj_push` を 1 バイトずつ呼ぶ) —
`con_sink_selftest` が 403 B の作業域を持つのと違い、256 B に収めた意味を消さないため。

## カーネル帯の静的計上 (T8-K、2026-09-12)

画面の所有者 (票 T8 D1) と、`AppSlot` に増えたヘッダ flags の欄 (D1a)。どれも `kmalloc` せず
**カーネルの .data / .bss の静的領域**なので、シェル帯・アプリ帯・exec_heap のどれも減らさない。
測定は `i386-elf-gcc -O2 -c` + `i386-elf-size` / `i386-elf-nm -S` を HEAD 版と並べたもの
(カーネル全体のリンクとゲストの空き容量は未測定 — `make` は未実施)。

| 項目 | 値 | 出所 |
|---|---:|---|
| 所有者 `g_gfx_owner` | 4 B (.data) | `exec/appslot.c`。初期値 `GFX_OWNER_WM` = 1 なので .bss ではなく .data |
| カウンタ `gfx_init_reject_count` | 4 B (.bss) | 同上。KAPI にはしない (kernel.map から読む) |
| 欄 `AppSlot.hdr_flags` (4 B × 6 スロット) | 24 B | `g_slot` が 0x420 → 0x438 |
| 整列で増えた分 | 28 B | `g_slot` の先頭が 0x20 → 0x40 へ寄った (`appslot.o` .bss 1088 → **1144** = +56 B) |
| **合計 (静的)** | **60 B** | .data +4 B / .bss +56 B |

`appslot.o` の実測は text 2617 → **3355 B** (+738 B: 判定 3 本 + 回収 + 自己診断
`appslot_gfx_owner_selftest`)、data 4 → 8 B、bss 1088 → 1144 B。
`gfx/gfx_core.c` 側は門 2 本と `gfx_screen_owner` (どれも 3 行以下) で、静的領域は増えない。
KAPI スロットが 1 本増えた分 `KernelAPI` 構造体が 4 B 伸びる (192 → 193 関数)。

票 §2 の見積り (「所有者 1 語 + カウンタ 1 語」= 8 B) との差 52 B は、宣言ビットを起動時に
控える欄 (`hdr_flags` 24 B) と整列 (28 B) — 決裁 D1a を入れた分。

## カーネル帯の静的計上 (T8-3 K、2026-09-12)

ポーリング型の協調 yield (票 T8 §7 D8)。増えたのは `AppSlot` の印 1 語と、間引きの控え
1 語、カウンタ 1 語だけ。どれも `kmalloc` せず **カーネル .bss の静的領域**なので、
シェル帯・アプリ帯・exec_heap のどれも減らさない。測定は `i386-elf-gcc -O2 -c` +
`i386-elf-size` / `i386-elf-nm -S` を `feat/gui` cfd2768 版と並べたもの
(カーネル全体のリンクとゲストの空き容量は未測定 — `make` は未実施)。

| 項目 | 値 | 出所 |
|---|---:|---|
| 印 `AppSlot.parked_from_poll` (4 B × 6 スロット) | 24 B | `g_slot` が 0x438 → **0x450** |
| 間引きの控え `g_poll_last_tick` | 0 B (実質) | `static u32`。`g_slot` 手前の整列の隙間に入る |
| カウンタ `ring3_poll_yield_count` | 0 B (実質) | 同上。KAPI にはしない (kernel.map から `emu_read_mem` で読む) |
| **合計 (静的)** | **24 B** | `appslot.o` の .bss 1144 → **1168 B** |

`appslot.o` の実測は text 3474 → **4131 B** (+657 B: `appslot_park_poll_check/commit` +
`appslot_poll_yield_reset` + `resume_check` / `kill_check` / `appslot_state` の枝 +
自己診断 2 項)、data 8 B で不変。`exec/exec.c` (`exec_park_poll` + `exec_resume` の poll 分岐) と
`drivers/kbd.c` (`kbd_trygetchar` / `kbd_trygetkey` / `kbd_has_key` の GUI 分岐) は
**静的領域を 1 バイトも増やさない** — cfd2768 版と並べた実測で
`exec.o` は text 11037 → 11325 B (+288)・data 12 B・**bss 8708 B で不変**、
`kbd.o` は text 2530 → 2650 B (+120)・data 0 B・**bss 208 B で不変**。

票 §7 の見積り (「印 1 語 + tick 1 語」) との差は、印がシェル帯と ID 0 を含む 6 スロット分
あることと、控え・カウンタが既存の整列の隙間に収まったこと。KAPI は 1 本も増えていない
(v48 のまま — `kbd_trygetchar` / `kbd_trygetkey` の中身だけが変わる)。

## カーネル帯の静的計上 (T9-K、2026-09-12)

`exec/launch.c` の起動要求表 (票 T9 D3) と、`AppSlot` に増えた印 `parked_from_yield` (D5)。
どれも `kmalloc` せず **カーネルの .data / .bss の静的領域**なので、シェル帯・アプリ帯・
exec_heap のどれも減らさない。測定は `i386-elf-gcc -O2 -c` + `i386-elf-size` / `i386-elf-nm -S` を
基準版 (feat/gui `d301324`) と並べたもの (カーネル全体のリンクとゲストの空き容量は未測定 —
`make` は未実施)。

| 項目 | 値 | 出所 |
|---|---:|---|
| 要求表 `g_req[]` | 1704 B | 1 本 284 B (`requester` / `child` / `phase` / `kind` / `token` / `rc` / `arg` の `int` 7 個 + `cmdline[256]`) × `APP_SLOT_COUNT` (6) |
| token の種 `g_next_token` | 4 B | `.data` (初期値 1) |
| 診断カウンタ `launch_req_count` / `launch_orphan_count` | 8 B | KAPI にはしない (kernel.map から `emu_read_mem`) |
| 整列込みの実測 `launch.o` | **.bss 1736 B / .data 4 B** | text 2900 B (KAPI 7 本 + 連鎖 + 回収通知 + 自己診断) |
| 印 `parked_from_yield` (AppSlot 1 本 4 B × 6 スロット) | 24 B | `appslot.o` の .bss 1168 → **1192 B** |
| カウンタ `ring3_yield_count` | 0 B (実質) | 既存の整列の隙間に入る |
| **合計 (静的)** | **1764 B** | 1736 + 4 + 24 |

票 §1 メモリの見積り (「要求表 4 本 ≈ 1.1KB + 印 1 語 × 5」) との差は 2 つ:

- 表は使う 4 本 (ID 2〜5) で 1136 B = 見積りどおり。実測の 1704 B との差 568 B は、
  添字を **ID そのもの**にするために ID 0 と 1 の枠も持っているため (`parked_from_kbd` が
  6 スロット分あるのと同じ理由)。ずらすと「表 = ID」が読めなくなり、回収通知と連鎖の
  照合が 1 段増える。
- `token` / `rc` / `arg` の 3 欄が見積りに無かった (12 B × 6)。`token` は §9 blocker 4 の
  32bit 化、`rc` は FAILED の値、`arg` は KILL の宛先で、どれも落とせない。

text の増分は `launch.o` 2900 B + `appslot.o` 4131 → **4579 B** (+448: `park_yield_check/commit` +
`resume_source` + `resume_check` の枝 + 自己診断 1 項) + `exec.o` 11325 → **11765 B**
(+440: `exec_sys_yield` + `exec_kill` の連鎖 + `exec_resume` の印分岐 + 回収通知 1 行)。
`exec.o` の **.bss は 8708 B で不変**、`.data` も 12 B で不変。
KAPI は 8 本増えて v49 (`KernelAPI` 構造体が 32 B 伸びる — 関数ポインタ 8 個)。

## カーネル帯の静的計上 (T9 §12 T1、2026-09-13)

標準 FD のリダイレクト表 (`fs/fd_redirect.c`) を **アプリ ID ごとの文脈** にしたぶん。
枠は `exec/appslot.c` の `static FdRedirectState g_redir[APP_SLOT_COUNT]` (カーネル .bss、
`kmalloc` しない)。測定は `i386-elf-gcc -O2 -c` + `i386-elf-size` / `i386-elf-nm -S` を
基準版 (feat/gui `9f1d77a`) と並べたもの (カーネル全体のリンクとゲストの空き容量は未測定 —
`make` は未実施)。

| 項目 | 値 | 出所 |
|---|---:|---|
| `FdRedirect` 1 本 | 28 B | `target_type` / `file_fd` / `owner` の `int` 3 個 + `buffer` ポインタ + `buf_capacity` / `buf_pos` / `buf_len` の `u32` 3 個 |
| `FdRedirectState` (FD 0/1/2) | 84 B | 28 × `FD_REDIRECT_SLOTS` (3) |
| 枠 `g_redir[]` | **504 B** | 84 × `APP_SLOT_COUNT` (6)。`i386-elf-nm -S` の実測 `0x1F8` |
| 整列込みの実測 `appslot.o` の .bss | 1192 → **1720 B** (+528) | 504 + `g_slot` の後ろの詰め物 24 B |
| `fd_redirect.o` | .bss 116 B で**不変** | text 1289 → 1676 B (+387: save/restore/clear/close/state_active の 5 本) |
| **合計 (静的)** | **528 B** | すべて `appslot.o` の .bss |

`appslot.o` の text は 4579 → **4967 B** (+388: 持ち替え 2 本 + park 4 か所と resume / init /
shell_commit / reclaim の呼び出し)。切替のコストは構造体コピー 84 B × 2 (退避 + 復元) で、
park / resume ごとに 1 回。`exec.o` は 1 行も変わらない (呼ぶのは `appslot.c` の中だけ)。

## カーネル帯の静的計上 (T9 §12 S6、2026-09-13)

GUI 中の CTRL+STOP を WM に任せる判定 (票 §12 S6) で `AppSlot` に増えた欄
`last_kernel_tick` (`u32`)。`kmalloc` せずカーネル .bss の静的領域で、シェル帯・アプリ帯・
exec_heap のどれも減らさない。

測定は `i386-elf-gcc -O2 -c` + `i386-elf-size` / `i386-elf-nm -S` を基準版
(feat/gui `67b512e`) と並べたもの (カーネル全体のリンクは未測定 — `make` は未実施)。

| 項目 | 値 | 出所 |
|---|---:|---|
| `last_kernel_tick` (AppSlot 1 本 4 B × 6 スロット) | 24 B | `g_slot` が `0x468` → **`0x480`** |
| `appslot.o` の .bss 合計 | **1720 B で不変** | 増えた 24 B は `g_slot` と `g_redir` の間の詰め物に収まった (`g_redir` は `0x4C0` のまま) |
| 判定 `appslot_abort_admit` / `appslot_mark_scheduled` / 自己診断 | 0 B (静的領域なし) | text 4967 → **5087 B** (+120) |

`exec.c` は静的領域を 1 バイトも増やさない (`ring3_abort_request` の 1 行と
`appslot_mark_scheduled` の呼び出し 2 か所)。KAPI も増えていない (v49 のまま)。

## S0-K (KAPI v50、2026-09-13)

`i386-elf-gcc -O2 -c` + `i386-elf-size` を基準版 (feat/gui `4c7d9f9`) と並べたもの
(カーネル全体のリンクは未測定 — `make` は未実施)。

> **注記 (実装レビュー 往復 2 / 3、2026-09-13)**: 下の数値は往復 1 の修正までのもの。
> **修正版の実数は K1 の clean build 後に再測する** (コーダーは `make` を行えないので
> ここでは測れない)。往復 2 以降に増えた静的領域は次の 2 本だけで、どちらも
> カーネル .bss。`kmalloc` / exec_heap / アプリ帯は 1 バイトも減らさない:
>
> | 増えたもの | 大きさ | 何 |
> |---|---:|---|
> | `abs_path_buf` (`kapi/kapi_db.c`) | 256 B | 解決後の絶対名。stat / journal 名 / open をこれで行う |
> | `probe` (`db_v50_selftest` 内の static) | 264 B | ブート時の自己診断が journal 名の容量境界を踏むための作業域 |
>
> 逆に往復 2 で自前の末尾判定 (`sql_is_space` / `sql_tail_is_blank`) が消え、
> `journal_buf` は `VFS_MAX_PATH` (256B) のままなので text は増減する。

| 目的語 | text | .bss | 内訳 |
|---|---:|---:|---|
| `kapi_db.o` | 3309 → **7927** (+4618) | 3520 → **8256** (+4736) | 検証済みコピー先の静的スクラッチ (blob 4096 + text 256 + journal 256) と `DbSlot` の 3 欄 × 8 + owner 別 open 失敗欄 6 |
| `exec.o` | 12321 → **12749** (+428) | 8708 → **8740** (+32) | `ring3_user_range_ok` (帯だけを見る形) と、実機 K2 の切り分け計器 `ring3_range_reject_*` 5 本 (.bss +32B)。回収順の入れ替えは 0 B。計器を外せば +150B 程度まで戻る |
| `paging.o` | 5328 → **5328** (増減なし) | 49215 で不変 | 2026-09-13 に PTE 検査そのものを落としたので、v50 のための追加関数は残っていない (`paging_addrspace_pte_flags` → `paging_current_pte_flags` → 削除) |

合計 +4898 B text / +4736 B .bss (≈ 9.4KB、すべてカーネル帯の静的領域)。
実装レビュー 往復 1 の修正ぶんは text +280 B / .bss **-32 B** (journal スクラッチを
下位層の容量 `VFS_MAX_PATH` = 256B に合わせたので 8 B 縮み、整列で 32 B)。
`kmalloc` も exec_heap もアプリ帯も 1 バイトも減らさない。SHM の 16KB
結果ブロックのレイアウトは不変 (境界検査を足しただけ)。

## N4a (Host Services 基盤、v1.4、2026-09-14)

`libos32gui.shlib` に Host Services の窓口 (`os32gui_host_*` 6 本) と、その実体
`libos32host` を積んだ。`libos32host.c` の `g_stream_buf`
(`HOST_STREAM_BUF` = **16384 B**、`userland/lib/host/libos32host.h:59` /
`libos32host.c:339`、`print_stream` の詰めバッファ) が shlib の per-app
`.data`/`.bss` 帯に入る。

shlib の `.data`/`.bss` は **アプリごとに別の物理ページ**に複製される
(`sdk/link/shlib.ld:9`、ページ数は `__shlib_data_pages =
(__shlib_data_end − __shlib_data_start) / 4096`、同 :65)。この 16KB ぶんで
per-app の data ページが **4 → 8 ページ** に増える (+16KB / GUI アプリ)。

| 項目 | 値 | 出所 |
|---|---:|---|
| `g_stream_buf` | 16384 B (= 4 ページ) | `HOST_STREAM_BUF` (`libos32host.h:59`) |
| per-app shlib data ページ | 4 → **8** ページ | `__shlib_data_pages` (`sdk/link/shlib.ld:65`)、`shlib_data_pages()` (`kernel/shlib.c:200`) |
| GUI アプリ 1 本あたりの増分 | **+16KB** | 上記は PD ごとに複製される (`shlib.ld:9`、`shlib_addrspace_detach` :255) |

text 側の窓口 (`os32gui_host_*` と検査・写し) は全 PD で共有する 1 枚の
`.text` に入るのでアプリ本数に比例しない。カーネル帯・exec_heap・アプリ帯
(0x500000〜) は 1 バイトも減らさない。数値は静的な帯定義とヘッダ定数からの
算術で、カーネル全体のリンクとゲストの空き容量は未測定 (`make` は未実施)。

## PM判断

- pipe案は使用時にkernel kmallocを消費する (`fs/pipe_buffer.c:30-46`) ため、無償の予約領域として採らない。
- 初期候補は親exec_heapから実行前に確保し、子実行中には確保も解放もしない方式。
  ただし動的空き・cleanup・失敗経路を検証するまで確定しない。
- セルモデルの単独ホスト試験は実行統合と分離する。モデルの必要容量を計算可能にし、
  提供容量不足をエラーで返す設計なら、ゲストの容量決定前に境界条件を検証できる。
- 起動前のバッファ確保が途中で失敗した場合は全体を巻き戻し、子を起動しない。
- ゲストで親ヒープ空き・ピーク・連続実行後の回収を測る試験は未実施。
