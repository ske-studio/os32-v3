# S5 — 設定レジストリの実測と受入 (DESIGN §6)、S4 の残件

> 発行: PM (2026-09-13) / 状態: **受入完了 (2026-09-13)**

R2 (PEGC / Cirrus) はユーザー決裁 (1.b) で未実施 = v1.4 の描画系の票でまとめて行う — S5-C `5ccf6b8` + `e1b4828` (Codex 往復 1 で 4 件 → 往復 2 で Approve)、S5-W `7a8ad16` (指摘なし)、M1〜M6 / R1 / R3 / R4 は §6。前提: S0 / S2 / S4 完了 (main `9d455e0`)。
正典: [DESIGN.md](../../tasks/settings/DESIGN.md) §6 (実測項目)、[S0_PLAN_2026-09-13.md](S0_PLAN_2026-09-13.md) §2 (S5 = 実測と受入、PM / テスター)、[TASK_S2.md](TASK_S2.md) §8 (C7 の pool 復帰が未計測: `db_test` が継承バグで落ちる)、[TASK_S4.md](TASK_S4.md) 状態行の残件 4 件。
規約: コーダーは worktree + ホスト TDD のみ。PM は測定の観測と判定、テスターが make / 配備。ini は [D2] (§3 R2 参照)。

## 0. 分担

| 票 | 範囲 | レーン | 触るファイル |
|---|---|---|---|
| **S5-C** | (1) 計測プログラム `userland/tests/cfg_bench.c` (CPL=3、`libos32cfg` をリンク): `cfg_bench [n] [m]` = n 回 (既定 50) の「`cfg_open(RO)` → m 件 (既定 20) の `cfg_get_int/text` (存在する 3 キーを巡回、無いキーも混ぜる) → `cfg_close`」。各回の tick (`get_tick`) と `db_mem_used` を 10 回ごとに 1 行、最後に **最小 / 最大 / 平均 tick、`db_mem_used` の開始 / ピーク / 終了**、状態 (cfg_status) と失敗回数を出す。`cfg_bench -w` は「`cfg_open(RW)` → begin → `cfg_set_int` 1 件 → commit → close」を n 回 (書き戻しの tick)。(2) `cfg status` の出力に `pool <db_mem_used> B` を足す (`cfg status` = `OK schema_version 1 pool 12345 B`。既存の受入 C2 の文言はこの追記を許す) | C | `userland/tests/cfg_bench.c`、`userland/cmds/cfg.c` (status の 1 行)、`tools/tests/cfg_host.c` / `test_cfg.py` (status の整形)、`tools/tests/s5_tdd.md` |
| **S5-W** | S4 の残件 4 件 (non-blocker): S20(c) を 2 本目の set 失敗 (`[0, -1]`) に、S18 の mock を「2 本目の get で ERROR へ遷移」に、S09b / S14 のコメントを「実 park 遷移ではない」に限定、TAB で Cancel に焦点表示しても RETURN が OK になる不一致 (Input ダイアログと同様に TAB でボタン焦点を動かさない) | W | `userland/gshell/src/modal.rs`、`userland/gshell/host/{settings_tests.rs, mocks.rs}`、`tools/tests/s4_tdd.md` |
| PM | `build/programs.mk` (cfg_bench の明示規則、`$(LIBCFG_OBJ)`)、`build/app.conf` (`userland/tests/cfg_bench 50 0`)、`userland/deploy.yaml` (`userland/tests/*.bin` の glob に乗るか確認)、`tools/emu_agent/agent.py`、測定の実施と記録 (§2)、3 バックエンド回帰 (§3) | PM / テスター | — |

## 1. 静的確認 (PM、コード変更なし)

- **同期** (DESIGN §6「同期」): `os32Sync` (lib/sqlite3/os32_sqlite_vfs.c:254) は `vfs_sync()` を呼び、`vfs_sync` は全マウントの `ops->sync` (= `ext2_sync`) を呼ぶ。`ext2_sync` が dirty バッファをデバイスへ書き戻すなら、`cfg_commit` の後に別の sync は不要。書き戻さない (no-op) なら `cfg_commit` の末尾に `vfs_sync` KAPI を足す (S5-C に追加)。判定は §2 の M3 に書く。
- **ジャーナル** (DESIGN §6「ジャーナル」): DELETE journal の回復は SQLite 本体。S2 の契約では **hot journal は自動回復せず CORRUPT** (BUSY_RECOVERY → CORRUPT、リカバリは S3)。強制終了試験 (使い捨てイメージ) は S3 の領分にし、S5 では「journal が残った状態の検出」だけを M2 で確かめる。

