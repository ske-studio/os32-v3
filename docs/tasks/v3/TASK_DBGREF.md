# DBGREF — デバッグ環境の整理「対象を絞った 1 段目」: 起動時に読み込むデバッグ部品 (`/boot/kdebug.bin`) 設計票 r4a (確定版)

> 状態: **受入完了 (2026-10-16)** — S0〜S3・G1〜G3 と実機受入 (4) は合格。証拠: `~/os32-tmp/evidence/2026-10-15/dbgref_g1/`、`dbgref_g2/RESULT.md`、`~/os32-tmp/evidence/2026-10-16/dbgref_s3_realhw/` (86ba40c)。採取途中の CTRL+STOP は [持越し台帳 E10-4](../DEFERRED_TESTS.md) の T7 残件。設計・レビュー記録は `~/os32-tmp/run/dbgref/`。

書き手: Fable 5.1 (`claude-fable-5-1`)、2026-10-14。基点: main `78207df`。数字は `build/out/kernel.map`・`.o` (`nm -S` / `nm -u` / `readelf -S`)・`ls -l`・ヘッダの `extern` 宣言から取った。
r4a = r4 + astra r4 の確認 (判定「依頼パックで直せば実装に出してよい」、P1 なし、P2 ①〜⑥) の反映 (`fable_r4_response.md`)。ユーザーの決定 (2026-10-14、推奨どおり): Q1 製品の既定は `KDEBUG=0`、受入と採取は「対の部品が RUNNING」を前提、1 段目は「対象を絞った 1 段目」と記録し全面達成とは書かない / Q2 L-B は S1+S2 後の実測で残り ≥ 19,456 B なら着手、SHM 削りは予備 / Q3 部品の形式は今回 T4 と同じ形式に確定。**この版は本文を全部持つ。前の版を参照しない。これ以上の往復はしない。**
前提: OS32 はシングルタスク・協調切替。凡例: [確] 現物で確認、[推] 推測、[未] 実機かビルドで確かめる。