## 2. 実測 (PM / テスター、ゲスト = NP21/W 15MB pc98、386 相当ではないので tick は相対値)

| ID | 項目 | 手順 | 合格 / 記録 |
|---|---|---|---|
| M1 | プール | `ime on` (FEP 辞書常駐) → `cfg_bench 50 20` | `db_mem_used` の終了値が開始値に戻る (差 0)、失敗 0、`-2` (pool 枯渇) が出ない。ピークを記録 |
| M2 | ジャーナル | `cfg status` OK → `cp /etc/settings.tsv /etc/settings.db-journal` (非ゼロの journal を偽装) → `cfg status` / `cfg get` / `cfg init` → `rm` → `cfg status` | 偽装中は `CORRUPT` (hot journal)、get は既定値、`cfg init` は `needs recovery: journal present` で拒否、rm 後に OK に戻る。gshell も起動時通知 CORRUPT を出す (G4 と同じ経路) |
| M3 | 同期 | §1 の静的確認 + `cfg set` の直後に NP21/W を強制終了せず**リセット** (`os32-cycle` の emu 再起動と同じ = 電源断相当) → `cfg get` | 値が残る (ext2 が書き戻していれば)。残らなければ `cfg_commit` に `vfs_sync` を足して再測 |
| M4 | 速度 | `cfg_bench 50 20` (FEP なし / あり) の平均 tick、`cfg_bench -w 20` の平均 tick、gshell の `load` / `save` tick (S4 G7: 24t / 59t) | 記録。gshell 起動の体感 (load ≤ 50t) を目安 |
| M5 | 大きさ | ホスト: 300 件の合成 tsv (`tools/tests/mk_size_fixture.py` 相当を PM が scratch で) → `mk_settings_db.py` → サイズ | 64KB 以内 (page 1KB)。記録 |
| M6 | メモリ | M1 の `db_mem_used` ピーク (FEP 常駐 + 設定 DB) と、`cfg_bench` を CUI / GUI 端末の両方で | 記録。OOM (`-2`) が出ない |

## 3. 回帰と受入

| ID | 内容 | 合格 |
|---|---|---|
| R1 | 9801 (現行 `GFX=pc98`): `gui_gate.py v11` / `v12g1` / `v12g4`、S4 の G2 (Settings で色変更) | 全台本が従来どおり通る |
| R2 | PEGC / Cirrus: ゲスト側 `gfxmode pegc` / `gfxmode cirrus` → リセット → `hal_test` で backend を確認 → R1 と同じ台本 | **NP21/W が 9821 モデルで起動していることが前提** (現在の `hal_test` は `pc98 (planar 4bpp)`)。ini の変更が要るなら [D2] でユーザー承認を取ってから (スキル `os32-emu-config`)。承認が無ければ R2 は未実施と記録 |
| R3 | regress 6 本 + kselftest | 87 / 0 |
| R4 | S4 残件 (S5-W) のホスト試験 | gshell host 95 本 + 追加分 GREEN、TAB の焦点表示が RETURN の動作と一致 |

## 4. レビュー

Codex (枯渇時は Fable 5.1 サブエージェント、ROLES §5) に S5-C (`cfg_bench.c`、`cfg status` の 1 行、必要なら `cfg_commit` の sync) と S5-W の差分だけを 1 往復で見てもらう (実測票なので設計レビューは無し)。観点: cfg_bench が open〜close の間に yield しない、計測の tick の取り方、`db_mem_used` の読み方、`cfg status` の後方互換、S4 残件の直し方。

## 5. 記録

実測値は本票 §6 に PM が追記し、DESIGN §6 の表を「実測済み」に更新する。S3 (リカバリ) / S6 (tar) / F3a-c の着手判断は実測の後にユーザーへ。

## 6. 実測の記録 (追記)

- **M3 (静的、2026-09-13)**: `os32Sync` → `vfs_sync()` → `ext2_sync()` は superblock と group descriptor を書く (`fs/ext2_super.c:274`)。データ / inode / bitmap のブロックは `ext2_write_block` → `dev_blk_write_lba` で**書いた時点でデバイスへ出る** (write-through、`fs/ext2_super.c:43`。ext2 にダーティキャッシュは無い)。→ `cfg_commit` の後に別の sync は不要。動的確認 (2026-09-13): `cfg set gshell desktop/color int 9` → NP21/W を**ハードリセット** (MCP `emu_reset`、電源断相当) → 起動 18 秒後 `cfg get` = **9**。書いた時点で NHD に出ている。合格 (追加の sync は不要、`cfg_commit` は変更しない)。
- **M2 (ゲスト、2026-09-13)**: `cp /etc/settings.tsv /etc/settings.db-journal` (1406 B の偽 journal) → `cfg status` = **`CORRUPT sqlite=261`** (BUSY_RECOVERY)、`cfg get … 99` = 99 (既定値)、`cfg init` = `already exists` (本体が存在するので (a) の判定が先に効く — S2 §1-7 の順どおり。journal の拒否文言は本体が無いときに出る)、`rm` 後 `cfg status` = OK、get = 12。合格。
- **M1 / M4 / M6 (ゲスト、2026-09-13、配備 `5ccf6b8`、NP21/W 15MB pc98)**:
  - `cfg status` の pool: FEP 無し **24,768 B** (接続保持中)、`ime on` 後 **63,552 B**。
  - `cfg_bench 50 20` (FEP 無し): 1 回 (open + 20 get + close) = **114〜115 tick** (avg 114、total 5711)、pool start 0 → peak 24,832 → **end 0** (完全に戻る)、failures 0。
  - `cfg_bench 20 20` (FEP 常駐): **113〜114 tick**、pool start 38,784 → peak 63,616 → **end 38,784** (辞書ぶんを残して戻る)、failures 0。**M1 合格** (差 0、`-2` 無し)。
  - `cfg_bench -w 10` (FEP 常駐): 1 回 (open RW + begin + set + commit + close) = **58〜59 tick**、peak 64,640 B。
  - M4: get 1 件あたり約 4.5 tick、open + close で約 20 tick (gshell の起動時 load = 24t と整合)。gshell 起動の目安 (≤ 50t) 内。tick は NP21/W の実行速度 (386 相当ではない) での相対値。
  - M6: FEP 辞書 (約 38.8KB) + 設定 DB (約 25KB) の共存ピーク **64,640 B** / 384KB プール。OOM 無し。GUI 端末での実行は R1 の後に追記。
  - 注: `/api/cmd` は約 30 秒で EOT を待ち切るので、50 回の read (57 秒) は応答がずれる。実測は 20 回以下で回す。