> 入口の要約: ユーザーの要望「デバッグモードで起動しなければ、デバッグ用コードがメモリに乗らない」に対する**対象を絞った 1 段目**。カーネルは 1 種類、**kselftest の自己試験 (本体のファイルにある分)・`v86 -g` の記録器・crt_diag** を部品 `/boot/kdebug.bin` に移し、`/etc/system.cfg` の `KDEBUG=1` のときだけ root マウント後に読み込む。対は `vmkernel.lz4` の Image CRC で照合、状態は MemStat の末尾拡張 (slot を増やさない) で `ver` から読める。OFF でも lifecycle の監査は同じ回数走る。像の予算 (残り 7,424 B) から消える計画値 **約 −35.0 KB** (段の閾値は [§6](#6-2-段ごとの計画値と受入の閾値-b) を参照)、OFF で常駐から消えるのは移した 3 対象の **38,067 B**。本体に残る診断関連 (loader・監査・他ファイルの試験本体・bootlog・計装・例外ダンプ) は**約 43 KB** あり、後の段の対象 (§0-3・§6-4)。

---

## 0. 背景、達成範囲、元の要望への答え

### 0-1. ユーザーの意図と PM の解釈 (2026-10-14)

- ユーザー: 「オンオフは起動時にです。デバッグモードで立ち上げなければメモリにデバッグ用のコードが乗らないようにしたほうがいいのでは」「読み書きできるまでメモリ上に起動ログが残るのは仕方ありません」。
- PM: カーネル本体は 1 種類。デバッグ用コードは別ファイルの部品にまとめ、起動の設定でデバッグモードが選ばれたときだけ読み込み、カーネルの決まった口 (関数の表、オフのときは空) に登録する。デバッグ版/リリース版に分けない ([V1] と同種の危険)。1 段目はこれだけ。

### 0-2. 予備案の数字で成立しなかった点 (記録)

[確] Rust lz4 の compiler_builtins は像に 1,133 B しか残らず 392,551 B は `--gc-sections` で Discarded (100 KB は不成立)。`#if DEBUG` 系 646 行は `lib/sqlite3` の `SQLITE_DEBUG` 等で NDEBUG では 0 B。削るものは無い。試験専用像 (`kernel-r1`、`kernel-lgy98-link`) は既にあり製品像に 0 B。

### 0-3. 元の要望への答え — 1 段目で満たす範囲と、残るもの (全面達成とは記録しない)

**満たす範囲**: 部品に移す 3 対象 — kselftest の自己試験 (`kernel/kselftest.c` にある 30 試験と名前と報告、29,890 B)、`v86 -g` の記録器と自己試験ゲスト (6,976 B)、crt_diag (1,201 B) — は `KDEBUG=0` では**ファイルを開かず**メモリに乗らない (合計 **38,067 B**)。
**本体に残るもの (OFF でも常駐、約 43 KB)**: (a) 部品の仕組み — loader/validator・import 表とアクセサ・結線・record・状態 (約 2.4 KB、§6)。(b) lifecycle の監査 — `kaudit` 0.6 KB と実体 (`paging_master_audit` 917 / `lease_audit_all` 432 / `ledger_selfcheck` 748) 2.1 KB。(c) **kselftest から呼ぶが他ファイルにある自己試験の本体 約 12.5 KB** — `lease_selftest` 2,152、`con_sink_selftest` 1,689、`kbd_inject_selftest` 1,658、`exec_tramp_user_selftest` 1,336、`appslot_resume_mark_selftest` 1,182、`launch_selftest` 788、`paging_memmap_selftest` 688、`paging_map_user_keep_selftest` 526、`appslot_gfx_owner_selftest` 495、`exec_kapi_layout_selftest` 465、`gfx_selected_selfcheck` 380、`paging_app_band_selftest` 369、`appslot_abort_admit_selftest` 363、`paging_pd_clone_selftest` 330、`selftest_as_begin` 83 [確、`nm -S`] — paging.c / lease.c / con_sink.c 等の static な内部を触るので、この段では動かさない。(d) bootlog 16.7 KB (FS が使えるまでの記録、ユーザーの決定で本体)。(e) R7-1 の計装 約 2.5 KB、例外ダンプ 2.9 KB、`net/link.c` の自己試験 2.8 KB、利用者向けの案内文。
**オンオフの形**: 起動時の `KDEBUG=` だけ。ユーザーランドの診断道具は従来どおり DEBUG 集合 (`build/packages.yaml`)。実行時に読み込み・取り外しする本物のモジュールは T4 の後で、この部品は T4 と**同じ形式** (§3-2) の最初の実例。

---

## 1. 棚卸し (1 段目の対象と、本体に残るものの境界)

### 1-1. 像の大きさの基準 [確]

| 項目 | 値 | 出典 |
|---|---:|---|
| `kernel.bin` のファイル | **400,332 B** (節の合計 `.text` 365,870 + `.data` 34,431 = 400,301、差は `.got.plt` 12 B と整列) | `ls -l`、map |
| `.bss` | 218,912 B | map |
| `__bss_end` | 0x197300 = 像 **619,264 B**。`MEM_KERNEL_IMAGE_MAX` 0x99000 = 626,688 B → **残り 7,424 B** | map:4293、`memmap.h:259` |
| `vmkernel.lz4` | 505,298 / 520,192 B | `ls -l`、`boot_defs.h:93` |

### 1-2. 部品へ移すもの — 大きさ、外部参照の実数、外から読まれる記号

| ファイル | text / 文字列 (併合前) / bss (B) | 外部参照 (`nm -u`、定義側の型で分類) | 外から読まれる記号 |
|---|---:|---|---|
| `kernel/kselftest.c` の自己試験 (`kselftest_run` 12,482、`_post_exec` 2,075、`_post_unicode` 113、`_post_probe` 88、30 の `test_*`、`check` 45、`ksel_*` 補助) | 19,358 − 本体に残る監査 635 (`kselftest_run_audit` 262 + `.part.0` 219 + `audit_v86_return` 66 + `_post_probe` の監査呼出し 88) − 報告 約 150 ≈ **18,570** / 9,196 − 約 60 ≈ **9,130** / 2,368 − 48 (計数) = **2,320** → 合計 29,890 | **関数 135、データ 41** (§3-6) | `kselftest_pass/fail`、`memmap_audit_*`、`as_audit_*`、`audit_runs/fail`、`v86_return_audit_*`、`audit_first_fail_tag` — 全部**本体に残す** |
| `kernel/v86_gcap.c` + `v86_gcap_math.c` (+ `v86_test16_gcap.asm` の写しの配列、`v86g_phase_diag` 364 を含む) | 4,966 + 1,410 = **6,376** / **572** / 40 − 12 = 28 → 合計 6,976 | 外部参照 22 のうち **`v86g_*` 14 本は math と一緒に部品内**。本体への参照は関数 17 + データ 5 で、kselftest と共有 5 (`kctx_irq_depth`、`kctx_exc_depth`、`kmemcpy`、`serial_gate_active`、`serial_puts_polled`) → **gcap だけの新規: 関数 14、データ 3** | `v86_gcap_alloc_count` / `v86_gcap_free_count` (pt0_snapshot の必須記号 → **本体**)、`v86_gcap_phase` (本体)、`v86_gcap_rec` (v86_mem.c が `= 0` に書く → 口 `gcap_detach`) |
| `kernel/crt_diag.c` | **1,039** / **162** / 0 → 合計 1,201 | 関数 1 (`kprintf`、kselftest と共有) | `crt_diag_read` / `_format` / `_bootlog` (ホスト試験が `#include` で使う) |
| 合計 | **38,067** | 関数 149 (135 + 14)、データ 44 (41 + 3) → アクセサ 56 + 新しい口 6 → **import 211** (§3-6) | |

### 1-3. 本体の結線 (口の表経由に変わる点) [確]

`kernel.c:598/665/688/752/754` (kselftest ×4、crt_diag ×1)、`v86.c:252/454/464` (`insn_abort`、`keep_if` ×2)、`v86_io.c:357/387/414/432/444/454/464` (`active` ×2、`v86g_port_passed`、`pass_in`、`note_emul_in`、`pass_out`、`note_emul_out`)、`v86_mem.c:188/190/205` (`v86_gcap_rec = 0` ×2、`v86_gcap_release`)、`kapi_generated.c:2298` (`wrap_v86_gdc_capture` の実体)。

### 1-4. 既存の利用者 (変えると壊れるもの) — 3 列で

| 読む側 | 何に依存するか | 種類 |
|---|---|---|
| `tools/accept/pt0_snapshot.py:22-28` | 記号 `kselftest_pass/fail`、`audit_*` 11 本、`v86_gcap_alloc_count/free_count`、`exec_stop_count` 等が `kernel.map` にあり型 u32 | 記号 (全部本体に残す) |
| `guest_acceptance.yaml` | `kselftest_fail` 28 か所、`kselftest_pass` 7 (絶対値 289 :701、283 :847)、`minimum_added_boot_checks: 4`、`minimum_boot_audit_runs: 2`、`audit_runs` 29、`audit_fail` 25、:431 (`v86_gcap_rec`)、:1161 (`mem_stat_test` の prereq v72) | 値・手動 observe (前提 = 部品が RUNNING) |
| `tools/emu_agent/tasks/regress.txt:4` | selftest の 2 つの数字の転記 | 採取 (`kdebug_state` を足す) |
| HANDOVER §1 (295/0)、POLICY_DEBUG §2、skill `os32-build-verify:85` | kselftest の値を到達確認に使う運用 | 運用 (書き換え) |
| `userland/tests/mem_stat_test.c:20,27,44` | `sizeof(MemStat) == 200`、`kselftest_pass >= 295`、過大サイズで 200 を返す | **ゲスト試験 (2 本に分ける、§2-3、段階別の期待)** |
| `tools/tests/test_e10c_lifecycle.py:9-46` | `PATHS` に `kernel/v86_gcap.c` と `kernel/kselftest.c`、関数本体の切り出し、変異 `gcap-*` の置換文字列、直接呼出しの検査 (`kselftest_run_post_probe();` が `gfx_prepare_backend();` の後、`check(kselftest_run_audit("post-exec") == 0,` が `kselftest_run_post_exec` の本体) | ソース抽出・直接呼出し文字列 |
| `tools/tests/test_memmap_audit.py:44-75`、`memmap_audit_host.c:295-305` | `/* Lifecycle diagnostics,` 以降の切り出し、変異 5 本の置換文字列、tag の文字 | ソース抽出 (→ `kernel/kaudit.c` に同じ文字列で) |
| `v86_gcap_host.c:353,361`、`e10c_gcap_host.c`、`crt_diag_host.c:149` | `#include` のパス、`[gcap] ph=…` / `[crt] …` の完全一致 | 出力の照合 (文字列は不変) |
| `r1_recovery_host.c:123`、`shm_user_host.c:56`、`surface_bundle_host.c:42` | `V86Gcap *v86_gcap_rec;` の偽物 | 偽物の記号 (→ record の偽物) |
| `check_map.d` の 12 本 | `kernel/kselftest.c` 等のパス | 対応表 |
| `cmd_base.c:60-95` (`ver`)、`cmd_sys.c:49-130` (`mem`: owner_pages は選択アプリだけ、system は 0)、`userland/cmds/v86.c` | KAPI の版と MemStat の大きさ | 利用者 (`ver` に 1 行、`v86 -g` は OFF で NOSYS) |
| 配備: `userland/deploy.yaml`、`build/packages.yaml:114`、`deploy-set.json`、`nhd_deploy.py`、`hsync`、`build/app.conf:244` | `/boot` の集合、要求版 | 配備 ([V2]、対で配る) |

---

## 2. 分類と移行表

### 2-1. 行き先

| 項目 | 行き先 | 理由 |
|---|---|---|
| kselftest の 30 試験・`check()`・名前・情報行・報告 | **部品** (`kdebug/kselftest.c`) | CPL0 のまま部品で走る。名前は部品の中。計数は本体のアクセサ経由 |
| 監査と計数、`v86_gcap_alloc/free_count`、`v86_gcap_phase` | **本体** (`kernel/kaudit.c`) | OFF でも同じ回数走る (§3-8)。外部の道具が記号を読む |
| v86_gcap / math / 自己試験ゲストの写し / `v86g_phase_diag` | **部品** (`kdebug/v86_gcap.c`、`v86_gcap_math.c`) | KAPI 239 の実体は部品。`[gcap] ph=…` の文字列は不変 |
| crt_diag | **部品** (`kdebug/crt_diag.c`) | 文字列は不変。OFF では `[crt]` 行が出ない (CRTFREQ の採取は RUNNING で) |
| 他ファイルの自己試験の本体 (§0-3 (c))、`v86_smoke_test` / `v86_disk_test` | **後の段の候補** (§9) | static な内部に触る / 大きさ未測定 |

### 2-2. 本体の結線と OFF の意味

| 点 | 1 段目の後 | OFF のとき |
|---|---|---|
| `kernel.c:598` | `if (kdebug.ops.selftest_run) ops.selftest_run(); else kprintf("[selftest] skipped (KDEBUG=0)\n")` (**S2 で**。S1 では今の `kselftest_run()` のまま) | 1 行、計数 0/0 (S2 以降) |
| `kernel.c:665` | `kaudit_post_exec();` (本体、`paging_v86_session_open()` なら監査だけして**印を残す**) + `if (ops.selftest_post_exec) …` (S2) | 監査は走る |
| `kernel.c:688` / `:752` / `:754` | `if (ops.selftest_post_unicode) …` (S2) / `if (ops.crt_diag_bootlog) …` (S1a) / `kaudit_post_probe();` + `if (ops.selftest_post_probe) …` (S2) | 監査は走る、試験と `[crt]` は無い |
| `v86.c:252` / `:454,464` | `ops.gcap_insn_abort ? … : 0` / `ops.gcap_keep_if ? … : 0` (S3) | 0 (今の非採取と同じ) |
| `v86_io.c:357,414` / `:387` / `:432,454` / `:444,464` | `ops.gcap_active ? … : 0` / `ops.gcap_port_passed(p)` (`capture` が真の中だけ) / `ops.gcap_pass_in/out ? … : 0` / `if (ops.gcap_note_in/out) …` (S3) | 0 / 到達しない / 0 / nop |
| `v86_mem.c:188,190` | `if (ops.gcap_detach) ops.gcap_detach()` — 部品の static を 0 にするだけ (割当・解放・VFS なし、IRQ 停止中・例外スタックで可) (S3) | nop |
| `v86_mem.c:205` | `if (ops.gcap_release) …` (通常文脈だけ、`kctx_*depth` が 0) (S3) | nop |
| `kapi_generated.c:2298` | 本体の `v86_gdc_capture` は `ops.gcap_capture ? … : OS32_ERR_NOSYS` (slot 239、ABI 不変) (S3) | `v86 -g` が NOSYS |

### 2-3. 移行表 (依存するものごとに、何を・どう書き換えるか)

| 依存するもの | 段 | 書き換え |
|---|---|---|
| `mem_stat_test` | S1b → S2 | **2 本に分ける**: (1) `userland/tests/mem_stat_compat_test.c` — 旧 200 B の prefix の互換を固定: `size` の列は 0 / `MEMSTAT_MIN−1` / `MEMSTAT_MIN` / 120 / 132 / **200** (`sizeof` を使わず定数 200)、`rc == 200`、`owner_pages` の意味 (選択アプリ > 0、system 0) 不変、kselftest の値は**見ない**。`app.conf` の要求版は 72 のまま。(2) `userland/tests/mem_stat_test.c` v75 — `sizeof(MemStat) == 220`、過大サイズ (236) は **220 を返す**、`kdebug_owner_pages` は `kdebug_state == 1` で > 0、それ以外 0。**kselftest の期待は段階別**: **S1b では** `kselftest_fail == 0 && kselftest_pass >= 295` を **`kdebug_state` に関係なく**期待 (自己試験はまだ本体で走る)。**S2 で**この 1 行を `kdebug_state == 1 ? (pass >= 295 && fail == 0) : (pass == 0 && fail == 0)`、`kdebug_state >= 2` は FAIL、に書き換える (依頼パックに新旧を列挙)。`app.conf` の要求版 75。`guest_tests.txt` に 2 本、`deploy.yaml` (DEBUG 集合)、yaml:1161 の prereq を「対の部品が RUNNING」に |
| `pt0_snapshot.py` の必須記号 | S2/S3 | **不変** (計数は本体) |
| `test_e10c_lifecycle.py` | S2: 直接呼出しの検査を **`kaudit_post_probe();` が `gfx_prepare_backend();` の後に kernel.c にあること** と **`check(kdi_kaudit_last_rc(KAUDIT_POST_EXEC) == 0,` が `kdebug/kselftest.c` の `kselftest_run_post_exec` 本体にあること** に (変異 `post-exec-body` の置換文字列も同じ)。S3: `PATHS[0]` を `kdebug/v86_gcap.c` に、`gcap-*` の置換文字列は不変 | 新旧の文字列を依頼パックに列挙 |
| `test_memmap_audit.py` の切り出しと変異 5 本、`memmap_audit_host.c` | S2 | 切り出しの元を `kernel/kaudit.c` に (印 `/* Lifecycle diagnostics,` と置換文字列 4 本は無変更)。`lifetime-boot-count` の `ksel_pass++` は `kselftest_count(1)` (本体の関数) に |
| `v86_gcap_host.c` / `e10c_gcap_host.c` / `crt_diag_host.c` | S1a (crt) / S3 (gcap) | `#include` のパスだけ。期待文字列は不変。ハーネスに record の偽物 `struct kdebug kdebug;` を 1 行 |
| `r1_recovery_host.c` / `shm_user_host.c` / `surface_bundle_host.c` の `V86Gcap *v86_gcap_rec;` | S3 | record の偽物に置き換え (口が NULL = OFF の経路) |
| `guest_acceptance.yaml` | S2 | 全 entry の prereq に「`kdebug_state == 1` かつ `kdebug_pair_crc == ver の Image CRC`」。:701・:847 の絶対値は「当時の値」と注記。:431 の `v86_gcap_rec` は「`gcap_detach` の後に `v86 -g` が再採取できること」に |
| `regress.txt:4` | S2 | 「`kselftest_pass/fail` と **`kdebug_state`** を転記 (1 でなければ未実行と記録)」 |
| POLICY_DEBUG §2、HANDOVER §1、skill | S1b/S2 | 到達確認は「`KDEBUG=1` で起動し `kdebug_state == 1` と `kdebug_pair_crc == Image CRC` を確かめた上での kselftest の値」。拒否 (≥ 2) は OFF と同一視せず理由コードを記録。OFF の到達確認は `ver` の Image CRC と `boot.log` |
| `check_map.d` の 12 本 | S1〜S3 | パスの付け替え |
| `ver` (`cmd_base.c`) | S1b | `g_api->version >= 75` なら `mem_stat(0, &st, sizeof st)` で `kdebug: off` / `kdebug: on (pair <crc>, body N pages, owner M pages)` / `kdebug: rejected (<code> <名前>, file pair <crc>)` を Image CRC の行の次に |
| 配備 | S1b / G1 | `/boot/kdebug.bin` を boot の集合に ([V2])、期待表に両方、`deploy-kernel` と `hsync boot` は対で配る。`kdebug_probe.bin` と 2 本の mem_stat 試験は DEBUG 集合 |
| 08_build §8-3 / §8-4、DEVELOPMENT §2、KAPI_SPEC §3-2 (v75 = MemStat 220 B、slot なし)、T4 票 §2-1 (§3-2 のスキーマを正典として参照) | S1b | 文書 (PM) |

---

## 3. 部品の設計

### 3-1. 読み込みの時期と方法 — root マウント後、本体が読む (T4 の POST_ROOT)

- 起動順 [確]: root の `vfs_mount` :455 → `paging_init` :526 → `memory_boot_init` :577 → `dma_pool_init` :590 → 自己試験 :598。読み込み点は **`dma_pool_init` の直後**: `sysconfig_get_int(SYS_SYSTEM_CFG, "KDEBUG", 0)` が 1 のときだけ `vfs_open("/boot/kdebug.bin")` → 検証 → 読み込み → 登録。0 なら**開かない**。`kdebug.open_count` を open の成否に関係なく +1。
- 失敗は起動を止めない: `kdebug.state` に理由コード、画面に `[kdebug] rejected: <名前>` 1 行。

### 3-2. 部品の形式 — T4 と同じ形式として 1 つのスキーマに確定 (Q3)

LB-1: ops は版 2、18 口。版 1 の部品は init 前に ABI_MISMATCH で拒否する。

**容器 (container、ファイル = 読み込んだ像)**: `header (144 B) → reloc 表 → import 表 → export 表 → 名前表 → (ページ整列) → payload (text / rodata / data) → bss (ファイルには無い)`。
**基点**: `reloc_offset` / `import_offset` / `export_offset` / `names_offset` / `payload_offset` は **container の先頭 (バイト 0) 基点**。節 (`text/ro/data/bss` の offset)、reloc の書込位置、import slot、`init_offset`、export の `entry_off` は **body 基点** (`body_base = container_base + payload_offset`、body は VA 0 でリンクした像)。
**固定の読み込み長**: 本体は最初に **144 B** を固定バッファへ読み、ヘッダの全欄を検査してから `load_span` を確保する。

| 欄 (u32 LE、添字 = バイト ÷ 4) | 意味 | 検査 |
|---|---|---|
| 0 `magic` | `'KMOD'` | 一致 |
| 1 `format_version` | 1 | 未来版は拒否 |
| 2 `header_size` | **144** | 一致 |
| 3 `image_size` | = `payload_offset + payload_size` (ファイル全長) | ファイルの実長と一致、≤ 262,144 |
| 4 `kapi_abi_gen` | `KAPI_VERSION` (75) | 一致 |
| 5 `layout_gen` | memory_layout (3) | 一致 |
| 6 `module_abi_gen` | `.def` の全文 (名前・型・順) の FNV-1a 32 bit | 本体の生成値と一致 |
| 7 `module_id` | 1 (KDEBUG) | 許可リスト |
| 8 `phase` | 2 (POST_ROOT) | 一致 |
| 9 `flags` | bit0 必須: `pair_image_crc` あり | 未知の必須 bit は拒否 |
| 10 `mem_size` | body の占有 = text + ro + data + bss、4096 の倍数 | `payload_size ≤ mem_size ≤ 262,144`、`load_span = payload_offset + mem_size` が overflow しない |
| 11 `payload_offset` | 4096 の倍数、**≥ 4096** | `header_size ≤ payload_offset ≤ image_size` |
| 12 `payload_size` | payload の長さ | `image_size − payload_offset` と一致 |
| 13〜20 `text_off/size`、`ro_off/size`、`data_off/size`、`bss_off/size` | body 基点 | 各 `off ≤ mem_size && size ≤ mem_size − off`、互いに非重複、text/ro/data は `payload_size` 内 (`off ≤ payload_size && size ≤ payload_size − off`)、bss は `bss_off ≥ payload_size` |
| 21〜22 `reloc_offset/count` | container 基点 / 件数 | 表は `[header_size, payload_offset)` 内: `144 ≤ reloc_offset ≤ payload_offset && count × 4 ≤ payload_offset − reloc_offset`、`count ≤ 16,384` |
| 23〜24 `import_offset/count` | 同 | 同じ形、`count × 12`、`count ≤ 1,024` |
| 25〜26 `export_offset/count` | 同 | 同じ形、`count × 12`、`count ≤ 64` |
| 27〜28 `names_offset/size` | 同 (NUL 区切り、各名 ≤ 63 B) | 同じ形 |
| 29 `init_offset` | body 基点、text 内 | `text_off ≤ init_offset && init_offset − text_off < text_size` |
| 30 `stop_offset` | 0 (無し) | 0 以外は拒否 |
| 31 `pair_image_crc` | 対の `vmkernel.lz4` の Image CRC (§3-3) | `bootinfo` と一致 |
| 32 `image_crc` (バイト 128〜131) | **容器 `[0, image_size)` 全体の CRC32 を、この欄を 0 として計算** | 一致 |
| 33 `ops_version` | export 表の契約版 (= `KDEBUG_OPS_VERSION` 2)。record の `ops.version` に写す。**関数の入口とは別の欄** | 一致 |
| 34〜35 予約 | 0 | 0 でなければ拒否 |

4 つの表は互いに非重複 (`[off, off+len)` を昇順に並べて隣接比較)。
**表のレコード**:
- reloc: `u32 off` (body 基点、`R_386_32` の書込位置)。**非整列を許す** (i386 の絶対即値は非整列の位置にある)。検査: `payload_size ≥ 4 && off ≤ payload_size − 4` (T4:44 の減算前提の形 `off ≤ limit && 4 ≤ limit − off`)、**4 バイト全体の区間 `[off, off+4)` が互いに重ならず** (昇順に並べて `off[i+1] ≥ off[i] + 4`)、import slot の区間とも重ならない、bss・prefix への書込みは範囲の検査で自動的に拒否。元の値 (リンク時の VA 0 基点) が `≤ mem_size` (one-past を許す)。実行時にも同じ範囲検査をしてから `+= body_base`。
- import: `{u32 name_hash; u16 type_ver; u16 kind; u32 slot_off}` 12 B。`kind` = 1 FUNC / 2 GETTER / 3 SETTER / 4 PTR / 5 OP (§3-6)。`slot_off` は body 基点で `.data` 内: `data_size ≥ 4 && slot_off ≥ data_off && slot_off − data_off ≤ data_size − 4 && slot_off % 4 == 0` (wrap しない形)、重複なし。解決した本体の関数の番地をその slot に書く。
- export: `{u32 name_hash; u16 slot; u16 ver; u32 entry_off}` 12 B。**関数の export は slot 1〜18** (`struct kdebug_ops` の添字。slot 0 は version の欄で export 表には置かず、0 があれば拒否)、`entry_off` は text 内、slot の重複・範囲外は拒否。
- 名前表: NUL 区切りの文字列、import/export の順に並ぶ (hash と突き合わせて不一致は拒否)。

**生成**: `tools/mkmod.py`。`build/kdebug.mk` が `kdebug/*.c` を本体と同じ `CFLAGS_BASE` で組み、`build/module.ld` で VA 0 に `ld --emit-relocs` → `R_386_32` だけを抜き (PC32 は内部で解決済み、それ以外の種別・未解決記号は生成失敗)、import slot は `.data` 先頭の `kdebug_import_slots[]` (生成した C の配列)、export は生成した `kdebug_ops` の初期化子から取る。世代記号 `__os32_abi_1_mem_3_fmt_4` は `module.ld` でも定義。`make kernel` の成果物に `build/out/kdebug.bin` (kernel-r1 は `build/out/r1/kdebug.bin`)。

### 3-3. 対の識別 — `vmkernel.lz4` の Image CRC

- 識別子 = VK32 ファイル全体の CRC32 (`mkvmkernel.py:184-188` が埋め、ローダが同じ計算で検査して bootinfo 0x30 に残し (`vk32_boot.c:68`、`boot_main.c:43`、FD は `loader_fat_new.asm:324`)、カーネルが `bootinfo_get()` で読み、`ver` が「Image CRC」として出す値 [確])。`mkmod.py` は組んだ `vmkernel.lz4` の欄を読んで `pair_image_crc` に埋める (kdebug.bin は vmkernel.lz4 に依存)。
- 照合 (本体、`bootinfo_get()` の実型): **`bi->img_valid && bi->img_crc == hdr->pair_image_crc`** でなければ拒否 (理由 4 `PAIR_MISMATCH`; `img_valid` が 0 なら 5 `NO_IMAGE_CRC`)。加えて `kapi_abi_gen` / `layout_gen` / `module_abi_gen` / `ops_version` を照合 (理由 6 `ABI_MISMATCH`)。
- 拒否試験: (a) 1 つ前のビルドの部品 → 4 (S1a)、(b) **生成後の Image CRC が異なることを確かめた本体** + 元の部品 → 4 (S1b)、(c) `image_crc` を書き換えた部品 → 9 `BAD_CRC` (S1a)、(d) `module_abi_gen` を書き換えた部品 (CRC は再計算) → 6 (S1a)。
- `os32_build_commit` (短いコミット ID + `-dirty`) は表示だけに使う。

### 3-4. 置き場と確保 — pgalloc の池、動的 MODULE owner、`load_span`

- 確保は **`load_span = payload_offset + mem_size`** のページ数を `ledger_owner_new(LEDGER_KIND_MODULE, 1, "kdebug", &owner)` の動的 owner に `pgalloc_alloc_phys(owner, n)` で連続確保。手順 (T4 の POST_ROOT): 144 B のヘッダを固定バッファへ読んで検査 → `load_span` を確保 → prefix + payload をその割当へ短読検査つきで読む → 容器全体の CRC・範囲・表を検証 → BSS ゼロ → reloc → import slot の解決 (§3-6) → export を**非公開の候補表 `ops_candidate`** に写す → `init` を通常文脈で呼ぶ → 戻り 0 なら候補表を record の `ops` に写し、prefix のページを返し、**最後に `state = RUNNING`** (公開)。定常 = body のページ、ピーク = load_span。アプリ PD は master の写しで池の supervisor 恒等写像を引き継ぐ (`paging.c:845` [確]) ので KAPI の文脈でも例外文脈でも呼べる。ポインタは `P2V(phys)` ([C5])。
- 保証 A (検証のどこかで失敗): **init を一度も呼ばず**、口は全部 NULL のまま、fd は close、確保した `load_span` を全部 `pgalloc_free_n_owner`、owner を `ledger_owner_retire`、`state` = 理由。
- 保証 B (init の戻り ≠ 0、理由 10 `INIT_FAILED`): 候補表を捨てて A と同じ回収。これが許されるのは **この部品の `init` が候補表を埋めて返すだけで副作用 (割当・IRQ/tick/DMA の登録・装置の操作・import の呼出し) を持たない**契約があるから (`check-kdebug-init-host` が固定)。T4 の一般規則とは別の、kdebug 固有の保証。
- OFF: 割当なし、ファイルを開かない → 0 B。

### 3-5. 口の表 (export) — 関数は slot 1〜18

| slot | 口 | 呼ぶ文脈 | OFF |
|---:|---|---|---|
| (0) | `u32 version` — ヘッダ欄 33 から record に写す (export 表には無い) | — | — |
| 1〜4 | `selftest_run` / `selftest_post_exec` / `selftest_post_unicode` / `selftest_post_probe` | 起動の通常文脈 | 走らない |
| 5 | `crt_diag_bootlog` | 起動の通常文脈 | `[crt]` 無し |
| 6 | `gcap_capture(mode, out)` | KAPI (アプリ PD、CPL0) | NOSYS |
| 7〜8 | `gcap_active()` / `gcap_keep_if()` | #GP (例外) / V86 入場 | 0 |
| 9〜10 | `gcap_insn_abort(opsize16, op)` / `gcap_port_passed(port)` | #GP (例外) | 0 / 到達しない |
| 11〜14 | `gcap_pass_in` / `gcap_pass_out` / `gcap_note_in` / `gcap_note_out` | #GP (例外) | 0 / nop |
| 15 | `gcap_detach()` — 記録参照を 0 に (割当・解放・VFS なし) | IRQ 停止中・例外スタック | nop |
| 16 | `gcap_release()` — `kfree` を含む回収 | 通常文脈 (`kctx_*depth` 0) | nop |

| 17 | `nic_selftest(void)` — LB-1 は NULL、LB-4 で結線 | 通常文脈のみ | nop |
| 18 | `nic_trace(u32 stage)` — lan.log の段の印 | probe の通常文脈のみ (IRQ/tick/irq_save 内は禁止) | nop |

LB-1 の trace は 256 B に seq/crc と段を積み、毎段の全文書き直しと sync を行う。
`void` ABI のため保存失敗は部品から本体の `kprintf` で `[e82557] trace=off` を一度出し、以後書かない (probe の最終行より前)。
bootlog は止めない。lan.log は boot.log の try seq/crc と一致するものだけを今回の証拠とする。

record (本体、bss、**単一の記号 `kdebug` が正典**): `struct kdebug { u32 state; u32 pair_crc; u32 base; u32 pages; u32 owner; u32 open_count; struct kdebug_ops ops; }` 100 B。独立の `kdebug_base` / `kdebug_pages` の記号は作らない。

### 3-6. import — `.def` の一覧 (型と順序)、データ参照の分類、段ごとの集合と費用

T4 どおりデータの import は禁止し、データ記号 44 本 (kselftest 41 + gcap 3、共有の `kctx_*` 2 は kselftest 側に数える) を用法で分ける [確、`extern` 宣言の型と `grep` の用法]。アクセサの名前は `kdi_get_<記号>` / `kdi_set_<記号>` / `kdi_ptr_<記号>`。

| 類 | 記号 (型) | アクセサ | 移す `.c` のソース変更 |
|---|---|---|---|
| **読むだけ 24** (getter 1 本) | `kctx_irq_depth` (volatile u32)、`kctx_exc_depth` (volatile u32)、`tick_count` (volatile u32)、`resident_heap_peak` (u32)、`ledger_bad_free`、`ledger_check_fail`、`ledger_exc_ops`、`ledger_irq_ops`、`ledger_region_count` (u32)、`paging_memmap_bad_count`、`paging_range_reject_count` (u32)、`ring3_range_reject_addr`、`_count`、`_last` (volatile u32)、`ring3_wm_depth_underflow` (volatile u32)、`cpu_calib_rounds`、`cpu_calib_ticks`、`ktime_clamp_count` (u32)、`irq_ctx_violations`、`irq_storm_masked`、`irq_line_quarantined` (u32)、`gui_ime_render_rejected` (volatile u32) — 以上 22 は kselftest; `v86_gp_cs`、`v86_gp_ip` (u32) は gcap | `T kdi_get_X(void)` × 24 | 無し (`#define X (kdi_get_X())`) |
| **書く 12** (getter + setter) | `ring3_in_syscall` (volatile int、代入 8 か所)、`ring3_wm_depth` (volatile int、7)、`appslot_trim_epoch` (u32、:732 の復元)、`appslot_trim_request_count`、`appslot_trim_mark_count` (:734)、`appslot_trim_done_count`、`appslot_trim_done_reject_count` (volatile u32、各 1)、`appslot_reclaim_count` (volatile u32、1)、`appslot_last_reclaim_id` (volatile int、1)、`irq_unexpected` (u32、:1519)、**`irq_test_quiet` (int、:1516,1518)**、**`time_test_feed_n` (volatile int、:1694,1780)** | `T kdi_get_X(void)` + `void kdi_set_X(T)` × 12 = 24 本 | 代入の行を `kdi_set_X(v)` に — **約 29 行** (読む箇所はマクロ) |
| **配列 7** (先頭ポインタ getter) | `ledger_surfaces` (struct ledger_surface[16]、ポインタ差・`&[i]`)、`ledger_regions` (struct ledger_region[48])、`ledger_resources` (struct ledger_resource[16])、`irq_lines` (struct irq_line[]、`[idx].count`)、`paging_memmap_bad` (u32[MM_BAD_MAX*3])、`time_branch_hits` (unsigned[TIME_BR_COUNT])、`time_test_feed` (struct time_test_feed[3]、要素への代入 :1689-1693,1778-1784 はポインタ経由で成立) | `T *kdi_ptr_X(void)` × 7 | 無し (`#define X (kdi_ptr_X())`) |
| **構造体実体 1** | `v86_session` (struct v86_session_state、`.aborting` 等のメンバ読み) | `struct v86_session_state *kdi_ptr_v86_session(void)` × 1 | 無し (**`#define v86_session (*kdi_ptr_v86_session())`** — 配列とは別の展開) |
| **新しい口 6** (本体の `kaudit.c` / `kselftest_report.c` の関数) | `ksel_pass/fail` の `++` (`check()` 2 行)、`= 0` (`kselftest_run` 2 行)、読み 20 か所 (:962,969,974,992-996,1560,1670,2085-2091,2126,2129)、監査の結果 | `void kdi_kselftest_count(int pass)`、`void kdi_kselftest_reset(void)`、`u32 kdi_get_kselftest_pass(void)`、`u32 kdi_get_kselftest_fail(void)`、`int kdi_kaudit_last_rc(int tag)`、`int kdi_kaudit_skipped(int tag)` | `check()` 2 行と `kselftest_run` 先頭 2 行、`post_exec` / `post_probe` の監査呼出し 2 行 = **6 行** (読みは `#define ksel_pass (kdi_get_kselftest_pass())`) |

**設計時の段別一覧 (2026-10-14 の記録。現在の本数・添字ではない)**。現行は `kernel/kdebug_imports.def` と生成 `kdebug/generated/kdebug_imports.h:117` の `KDEBUG_IMPORT_COUNT` が正。

| 段 | 添字 | kind | 内容 | 本数 |
|---|---:|---|---|---:|
| S1 | 0 | FUNC | `kprintf` (`lib/kprintf.h`) | 1 |
| S2 | 1〜134 | FUNC | kselftest の外部関数 134 本 (`kprintf` を除く): `appmem_*` 3、`appslot_*` 7、`as_access_page`、`bootinfo_*` 5、`bootlog_*` 4、`caller_access_*` 3、`con_sink_*` 3、`console_*` 3、`copy_*` 2、`cpu_delay_us`、`cpu_loops_per_tick`、`db_v50_selftest`、`dma*_*` 14、`exec_heap_used`、`exec_kapi_layout_selftest`、`exec_tramp_page_addr`、`exec_tramp_user_selftest`、`fd_*` 3、`gfx_selected_selfcheck`、`gui_ime_set_render`、`irq_*` 10、`kapi_mem_stat`、`kbd_diag`、`kbd_inject_selftest`、`kheap_*` 4、`kmemcpy`、`kmemset`、`kprintf_attr_to_pc98`、`kstack_high_water`、`kstr*` 6、`kutoa_*` 2、`launch_selftest`、`lease_audit_all`、`lease_selftest`、`ledger_*` 13、`memmove`、`paging_*` 15、`pcm_open`、`pcm_state`、`pgalloc_*` 2、`pit_get_setup`、`res_owner_*` 2、`ring3_*` 6、`serial_gate_active`、`serial_puts_polled`、`shell_print`、`strchr`、`sys_time_now`、`sys_usable_mem_end`、`sysclk_*` 2、`time_branch_reset`、`time_test_feed*` 3、`tvram_readchar_at` (`nm -u kernel/kselftest.o` の順で `.def` に写す。生成器が `nm -u` の集合と `.def` の一致を検査する) | 134 |
| S2 | 135〜156 | GETTER | 読むだけ 22 (kselftest 分) | 22 |
| S2 | 157〜180 | GETTER/SETTER | 書く 12 × 2 | 24 |
| S2 | 181〜187 | PTR | 配列 7 | 7 |
| S2 | 188〜193 | OP | 新しい口 6 | 6 |
| S3 | 194〜207 | FUNC | gcap だけの外部関数 14: `console_hw_cursor_enable`、`exec_ledger_owner`、`gfx_get_height`、`kfree`、`kmalloc`、`kzalloc`、`pegc_restore_text_sync`、`v86_cui_display_restore`、`v86_gui_refuse`、`v86_is_active`、`v86_mem_setup`、`v86_mem_teardown`、`v86_pic_set_imr`、`v86_run_limit` | 14 |
| S3 | 208〜209 | GETTER | `v86_gp_cs`、`v86_gp_ip` | 2 |
| S3 | 210 | PTR | `v86_session` (構造体実体) | 1 |
| S4 | 211〜222 | FUNC | NIC 6 本 (`send/recv/get_mac/state/loopback/stat`)、VFS 5 本 (`mkdir/read/write/rm/sync`)、`boot_image_info`。tick_count getter は S2 の既存 slot 137 を使う | 12 |
| 合計 | 0〜222 | | **223** (関数 161 + アクセサ 56 + 新しい口 6)、schema 2 | **223** |

LB-1 の生成段は S4、export は既存 1〜5 と 18。gcap の移動はこの段では行わず、6〜17 は NULL。未実装の NIC import は NOSYS/DOWN/0 のスタブを持つ。

- 設計時の本体側費用の見積り: `kdebug_kexports[]` 4 B × 本数、getter 約 8 B、setter 約 10 B、ポインタ getter 約 6 B、新しい口 約 12 B。段別: S1 = 4 B。S2 = 193 × 4 = 772 + getter 34 × 8 = 272 + setter 12 × 10 = 120 + ptr 7 × 6 = 42 + 口 6 × 12 = 72 → **1,278 B**。S3 = 17 × 4 = 68 + getter 2 × 8 = 16 + ptr 6 → **90 B**。
- 部品側: import slot は `kdebug_import_slots[KDEBUG_IMPORT_COUNT]`。slot とスタブは部品の中 (像の予算の外)。
- 解決: `module_abi_gen == kdebug_abi_gen && import_count == KDEBUG_IMPORT_COUNT` を確かめ、**位置で**結ぶ (i 番目の slot に `kdebug_kexports[i]`)。名前表は道具用で、`name_hash` の不一致も拒否する。IRQ/例外文脈で呼べる import (`serial_puts_polled`、`kctx_*` の getter、`v86_gp_*` の getter、`v86_session` のポインタ) に `.def` で印を付け、例外文脈の口 (slot 7〜15) から呼べるのは印つきだけ、を `check-kdebug-ctx-host` で固定する。

### 3-7. 本体が常に返す状態 — MemStat 200 → **220 B** (版 75、slot 追加なし)

末尾に 5 欄: `u32 kdebug_state` (0 OFF / 1 RUNNING / 2 NOT_FOUND / 3 BAD_HEADER / 4 PAIR_MISMATCH / 5 NO_IMAGE_CRC / 6 ABI_MISMATCH / 7 NO_PAGES / 8 SHORT_READ / 9 BAD_CRC / 10 INIT_FAILED / 11 RELOC_INVALID / 12 IMPORT_INVALID; 負は使わない)、`u32 kdebug_pair_crc` (ヘッダから読めた値、読めなければ 0)、`u32 kdebug_pages` (= `kdebug.pages`、定常の body ページ数)、**`u32 kdebug_owner_pages`** (= `ledger_owner_pages(kdebug.owner)`、owner が retire 済み・0 なら 0 — `kdebug.pages` と独立に台帳から読む)、`u32 kdebug_open_count`。`kapi_sys.c:123` の `snap.size = min(size, sizeof)` で旧呼び手 (200 B) は不変。`STATIC_ASSERT(sizeof(MemStat) == 220)` と `offsetof(kdebug_state) == 200`。Rust 側 (`kapi_rust_gen.py`) も再生成。KAPI_SPEC §3-2 に v75 を登録、`kapi.json` の version と `KAPI_VERSION` を両方 75 に。
**owner のページを観測する経路**: (1) ゲスト内は `ver` / `kdebug` / `mem_stat_test` が `kdebug_owner_pages` を読む。(2) NP21/W は `kernel.map` の `kdebug` (record) の `owner` と `ledger_owners[owner]` (`pgalloc.c:30`、24 B、`pages` は +12、`kind` は +0) を `/api/mem` で読む。拒否後の漏れの判定は **`kdebug_owner_pages == 0` かつ `ledger_owners[owner].kind == 0`** (retire 済み)。

### 3-8. 監査の契約 — OFF でも監査は走る

- `kernel/kaudit.c` (本体) に `kselftest_run_audit` / `kselftest_audit_v86_return` / 計数 / `audit_first_fail_tag` / `v86_gcap_alloc/free_count` / `v86_gcap_phase` をそのまま移す (切り出しの印 `/* Lifecycle diagnostics,` と変異の置換文字列 4 本は無変更)。起動時の 2 回は `kaudit_post_exec()` と `kaudit_post_probe()` (kernel.c から)。結果は `kaudit_last_rc[tag]`、**`kaudit_post_exec` は `paging_v86_session_open()` なら監査だけ行い `kaudit_skipped[POST_EXEC] = 1`** を残す。
- 部品の `selftest_post_exec` は `kdi_kaudit_skipped(POST_EXEC)` なら**何もせず返る** (元の kselftest.c:975 の早期 return と同じく後続の自己試験もスキップ)。そうでなければ `check(kdi_kaudit_last_rc(KAUDIT_POST_EXEC) == 0, …)` と続きを行う。監査を呼び直さない。
- 契約: `KDEBUG` に関係なく監査の回数と計数 (`audit_runs` ≥ 2 の起動、AS-launch / gui / v86-return) は同じ。OFF で変わるのは `kselftest_pass/fail` が 0/0 になることだけ (S2 以降)。

### 3-9. 実機でデバッグモードを付けて起動する手順 ([D1] を含む)

1. rshell で `kdebug on` (`cmd_sys.c` の既存の `system.cfg` 書き換えで `KDEBUG=1`)、`kdebug off` で 0、`kdebug` で設定値と実状態 (MemStat の 5 欄)。
2. `reboot` (シリアルから、起動待ちは 1 回の待ち受け)。
3. `ver` で `Image CRC` と `kdebug: on (pair <同じ CRC>, body N pages, owner M pages)` を確かめる。
4. 受入・配備確認・CRTFREQ の採取・`v86 -g` はこの状態で。終わったら `kdebug off` → `reboot`。
5. 配備は対で: NHD は **停止 → `deploy-kernel` (vmkernel.lz4 + kdebug.bin) → 起動** ([D1]、`np21w_ctl.py` だけで)。不一致の注入試験: 停止 → 1 つ前の `kdebug.bin` を `nhd_deploy.py` で置く → 起動 → `ver` が `rejected (4 PAIR_MISMATCH)` → 停止 → 対を戻す → 起動。FD は boot の集合、HostDrv の `hsync` は `/boot` も入れ替える。
6. NP21/W の受入: `run-os32` の配備で `system.cfg` に `KDEBUG=1`。受入の prereq は「`kdebug_state == 1` かつ `kdebug_pair_crc == Image CRC`」。

---

## 4. 既存の利用者を固定する試験 (先に書くもの)

| 試験 | 何を固定するか | 段 |
|---|---|---|
| `check-module-image-host` (新設、`kernel/module_image.c` を取り込み) | §3-2 の全検査: 拒否 14 経路 (未来版・header_size・image_size 不一致・表の範囲外・表の重なり・reloc の区間の重なり・reloc が payload 外・import slot が data 外または非整列・export slot 0/重複/範囲外・名前 hash 不一致・CRC・pair 不一致・img_valid 0・abi/ops_version 不一致) が各 RED、正常 GREEN (**非整列の reloc を含む正常品を受理**)、`--mutate` | S0 |
| `check-mkmod-host` (新設) | `mkmod.py` の生成 → `module_image` の検証の往復、reloc の抽出 (PC32 除外、未解決は失敗)、`pair_image_crc` の埋め込み、prefix の実サイズの表示、`.def` と `nm -u` の集合の一致 | S0 |
| `check-kdebug-init-host` (新設) | 部品の `init` が候補表を埋めるだけで import を 1 つも呼ばない | S1a |
| `check-kdebug-ctx-host` (新設) | 例外文脈の口 (slot 7〜15) から呼ぶ import が `.def` の印つきだけ | S3 |
| `check-crt-diag-host` | 期待文字列不変、パスだけ | S1a |
| `check-mem-stat-host`、`check-kapi-layout-host`、`check-kapi-bounds-host`、`check-kapi-version`、`check-kapi-out` | MemStat 220 B、旧 size 200 の呼び手が不変、版 75 の一致 | S1b |
| `check-memmap-audit-host`、`test_memmap_audit.py` | `kaudit.c` の切り出しと変異 5 本が無変更の文字列で通る | S2 |
| `check-e10c-lifecycle` | 新しい直接呼出しの文字列 2 本、`PATHS` | S2 / S3 |
| `check-v86-gcap-host`、`e10c_gcap_host.c`、`r1_recovery_host.c`、`shm_user_host.c`、`surface_bundle_host.c` | 期待文字列不変、パス、record の偽物 | S3 |
| `check-guest-acceptance-host`、`check-tests-inventory`、`check-map` | yaml の prereq、`guest_tests.txt` の 2 本、対応表 | S1b / S2 |

---

## 5. 段の分け方、開始条件、順序 (各段 10 項以内、1 項 1 判定)

### 5-1. 開始条件 (R7-1 との関係)

- **今すぐ**: S0 (ホスト側だけ)。
- **カーネルに触る段 (S1a〜S3) の着地**は次のどちらかの後: (a) R7-1 の修正の中身が決まり、その修正と同じゲスト受入の回に載せられる、(b) 目視の後に PM が R7-1 をカーネルの外と判断し台帳の行をそう書き換えた (POLICY_DEV §1)。
- **S3** は CRTFREQ の読み取り段と R7-1 の両方が閉じてから。

### 5-2. 順序と受入時点

```
S0 (今、ホスト側)
S1a (loader・拒否・所有権 + crt_diag) → S1b (状態・CLI・互換試験)
  G1 = S1a + S1b のゲスト受入 (KDEBUG=1 と 0 の両方、R7-1 の修正と同じ回) + 運用受入 4 項  ← S2 の前提
S2 (kselftest → 部品、kaudit を本体へ、OFF = 0/0 への切替)
  G2 = S2 のゲスト受入 (台帳の 8 行 + H-4 もここで) → 実機 (24k 目視・CRTFREQ・H-5)
  → §6-3 の判定 → LAN L-B
S3 (gcap → 部品、CRTFREQ・R7-1 の閉鎖後)
  G3 = S3 のゲスト受入 (L-B の受入の回に載せてよい) → 実機
  → 後の段 (§9) / T3
```
ROLES §0 の 3 段: S0 は (2)、S1a〜S3 は (3) 全体。

### S0 — ホスト側の部品 (sol、製品像に触れない)
- 内容: `tools/mkmod.py`、`build/module.ld`、`build/kdebug.mk` の雛形 (空の部品)、`kernel/module_image.c` (検証だけ、未結線)、`kernel/kdebug_imports.def` と生成器 (`tools/gen_kdebug_imports.py`: `.def` → `kdebug_kexports[]`・アクセサ・abi hash・部品側の slot とスタブとマクロ、`nm -u` との一致検査)、`check-module-image-host`、`check-mkmod-host`、T4 票 §2-1 のスキーマの確定 (PM)。
- 受入 (5): (1) `check-module-image-host` の拒否 14 経路が各 RED。(2) 正常 GREEN (非整列 reloc 込み) と `--mutate` RED。(3) `check-mkmod-host` の往復が通る。(4) `kernel.bin` の sha256 が不変。(5) 静的 5 本 rc=0。

### S1a — loader・拒否・所有権 + 最初の中身 crt_diag (astra 候補)
- 内容: `kernel/module.c` (record・load・start・回収)、読み込み点と `KDEBUG=` の読み、`kdebug_kexports[]` (S1 は 1 本)、`struct kdebug_ops`、動的 MODULE owner、`kdebug/crt_diag.c` (移動) と `kdebug/kdebug_main.c` (init)、`[kdebug] rejected: …` の文字。MemStat はまだ触らない (観測は `kernel.map` の record と `ledger_owners` を `/api/mem` で)。**自己試験は本体のまま** (`kernel.c:598` は変えない)。
- 受入 (10): (1) `KDEBUG=0`: `kdebug.state == 0` かつ `open_count == 0`。(2) `KDEBUG=0`: `kdebug.owner == 0`。(3) `KDEBUG=1`: `state == 1` かつ `pair_crc == bootinfo の img_crc`。(4) `KDEBUG=1`: `ledger_owners[kdebug.owner].pages == kdebug.pages` (定常 = body、prefix は返した)。(5) `KDEBUG=1` で `[crt]`/`[crt2]` が boot.log に今と同じ形。(6) `KDEBUG=0` で `[crt]` が出ない。(7) 拒否 (a) 1 つ前のビルドの部品 → `state == 4`、起動継続。(8) 拒否 (c) `image_crc` の書き換え → `state == 9`。(9) 拒否 (d) `module_abi_gen` の書き換え → `state == 6`。(10) 拒否 (7)〜(9) の後に `ledger_owners[owner].kind == 0` かつ `pages == 0` (回収と retire)。
- 増分の実測: `__bss_end` の**増分 2,700 B 以下** (最小 loader と検証器の費用。当初の 800 B は見積もりの誤り — 検証 14 経路を本体に置く費用を約 1,000 B と見ていた。`-Os` 後の実測 +2,688 B、ユーザー承認 2026-10-14「推奨」で改めた。検証は削らない)。
- S1a 作業木の実測 (2026-10-14、基点 `6a712a8`): `-Os` 前 +3,456 B → `module.c`・`module_image.c` だけ `-Os` で +2,720 B → 最終 **+2,688 B** (`__bss_end` `0x197300` → `0x197d80`。`kdebug_load` 1,097 B、`module_image_header` 899 B、`module_image_validate` 1,109 B、crt 3 関数 −1,035 B)。部品は prefix 実データ 313 B (1 ページ)、body 1,320 B (1 ページ)。検証は削っていない。閾値は上の行のとおり 2,700 B に改めた。
- 検査の候補: `check-crt-diag-host check-module-image-host check-mkmod-host check-kdebug-init-host check-memmap-audit-host check-memory-host check-p2v` + 静的 5 本。

### S1b — 状態・CLI・互換試験 (sol、KAPI 75)
- 内容: MemStat 220 B (版 75: `kapi.json` の version、`KAPI_VERSION`、C/Rust 再生成、KAPI_SPEC §3-2)、`ver` の 1 行、シェル `kdebug on|off|(無し)`、`userland/tests/kdebug_probe.c` (要求版 75、`mem_stat` の 5 欄を出すだけ)、`mem_stat_compat_test` と `mem_stat_test` v75 (§2-3、**S1b の期待 = 既存の自己試験の計数**)、deploy.yaml / packages.yaml / deploy-set / nhd_deploy / hsync / app.conf、`guest_tests.txt`、yaml:1161。
- 受入 (10): (1) `KDEBUG=0`: `ver` が `kdebug: off`。(2) `KDEBUG=1`: `ver` が `kdebug: on (pair <Image CRC>, body N pages, owner N pages)`。(3) 拒否 (a) で `ver` が `rejected (4 PAIR_MISMATCH, file pair <crc>)`。(4) 拒否 (b) Image CRC が異なることを確かめた本体 + 元の部品 → `ver` が `rejected (4 …)`。(5) `mem_stat_compat_test` が新カーネルで PASS。(6) `mem_stat_test` v75 が `KDEBUG=1` で PASS (`pass ≥ 295`、`fail == 0`、`owner_pages > 0`)。(7) 同 v75 が `KDEBUG=0` で PASS (`state == 0`、**`pass ≥ 295` — 自己試験はまだ本体**、`owner_pages == 0`)。(8) `kdebug_probe.bin` (要求版 75) を**旧カーネル 74** が `required KAPI version is newer than kernel` (exec.c:1784) で断る (隔離 NHD に旧像を戻して)。(9) 旧要求版 (≤ 74) の gshell が新カーネルで通る。(10) 旧要求版の shlib が新カーネルで通る (`shlib.c:28`)。
- 増分の実測: `__bss_end` の**増分 300 B 以下**。
- 検査の候補: `check-mem-stat-host check-kapi-out check-kapi-version check-kapi-layout-host check-kapi-bounds-host check-gshell-host check-shlib check-deploy-set-host check-packages-host check-guest-acceptance-host check-tests-inventory` + 静的 5 本。[ABI3] `make clean` → `make all`、`/sys` の再配備。

> 2026-10-14 ユーザー決定: 搭載メモリ量ごとの確認は v3 の最終段にまとめる (DEFERRED_TESTS の関門「v3 の最終段」)。それまでのゲスト受入は 64MB の 1 構成。以下の構成一覧は最終段用として残す。

### G1 — S1a + S1b のゲスト受入 (両モード) と運用受入 (PM)
- ゲスト受入: S1a の 10 項と S1b の 10 項を NP21/W (64MB PEGC、np21x64w.ini) で、`KDEBUG=1` と `0` の両方の起動で。
- 運用受入 (4): (1) `deploy-set.json` と `verify-set` が `kdebug.bin` を含む。(2) `hsync boot` の後に `ver` の pair が一致。(3) NHD の対の配備 (停止 → `deploy-kernel` → 起動、[D1]) の後に `ver` の pair が一致。(4) 不一致の注入 (停止 → 旧部品 → 起動 → 停止 → 戻す → 起動) が §3-9 の 5 の手順で再現し、戻した後に `state == 1`。
- G1 の完了が S2 の着手条件。

### S2 — kselftest を部品へ、監査を `kaudit.c` へ、OFF = 0/0 へ切替 (astra 候補、G1 の後)
- 内容: `kdebug/kselftest.c` (30 試験、`check`、名前、報告; §3-6 のソース変更 約 35 行)、`kernel/kaudit.c`、`kernel/kselftest_report.c` (`kdi_kselftest_count/reset/get_*`、`[selftest] skipped (KDEBUG=0)`)、kernel.c の 4 点、`.def` に添字 1〜193、`check_map.d` の 10 本、`test_e10c_lifecycle.py` / `test_memmap_audit.py`、**`mem_stat_test` v75 の kselftest の期待を OFF = 0/0 に書き換え**、yaml の prereq、regress.txt、HANDOVER / POLICY_DEBUG / skill。
- 受入 (10): (1) `KDEBUG=1` で `kselftest_pass` = 今の N (295、planar 294)。(2) `KDEBUG=1` で `kselftest_fail == 0`。(3) `KDEBUG=0` で `kselftest_pass == 0 && fail == 0`、`[selftest] skipped` 1 行、起動継続。(4) OFF と ON で起動時の `audit_runs` が同じ値 (≥ 2) かつ `audit_fail == 0`。(5) OFF で GUI 往復 2 回後に `audit_runs` が増える。(6) `kernel-r1` + `KDEBUG=1` で 1 項目を落とし赤い `FAIL: <名前>`。(7) `mem_stat_test` v75 (書き換え後) が ON と OFF の両方で PASS。(8) `check-memmap-audit-host` と `test_e10c_lifecycle.py` が書き換え後に `--mutate` で RED を保つ。(9) `pt0_snapshot.py` の必須記号が全部 `kernel.map` にある。(10) `__bss_end` の**純減 25,000 B 以上** (S1 からの差)。8MB 構成で `KDEBUG=1` の起動が通ることは G2 の H-4 の行で (別の判定、[未])。

### S3 — `v86 -g` の記録器を部品へ (astra 候補、CRTFREQ・R7-1 閉鎖後)
- 内容: `kdebug/v86_gcap.c` / `v86_gcap_math.c` / `v86_gcap_diag.h` / `v86_test16_gcap.asm` の写し、本体の結線 11 本、KAPI 239 の NOSYS 分岐、`.def` に添字 194〜210、`check-kdebug-ctx-host`、ホスト 3 本の record の偽物、`userland/cmds/v86.c` の文言。
- 受入 (8): (1) `KDEBUG=1` で `v86 -g -t` が今と同じ出力 (NP21/W、`[gcap] ph=` 3 行)。(2) `KDEBUG=0` で `v86 -g` が NOSYS の文言、`v86 -b` / `hal_test` が不変。(3) `KDEBUG=1` で `v86 -g` 中の CTRL+STOP (台帳 E10-4 の hook) の後に `v86_gcap_free_count == alloc_count` で再採取できる。(4) 実機で CRTFREQ r3a §3 の採取が `KDEBUG=1` で今と同じ欄。(5) `check-v86-gcap-host` 等が無変更の文字列で rc=0。(6) `__bss_end` の**純減 6,000 B 以上**。(7) 部品の body が計画のページ数 (§6-2) 以下、超えたら票に実数を書く。(8) `pt0_snapshot.py` の必須記号が全部ある。

---

## 6. 削れる量 — 段ごとの import 集合と増分を同じ実数で

### 6-1. 基準 [確]
像予算の残り **7,424 B**。T2D_T2H §6-1 の表に「実測 (78207df)」の行として足す。

### 6-2. 段ごとの計画値と受入の閾値 (B)

| 段 | 移る節 | その段で足すもの (本体) | 像の予算: 計画値 | **閾値** | OFF で常駐から消える | 部品の body (段ごとの丸め) |
|---|---:|---|---:|---:|---:|---:|
| S0 | 0 | 0 | 0 | **sha256 不変** | 0 | 0 |
| S1a | crt 1,201 | loader + 検証 約 1,000、record 96、`kdebug_kexports` 1 本 4 + abi/count 50、結線 1 点 12、文字 約 100、cfg 読み 60 | **+121** | **増分 2,700 B 以下** (旧 800 B、2026-10-14 改) | 1,201 | crt 1.2 KB + init 0.3 + スタブ 7 B → **1 ページ** |
| S1b | 0 | MemStat 5 欄の写し 約 60 + bss 20 | **+80** | **増分 300 B 以下** | 0 | 0 |
| S2 | 29,890 | `.def` 添字 1〜193: 表 772 + getter 272 + setter 120 + ptr 42 + 口 72 = 1,278、結線 4 点 50、`kaudit` の分離 50、`[selftest] skipped` 40 | **−28,472** | **純減 25,000 B 以上** | 29,890 | text 18.6 + rodata 9.2 + bss 2.3 + スタブ 193 × 7 ≈ 1.4 + slot 0.8 → 32.3 KB → **8 ページ** |
| S3 | 6,976 | `.def` 添字 194〜210: 表 68 + getter 16 + ptr 6 = 90、結線 11 点 約 130、NOSYS 20 | **−6,736** | **純減 6,000 B 以上** | 6,976 | 7.0 KB + スタブ 0.1 → **2 ページ** |
| **1 段目の合計** | **38,067** | | **−35,007** | **合成 −28,000** (+2,700 + 300 − 25,000 − 6,000、2026-10-14 改。旧 −29,900) | **38,067 (≈ 37.2 KiB)** | **段ごとの丸め 1 + 8 + 2 = 11、全体の詰め直し (41.5 KB → 10.1 → 11) = 11 ページ** |

- **prefix**: ヘッダ 144 + reloc (約 1,500 件 × 4 ≈ 6 KB [推]、`mkmod.py` が実数を出す) + import 211 × 12 = 2,532 + export 16 × 12 = 192 + 名前表 約 4.3 KB → 約 13.2 KB → **4 ページ [推]**。ピーク = body 11 + prefix 4 = **15 ページ**、定常 = 11 ページ (prefix は init 後に返す)。実数は S0 の `check-mkmod-host` が表示する値で票を書き直す。
- 二重計上なし: kselftest の監査 635 と報告 150 は「移る」に入れない。gcap は `v86_gcap.o` の 4,966 (`v86g_phase_diag` を含む) + math 1,410 を 1 回だけ。`.def` の表は段ごとに足す本数だけ計上。
- 閾値に届かない段は着地させず、原因を票に書いて計画値を直す。各段の着地で `kernel.map` の `__bss_end` と `ls -l vmkernel.lz4` を T2D_T2H §6-1 に実測の行として足す。

### 6-3. LAN L-B への効き (Q2、単位 B)
着手条件: **S1 + S2 の着地後に残りが ≥ 19,456 B** (15 KiB + 4 KiB — 計画上の着手条件)。閾値どおりなら 7,424 − 2,700 − 300 + 25,000 = **29,424 B** (旧 31,324 B)、計画値なら 7,424 − 121 − 80 + 28,472 = **35,695 B** で成立。届かなければ 2026-09-22 の決定 (b) (SHM −16 KB) を予備として使う。

### 6-4. 達成範囲の表示 (Q1、全面達成とは記録しない)
OFF で常駐から消えるのは移した 3 対象の **38,067 B**。本体に残る診断関連は**約 43 KB** (§0-3: 仕組み 2.4 + 監査 2.7 + 他ファイルの試験本体 12.5 + bootlog 16.7 + 計装 2.5 + 例外ダンプ 2.9 + link 2.8)。1 段目は「対象を絞った 1 段目」と記録する。

---

## 7. ユーザーの決定 (2026-10-14、推奨どおり) — この票の前提

| # | 決定 |
|---|---|
| Q1 | 製品の既定は `KDEBUG=0`。受入と採取は「対の部品が RUNNING (`kdebug_state == 1`、`kdebug_pair_crc == Image CRC`)」を前提にする。1 段目は「対象を絞った 1 段目」と記録し、全面達成とは書かない (§0-3・§6-4)。CI (build / host / static) は実ゲストの起動確認の代用にしない |
| Q2 | L-B は S1 + S2 の後の実測で残り ≥ 19,456 B なら着手。SHM を削る案は予備として残す (§6-3) |
| Q3 | 部品の形式は今回 T4 と同じ形式に確定 (§3-2 のスキーマ: ヘッダ 144 B・固定の読み込み長・表の下限・CRC の範囲・slot の位置・offset の基点・prefix の表のレコード・body 内の import slot・検査の上限)。T4 票 §2-1 はこのスキーマを正典として参照する |

PM の推奨の範囲で進めるもの: 版 75 (MemStat 220 B、slot なし)、`system.cfg` の `KDEBUG=`、動的 MODULE owner、`kdebug_probe.bin`、S0〜S2 の着手。

---

## 8. 触らない範囲・未確認

- 触らない: 監査の実体 (paging/lease/pgalloc 内)、他ファイルの自己試験の本体 (§0-3 (c))、bootlog の配列と手順と置き場、R7-1 の計装、`owner_diag.h` / `serial_diag.h`、起動進行の kprintf、`net/link.c`、`con_sink` / `kbd_inject`、例外ダンプ、KAPI の既存 slot の ABI (239 は実体の置き場だけ)、`BootImageInfo`、gshell / shlib / sh.bin の要求版。
- [未] 8MB 構成で `KDEBUG=1` のとき池に 15 ページ (ピーク) の余裕 (G2 の H-4 の行で)。
- [未] prefix の実サイズ (S0 の `check-mkmod-host` で確定し §6 を書き直す)。`.def` の 211 本は `nm -u` の集合との一致検査で固定する。
- [未] S1a / S1b で loader と MemStat の実測費用 (増分 2,700 / 300 B 以下。S1a は実測 +2,688 B)。
- [推] 併合後の文字列の減り (9,196 → 約 9,000)。着地で map の kernel.o の `.rodata.str1.1` の行の前後差を記録。
- 文書の更新先 (正典 1 か所): 到達確認の規則 → POLICY_DEBUG §2、読み口の一覧 → POLICY_DEBUG §5、KAPI → KAPI_SPEC §3-2 (v75)、予算 → T2D_T2H §6-1、ファイル地図 → DEVELOPMENT §2 と 08_build §8-3 (`kdebug/`、`build/kdebug.mk`、`kernel/module*.c`、`kernel/kaudit.c`、`kernel/kselftest_report.c`)、配備 → 08_build §8-4 (対で配る、[D1])、モジュール形式 → TASK_T4_T6_MODULES §2-1 (§3-2 のスキーマ、`pair_image_crc`、`ops_version`、最初の実例 = kdebug)、台帳 → DEFERRED_TESTS に S3 の関門 (R7-1 / CRTFREQ の閉鎖) と 8MB の行。

---

## 9. 後の段へ送るもの (設計は書かない — 送り先と関門だけ)

| 送るもの | 関門・条件 |
|---|---|
| 他ファイルにある自己試験の本体 (§0-3 (c)、約 12.5 KB) を部品へ — paging / lease / con_sink / kbd_inject / appslot / launch / exec の static な内部への export が要る | S3 の後。T4 の export 機構が整ってから |
| 事象の記録帯 (`kevent_put`、リング)、`kevent_read`、`boot.kev`、シリアルの `E:` 固定行、復号器 | G2 の後。読み出し・保存は部品側に置く前提で設計 |
| bootlog の固定窓 (`MEM_KERNEL_RESV` の空き) への移設と保存後の NP 化 | paging の写像と `paging.c:1318/1591` の監査の期待状態の変更を伴うので別段。`P2V_BOOT` / `P2V` の時点を明記、DMA プール直下の NP ガード 1 ページを残す |
| R7-1 の計装 (L1〜L8・L7') の後継、`owner_diag` / `gfx_diag` / `pegc_diag` / 起動進行の文字列の事象化 | 台帳 R7-1 行の閉鎖台帳への移動 |
| D3: `net/link.c` の L0〜L3 の自己試験を製品像から外す | 残すなら同一翻訳単位内の条件付き include (`#if CONFIG_LGY98_FLAGS & LGY98_FLAG_LINKTEST` → `#include "link_selftest.inc"`、static の補助関数 `link_ready` / `link_state` / `link_wait_status` / `link_counters_reset` と公開 4 関数を一体で、ホスト側は `LINK_HOST_TEST` と同じ guard) の形だけの独立の小段 |
| `v86_smoke_test` / `v86_disk_test` の部品化、kselftest の項目のホスト試験への写し (任意) | S3 の後 |