- **R3 (ゲスト、2026-09-13)**: regress 6 / 6 (`s5reg`)、kselftest 87 / 0。
- **R1 (ゲスト、2026-09-13、9801 `GFX=pc98`)**: `gui_gate.py v11` / `v12g1` / `v12g4` すべて RESULT OK、各台本の `CUI back: ok` (6 項目の Start メニューで `ROW_CUI` が CUI mode に当たる)、`system.cfg: GUI=0`。S4 の G2 は配備 2 回目 (TASK_S4 §10d) で確認済み。
- **M5 (ホスト、2026-09-13)**: 合成 tsv 300 件 (int 100 / text 96B 100 / blob 64B 100、scope `app:bench`) → `mk_settings_db.py` → **29,696 B** (page 1KB)。64KB 以内で合格。
- **再測 (ゲスト、2026-09-13、配備 `e1b4828` = 往復 1 の 4 件反映後、cfg_bench.bin 16,060 B)**: CUI `cfg_bench 20 20` FEP 無し 114〜115 tick、pool 0 → 24,832 → 0。FEP 常駐 113〜114 tick、pool 38,784 → 63,616 → 38,784。`-w 10` 58〜59 tick、peak 64,640。**GUI 端末 (t5a_display) で `cfg_bench 10 20`** (M6 の GUI 分): 114〜115 tick、pool 38,784 → 63,616 → 38,784、failures 0、出力は 1KB ごとの yield で端末に全行届く (`s5_gui_bench.png`)。数値は修正前と同じ (出力の yield は計測窓の外)。
- **R4**: gshell host 96 / 96 (S21 TAB を含む)、cfg host 45 / 45 (signed-overflow sanitizer 込み)。
- **Codex**: S5-C 往復 1 = Request changes 4 件 (cfg_bench の signed overflow、status の採取位置、集計幅、GUI 端末で出力が消える) → `e1b4828` → 往復 2 = **Approve**。S5-W は指摘なし。
- **R2 (PEGC / Cirrus)**: 未実施。現在の NP21/W は 9801 モデル (`hal_test` = `pc98 (planar 4bpp)`) で、9821 への ini 変更は [D2]。**ユーザー決裁 2026-09-13 (1.b): 未実施のまま S5 を閉じ、v1.4 の描画系の票でまとめて行う。**
- **R2 の予備調査 (2026-09-29、Opus 5.5 コーダー、観測のみ)**: ユーザー報告「PEGC 構成 (`hal_test` = `backend pegc`、
  `screen 640x480`) で `gui_gate.py v11` / `v12g1` / `v12g4` は RESULT: OK だが、スクリーンショットは 640x400 (`scrn_ymax` 400)、
  リセット後も同じ」を NP21/W (ゲスト commit `371bd36`、KAPI v68、HDD 起動、`/etc/system.cfg` = `GUI=0` のみ) で調べた。
  - **原因: GUI に入っていない。** `gui_gate.py` の台本は `enter_gshell()` の前に rshell を抜けない。rshell が有効なまま
    (リセット直後・`leave_gshell()` の後・`/api/cmd` を使った後はいつもそう) だと、4 文字ずつ送る `os32gui` が
    `os32` / `gui` の 2 コマンドになり (`command not found`)、以後の Run... のパスも `/usr` `/bin` … と CUI で実行される。
    **再現した**: rshell 有効のまま `gui_gate.py v11` → `status` は台本の先頭から最後まで `scrn_ymax 400 grph_disp 0`、
    7 枚とも 640x400 の CUI の文字画面、それでも **RESULT: OK**。POLICY_DEBUG §4-31 の既知の罠 (「台本は先頭で ESC を 2 回」) と同じ。
  - **RESULT: OK は GUI に入った証拠にならない**: `v12g1` / `v12g4` の合否は `leave_gshell()` の「`ver` が返るか」だけ、`v11` は
    それに `wab_relay == 0` を足すだけで、`scrn_ymax` / `grph_disp` を照合しない。CUI に居続けても合格になる。
  - **PEGC 480 ラインそのものは正常**: 同じゲストで ESC → `os32gui` で入ると `/api/status` = `scrn_ymax 480 grph_disp 1`、
    `/api/screenshot` = 640x480 (タスクバー・時計が下端 y≈456〜479 に出る)、`emu_gdc` = `m_sync 104e4b0c0306e095`
    (NP21/W の 480 ラインの値)、`m_pitch 0x50`、`grphymax 480`。CUI では `scrn_ymax 400`・`m_sync 104e072507079065` で、どちらも期待どおり。
  - `hal_test` の `screen 640x480` は `pegc_query()` の固定値 (`PEGC_HEIGHT_480`) で、今 480 ラインで表示しているかは表さない。
  - **R2 そのものは未実施のまま** ([V4])。正しく回すには、各台本の前に `/api/key seq=ESC` で rshell を閉じる
    (`[Remote shell closed]` を tvram で確かめる)。台本側で直すなら、先頭の ESC と `status()` の `scrn_ymax == --h` かつ
    `grph_disp == 1` の照合を合否に入れる案 (道具の変更 = 別作業、未着手)。Cirrus 側は `[pci] 0 devices`・ini の
    USEGD5430 の有無を未確認で、有効化が要るなら ini の変更 = [D2] の承認事項。
- **R2 (Cirrus、2026-09-29 夕、テスター Opus 5.5、観測のみ)**: feat/gui `44bd0fe1` (A2: 窓の可否を物理地図で・リニア窓を
  FE000000h・auto は `np2_detect()` が真のときだけ Xe10 の ID を読む) を NP21/W `np21x64w-cirrus.ini`
  (USEGD5430=true, GD5430TYPE=91。PM 作成、テスターは無変更) で確認した。
  - **配備**: `np21w_ctl stop` → `make nhd-pull` → `make deploy-kernel` (rc=0、`Done! (199.9 MB copied)`) →
    `np21w_ctl start --ini np21x64w-cirrus.ini --wait-ready` → `make deploy` (rc=0) → ゲストで `hsync sys`
    (copied=0 errors=0) と `hsync` (copied=2 errors=0)。反映: `ver` = `Commit: 44bd0fe`・`API: v68`・Image 463,928 B、
    `ls -l /boot/vmkernel.lz4` = 463,928 B = 手元 `build/out/vmkernel.lz4`。kselftest (新 map `0x169ae0`) = pass 242 / fail 0。
  - **auto の probe** (`/etc/system.cfg` = `GUI=0` のみ、GFX 行なし): `hal_test` 1 行目 = **`backend cirrus (packed 8bpp)`**、
    `screen 640x480 bpp=8`。`/api/mem` (新 `kernel.elf` の nm): cirrus `s_probed` = 1・`s_probe_ok` = 1、pegc `s_probed` = 0・
    `s_probe_ok` = 0 (cirrus が先に通り PEGC は試されていない)、`sys_top_reserved` = 0 (予約するのは PEGC だけ — 期待どおり)。
    実行中の `s_lin` = **0xFE000000**・`s_lin_pages` = 0x200 (2MB)。A2 の症状 (probe が ID 判定の前に 0) は消えた。
  - **gui_gate (--h 480)**: 道具そのままの `gui_gate.py v11` は **NG** — 入口の `gui_entered()` が 98 の GDC
    (`scrn_ymax 400`・`grph_disp 0`) しか見ず、Cirrus の GUI は WAB 中継 (`/api/status` の `wab_relay 1`・`wab_height 480`)
    なので、gshell に入っているのに NG と判定し、`back_to_cui` が `grph_disp == 0` の枝で `rshell` を **gshell に打ち込んだ**
    (ゲストは GUI に残り、`leave_gshell(Mouse(480))` で手で戻した。害は無し)。§4-31 の `back_to_cui` の説明
    (「高さ違いでも `grph_disp == 1`」) は Cirrus には当たらない。**道具の修正は別作業 (未着手)**。
    台本は scratch の包み (道具は無変更、`status()` に `wab_*` を足し、`gui_entered()` に「`wab_relay == 1` かつ
    `wab_height == h`」を足し、撮影を src=auto / src=wab の両方にしただけ) で回した:

    | 台本 | 結果 | 画像で確かめたこと |
    |---|---|---|
    | `v11` | RESULT OK、最後の `wab_relay` = 0 | gui_demo 2 窓・XOR 枠のドラッグ・重なり・Help の前面化・チェックボックス / OK の配送 (`OK pressed!`)・× で閉じる |
    | `v12g1` | RESULT OK、CUI back ok | タスクバー・Start (6 項目)・Programs・右クリックのメニュー・窓ボタンで前面化・時計 17:28 → 17:29 |
    | `v12g4` (halt なし) | RESULT OK、`system.cfg: GUI=0` | Run... で v12_api_test 起動・key 4 で gui_demo 起動・CUI mode の確認ダイアログ (日本語) → Yes |

    撮影 19 手 × 2: src=auto も src=wab も **`X-Screen-Source: wab`・640x480**、auto と wab の画素は全枚一致。
    描画は FE000000h の窓経由 (present は `s_lin` へ書く) で、画面が正しく出ていることを画像で確認した。
    `fault_kill_count` = 0、`fault_generation` = 0、kselftest fail = 0 (台本後)。
  - 観察 (不具合とは判定しない): `v12g4` の「Run... again while demo runs -> v12_api_test **replaces** it」は、実際には
    gui_demo を残したまま v12_api_test が**並んで**起動する (タスクバーに Widgets / Help / v12 api tes)。台本の文言が
    多重アプリ (G7) 以前のもの。9801 で同じかは今回見ていない。
  - 終了時: ゲストは CUI + rshell、`/etc/system.cfg` = `GUI=0` のみ (GFX 行なし = auto)。NP21/W は `np21x64w-cirrus.ini` のまま稼働。
  - 未実施: PEGC 構成での R2 (台本の入口を直した後の再測)、`v12g4 --halt`、S4 の G2 (Settings で色変更) の Cirrus 版。
