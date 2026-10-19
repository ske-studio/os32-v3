# 持越し台帳の閉じた行 — e11 統合受入 〜 T2h R0〜R7

状態: **完了記録 (2026-10-13 更新、T2h R0〜R7 の閉鎖を末尾に追加)**。元の行は閉鎖時のまま保持。
証拠の基点: `~/os32-tmp/`。統合受入は `evidence/2026-10-07/accept_e11/RESULT.md`、T2h は `evidence/2026-10-10/` と `2026-10-11/` の `h_*/RESULT*.md` (実機 R7 は票 §5-1 とノート `~/os32-r7/`)。
現行の残件は [DEFERRED_TESTS.md](../../tasks/DEFERRED_TESTS.md)。

## E11-1

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-1 | c1 公開口と c2 SDK CLIENT/Unicode port・present/待ち帰路・USER橋を結線。RO日本語・両gfx実体・世代回復・旧USER撤去との統合ゲスト確認を PM に残す (c2_report.md)。c2 レビュー: present ごとの query (R5) は統合ゲストで bench の前後を記録、t5a_display の gfx 全体 attach (R4) は見送り | ホスト検証・ゲスト未配備 | 223–257、430–455、571、584–585、654–656、821–823、847、911、949、1023–1040、1069、1089、1105–1108、1143、1204–1207、1367–1368 | e11a/c準備 → 統合 |

## E11-2

閉鎖 (2026-10-07): `run/e11/b2_sizes.json (同一 toolchain の実測)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-2 | 結線後のkernel/SDK/shlibサイズを再実測し、§6のe枠・圧縮・8MB私有量を確認。撤去の減少を先取りしない。c3 で CRT (syscalls.o) が全バイナリ +348B (os32_ls、-ffunction-sections 無し) — 統合で計測 | 申し送り | 289–291、457–470、538、688、1240 | e11a/b/c → 統合 |

## E11-3

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-3 | 低位 Unicode/BB・共有 CLIENT USER、旧 PDE 準備・exec 共有 map を b2 で撤去。通常 map/unmap は共有 PT 無変更拒否、CLIENT/DISPLAY は lease 経由。実 PTE/PF・V86 は e11b2-isolation で PM 受入 | ホスト検証・統合ゲスト未実施 | 599、605–606、707、826、887–892、1006、1043、1573–1576、1762–1763 | e11a/b → 統合 |

## E11-4

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-4 | c3: C/Rust os32_ls 移行・CPL0 INVAL fallback実装、qemuホスト確認。PM: sh/CPL0 dir・補完・man/find/du/hsync・Rust filer を統合ゲストで受入、FD所有/授権flagを含む従来の統合関門も維持 | ホスト確認・統合ゲスト未確認 | 1501–1504、1546、1600、1694、1887 | e11b/c → 統合 |

## E11-6

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-6 | c2 の Unicode/両 gfx/待ち帰路に続き b2 は memory_layout=2 (KAPI70不変)・旧世代拒否・selftest 件数維持。全現行対象再ビルド、native 補完と日本語/boot/描画ゲストは e11b2-consumers/generation で PM 受入 | ホスト検証・統合ゲスト未実施 | 998–1002、1111、1156–1163、1274、1338、1361、1396、1429、1624 | e11a/b/c → 統合 |

## E11-7

閉鎖 (2026-10-07): `run/e11/c1_report.md (公開契約と生成の照合)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-7 | KAPI文書にSTALE/INVALの推測可能性とcallback/scheduling禁止を明記し、公開契約・生成物と照合 | 申し送り | 258、375–377、573、1004 | e11c → 統合 |

## E11-10

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-10 | b2 で E を kill (error7、0x6A000) へ反転。F の lease SURV/revoke kill は維持。ホスト確認後の実 PF/kill は e11b2-isolation で PM 受入 | ホスト検証・統合ゲスト未実施 | e9 / T2d〜h §2-5 | e11a/b → 統合 |

## E11-11

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-11 | db_v50_testに未貸与VRAM拒否を追加。e9の実RAM最終byte成功・guard越境拒否も維持して低位USER撤去後に確認 | 申し送り | e9 (2026-10-02 PM 決定) | e11b → 統合 |

## E11-13

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-13 | SHM lockで全ページRO・CPL3書込み拒否をゲスト確認。ホストの呼出し/結果判定だけで閉じずfree/exit後の次AS成功も対照 | 未実施の確認 | e9 R4 | e11b → 統合 |

## E11-14

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-14 | c3: shm_reuse_child/h2のページ・SHM長を公開定数へ移行。PM: shm_reuse_testとh2を統合ゲストで受入 | ホスト確認・統合ゲスト未確認 | e9 R5 | e11c → 統合 |

## E11-A1

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-A1 | c2 で USER lease 橋・互換 token 失効/再取得・ring3_guard F を結線。窓→全画面→復帰、日本語、F生存/revoke kill・旧E生存・native補完・予算の統合受入は PM (c2_report.md / guest_acceptance e11c2-*) | ホスト検証・ゲスト未配備 | e11a1 レビュー修正1 (2026-10-07) | e11c 切替と PM 統合受入、予算は公開前 |

## E11-B1

閉鎖 (2026-10-07): `accept_e11/RESULT.md (2026-10-07)`。

| ID | 何を | 種類 | 元の行 | 関門 |
| --- | --- | --- | --- | --- |
| E11-B1 | TVRAM/font低位USER撤去後の描画・日本語・CUI/GUI/WM TVDM、DB42件、SHM lockwrite先頭/末尾CPL3 PFと再利用、PT0・V86全出口3段監査・kselftest、native補完 | 未配備、qemuホストのみ (lockwriteは境界stubで制御フロー確認、CPL3保護の実効性未確認) | guest_acceptance e11b1-*、b1_results.json | b1準備確認 → c → b2 → e11統合受入 (PM) |

## E10-8

閉鎖 (2026-10-07): `run/e11/a1_fix1_report.md (pgalloc decode 範囲検査口)`。

| ID | 何を | 種類 | 元の行 |
| --- | --- | --- | --- |
| E10-8 | gfx が台帳のレコード (`ledger_resources[rid].map_*`) を直接書き換えている — pgalloc に範囲を検査して設定する口を作る | 改善 | e11a |

## E10-9

閉鎖 (2026-10-07): `run/e11/a1_fix1_report.md (初回 tag と polled 行)`。

| ID | 何を | 種類 | 元の行 |
| --- | --- | --- | --- |
| E10-9 | 監査 (launch・GUI 移譲・V86 帰路) の失敗が計数だけで表示されない — 最初の 1 回だけシリアル 1 行か tag を残す | 改善 | e11a |


## e12 の閉鎖

2026-10-07: X-12 は packed で blit_test 全体 / bench_scale2x Test 3 を SKIP、
X-13 は非配備の方針を維持して font_test の未配備を SKIP (終了 2) に修正。
実装・ホスト / ビルドの証拠は `run/e12/report.md`。ゲスト再確認は T2h の H-4 で行う。
X-10 は main 取り込み検査 `land_check2.log` (2026-10-07、rc=0) で受入済み。
X-2 の前半 (e8a の RO 日本語) は `accept_e11/RESULT.md` の GUI 日本語受入で閉鎖。
後半の font_load_test stat SKIP 理由は現行台帳に残す。

| ID | 閉鎖時の元の行 |
| --- | --- |
| X-12 | blit_test と bench_scale2x (Test 3) が 9801 の 4 プレーン前提で gfx_fb.planes[1..3] を読み、PEGC (1 プレーン) で NULL への書き込みで kill。e11 以前から同じで退行でない。非プレーン形式なら SKIP を出す。関門: T2h の構成試験の前 |
| X-13 | font_test の `/data/ipaexg.ttf` が NHD に無い。配備の対象に入れるか、試験が SKIP を出すか決める。関門: T2h の構成試験の前 |
| X-10 | 正常対照 21 本の省略と 6 本の実行順を修正し、段の包含を回帰試験化。取り込みから `make check-fast` を除去 → PM の統合 `make check` で受入 |
| X-2 (前半) | e8a の RO 拒否のゲスト確認 → e11 統合受入。 |

### f5a (2026-10-07、ホスト確認。ゲストは F-6)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| F-2 | 空 exec_heap の予約境界をまたぐ併合を禁止、shlib/lease/image/stack/guard の hint を INVAL。 | `tools/tests/appmem_host.c`、`/home/hight/os32-tmp/run/f5/report.md` |
| F-3 | remove_count 単独・空 PT SURFACE・PDE before PT free の負例と変異。部分 free 失敗の隔離・計数・中断要求はホスト確認、実 kill はゲスト F-6。 | `tools/tests/appmem_map_host.c`、同 report。ゲストは F-6 |

## f5b (2026-10-08)

| ID | 閉鎖した内容 | 証拠 |
|---|---|---|
| F-1 | 公開 USER 専用 mem_map/mem_unmap、KAPI 71・caller 接続・私有エラー翻訳を実装。ゲスト対照は F-6 に継承。 | `/home/hight/os32-tmp/run/f5/f5b_report.md` |

## f5b〜f9 のまとめゲスト受入 (2026-10-08、main 8d244dd)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| F-7 | f6 の USER CRT: sbrk_grow_test 397/397、kill+0・leftover 0、db 回帰 | `~/os32-tmp/evidence/2026-10-08/accept_f5b_f9/RESULT.md` |
| F-8 | f7/f8: malloc_arena_test 196658/196658、alloc_demo 19/19 (align 4096)、heap_test・db 回帰、kill+0・leftover 0 | 同上 |
| F-10 | f9: exec_heap_test 親子 PASS、resident exec_heap used 不変、heap_test の mem_alloc 64KB×19、hsync $?=0 | 同上 |

## f10 のゲスト受入 (2026-10-08、main 53fd67a)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| F-11 | f10: exec_heap_test 親子 PASS (65535/65536/65537・同 VA 再確保・親の LARGE 保持)、heap_test LARGE 29×64KB・重なり 0・retry 同数、回帰 (mem_map/sbrk/malloc_arena/alloc_demo/db/hsync) kill+0・leftover 0 | `~/os32-tmp/evidence/2026-10-08/accept_f10/RESULT.md` |

## f11 のゲスト受入 (2026-10-08、main 37976e7)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| F-13 | f11: malloc_arena_test 196664/196664 (trim → EXACT 再 map → 再 malloc)、回帰 (sbrk/heap/exec_heap/alloc_demo/db/mem_map/hsync) kill+0・leftover 0 | `~/os32-tmp/evidence/2026-10-08/accept_f11/RESULT.md` |

## f12 のゲスト受入 (2026-10-08、main d75e27f)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| F-5 | f12: 17MB で f12-startup (heap/sbrk_grow/exec_heap 親子/malloc_arena/mem_map)・f12-consumers (CUI 17 本 + GUI 7 本、kill+0・leftover 0、終了時 extent 最大 4/32・副 arena 最大 1)・f12-generation (世代 2 app 拒否・世代 3 起動)。kselftest 288/0。残りは F-14 | `~/os32-tmp/evidence/2026-10-08/accept_f12/RESULT.md` |

## 保守 X-14・X-15・X-16 (2026-10-09、main 8f4d27a)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| X-14 | test_ci_stab の子 report を一時ファイル + os.replace で公開。open 後にパイプで止める決定的な対照で旧形の空読みと新形の完全な公開を固定 | `~/os32-tmp/run/x14/x14_report.md`、全体検査 `~/os32-tmp/run/land_x/check.log` |
| X-15 | 新 check-kprintf-window: userland/sdk の .c・.inc・.h の kprintf を RING3_ARG_WINDOW 由来の上限 (14) と比べる。248 ファイル・1,143 呼出し、最大 12、非リテラル 0。Rust の kprint! と apps/game は対象外 (X-18) | `~/os32-tmp/run/x15/x15_fix1_report.md`、独立レビュー 2 回 (Approve) |
| X-16 | cdinst の main を int に (断り 0・失敗 1・成功 0、install.c の流儀)、cdinst_host の終了値対照と変異 2 本。ゲストの `$?` は次の install 受入で見る (F-14 と同じ機会) | `~/os32-tmp/run/x16/x16_report.md`、独立レビュー (Approve) |

## 保守 X-17・X-18 (2026-10-09、main a0ade40)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| X-17 | install の ERASE 断りを cdinst と同じく 0 (「Installation aborted. Nothing was written.」、[FAIL] なし)。install_fresh_host の case_erase (拒否入力 17 × 表 5)、cdinst の prepare/mkdir 失敗の対照、変異 4 本 | `~/os32-tmp/run/x17/x17_report.md`、全体検査 `~/os32-tmp/run/land_x17/check.log` |
| X-18 | check-kprintf-window に CPL3 の Rust (kprint!・kprint_attr!・(…kprintf)( 直呼び) を追加。140 ファイル・51 呼出し、最大 4。関数ポインタ経由・マクロの別名は対象外 (実物 0 件) | `~/os32-tmp/run/x18/x18_report.md`、独立レビュー (Approve) |

## T2g F-12 (2026-10-09、GK-3 の G-4、main b469f8f 配備)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| F-12 | exec_heap_user_trim の結線 (g1 の TRIM_DONE 横取り) をゲストで観測。17MB の GK-3 G-4 で、両 back が実 TRIM_DONE の後に ARENA1 の末尾返却 (`cur=88020000→88018000`)・空 ARENA の全返却 (`ARENA=2→1`)・`INITIAL=1→1`・`DATA=OK`。障害物を外した後の再伸長は cur +64KiB・ARENA=1・EXACT、終了行 hook=1・CRC=OK・regrow rc=0、終了後 free=2957・leftover=0。`mem <id>` と as_extents_at_teardown は未採取で fixture の自己観測で判断 (G-3 の成功の根拠には使わない)。native host runner は統合の全体検査 (native+qemu) で補った | `~/os32-tmp/evidence/2026-10-09/g23_gk/RESULT.md`・probe_log.txt・consink_G1_G4.txt |

## T2h R0〜R7 の閉鎖 (2026-10-13、h9 の照合、Fable 5.1)

受入の回: R0 (`evidence/2026-10-10/h_R0/RESULT.md`)、R1a/R1b (`2026-10-10/h_R1/RESULT.md`、再回 `RESULT_2.md`)、R1c (`2026-10-10/h_R1c/RESULT.md`、予行の再回 `2026-10-11/h_R1c_rehearsal/RESULT.md`)、R1d (`2026-10-10/h_R1d/RESULT.md`)、R3〜R6 (`2026-10-11/h_R3R6/RESULT.md`)、h9 再回 (`2026-10-11/h_h9/RESULT.md`)、8MB の g の残り (`2026-10-11/h_small/RESULT.md`)、R7 実機 (票 §5-1 の R7 段落、ノート `~/os32-r7/`)。
「部分閉」の行は、合格した部分だけをここに書き、残りは [DEFERRED_TESTS.md](../../tasks/DEFERRED_TESTS.md) の同じ ID の行 (縮めた形) に残る。元の行は閉鎖時のまま。

## PRIO-3 (部分閉)

閉鎖 (2026-10-13): シリアルの待ちの CTRL+STOP = `evidence/2026-10-10/h_R1/RESULT.md` (R1a-matrix: `audit_test serial` を画面キーボードから起動し CTRL+STOP で `$?=130`、cursor / fmch PASS)。GUI→CUI→GUI の往復自体は `2026-10-10/h_R1d/RESULT.md` と `2026-10-11/h_h9/RESULT.md` §1 で正常だが、TVRAM generation の値は読んでいない — その部分は現行台帳に残る。

| ID | 何を | 種類 | 関門 |
|---|---|---|---|
| PRIO-3 | シリアルの待ちの CTRL+STOP (`audit_test serial` — rshell の通信路とぶつかるのでキーボードから起動)、GUI→CUI→GUI の TVRAM generation。e11 受入ではローカル起動が必要で未実施 | 未実施の確認 | T2h 統合受入 |

## PRIO-1 (部分閉)

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R0/RESULT.md` — 新 NHD `os32_t2h_install.nhd` (200MB、派生 ini `t2h_install.ini`、FD 起動 + ISO) に `cdinst` Full: 検査 (C/H/S 3011/8/17、empty disk) → y → ext2 format・区画表・IPL/loader 検証・MINIMAL 18 / GUI 10 / NORMAL 101 / DEBUG 96 展開 → `Installation Complete`・`$?=0` (X-16 のゲスト `$?` もここで)、HDD 起動で API v74・kselftest 295/0・ext2 印 0、本 NHD hash 不変。残り (`install` の通し) は現行台帳。

| ID | 何を | 種類 | 関門 |
|---|---|---|---|
| PRIO-1 | install / cdinst の通しの実行 (授権が通り、区画・format・書込みまで) を別のディスクイメージで — 取り込み済みの DISK-AUTH の受入の残り。e11 受入では別イメージが必要で未実施 | 未実施の確認 | T2h の構成試験の前 |

## E9-2

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT_2.md` (R1b-h3: 6 mode とも `h3 case PASS`、identity の owner / gen が owner-start 行と一致)、`2026-10-11/h_h9/RESULT.md` §1 (identity: 自己公開の owner/generation = host identity、STOP → 再起動後の旧 case は `owner/generation/address changed` で拒否)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E9-2 | h3 の本人識別は接続済み、前景の証拠は h3fix (2026-10-10) で物理読みに替えて writer 不要。残りは R1b 再回の live で確かめるだけ | 未実施の確認 | 2605、2747–2893 |

## E9-3

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-e10: `audit_test tvram PASS (0 failures)`)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E9-3 | TVRAM 範囲外の呼び出しの拒否をゲストで (`6e4df78` はホスト試験だけ) | ホストのみ | 1432–1548 |

## E10-1

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-e10: `v86 -d <無い>` / `v86 -b <非画像>` とも rc=-1、owner-exit leftover 0、lease 0)、`RESULT_2.md` (V86 往復 + 表示アプリの前後で pt0_snapshot MATCH、v86_restore_mismatch 0)、`2026-10-10/h_R1c/RESULT.md` (hook 3 = K1: session 中の CPL0 例外 → kill app、leftover 0、次の `v86 -b` も正常)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E10-1 | V86 の `-d` / `-b` の失敗出口と K1 (session 中の CPL0 例外) のゲスト確認。正常・STOP 出口は e11 統合受入済み、故障画像と注入手段が要る | 未実施の確認 | e10c |

## E10-5

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1c/RESULT.md` (hook 4: `v86 -b` の end 中の再例外 → `[ring3] exception vec=6 EIP=0x156976 (r1_fixture_v86_end)` → `OS32: exec teardown stopped`、`exec_stop_count` 0→1、exec_pending_transfer で停止)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E10-5 | V86 session の end 中の再例外での停止の印 (シリアル 1 行と `exec_stop_count`) のゲスト確認 (ホストのみ、注入の手段が要る)。e11 受入の持越し理由: 故障画像と再例外の注入手段が要る | ホストのみ | T2h 統合受入 |

## E10-6

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-e10: `v86 -b /host/test/int80.img`、bp v86_session_end で linear 0x1FC40 = 0x3046 → IF (bit9) = 0、IOPL=3、ISR 実行済み)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E10-6 | V86 の INT 80h を反射する間 IF=0 のゲスト確認 (ホストのみ、INT 80h を出す V86 の画像が要る)。e11 受入の持越し理由: INT 80h を出す V86 の画像が要る | ホストのみ | T2h 統合受入 |

## E11-8

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-e11: pipe_owner_test PASS、子 id=3 も PASS、他 owner の長さ 0 は設計票 §1-2 の期待どおり、trusted の `ls / | cat` 正常)。

| ID | 何を | 種類 | 元の行 | 関門 |
|---|---|---|---|---|
| E11-8 | pipe_get_buf の kernel 番地返却・pipe_get_len の他 owner 照会の意味変更と範囲検査はホスト確認のみ。ゲストで本人成功・他 owner 拒否の対照を確認 | 契約接続待ち | 1502、1504、1546 | T2h 統合受入 |

## E11-9 (部分閉)

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT_2.md` (V86 往復 + gfx200_test の前後で PT0 MATCH、v86_return_audit_fail 0・memmap_audit_fail 0)、`2026-10-11/h_h9/RESULT.md` §1 (17MB PEGC: VRAM 系の装置フレームは master PT と全画面 AS で全て PCD=1・U=0、lease 帯 214 PTE は RAM の面で WB、装置の alias 0) と §3 (Cirrus: リニア窓 512 PTE PCD=1・U=0・RW=1)。CUI の tvdump は受入済み。残り (WM / GUI 端末の tvdump 0 埋め) は現行台帳と H-7。

| ID | 何を | 種類 | 元の行 | 関門 |
|---|---|---|---|---|
| E11-9 | CUI の tvdump は受入済み。GUI 端末と WM の 0 埋めのゲスト確認 (ホストのみ)、V86 後の DISPLAY/TVRAM 再 lease と全 alias UC の明示観測 (e12-17mb-pegc-v86-release) | 申し送り | e9 (2026-10-02 PM 決定) | T2h 統合受入 |

## E11-12

閉鎖 (2026-10-13): E9-2 と同じ証拠 (`evidence/2026-10-10/h_R1/RESULT_2.md` R1b-h3、`2026-10-11/h_h9/RESULT.md` §1 identity) — h3a/h3b の実ゲストで自己公開と host identity が一致、CRT 非依存の marker (物理読み) で受入。

| ID | 何を | 種類 | 元の行 | 関門 |
|---|---|---|---|---|
| E11-12 | c3: h3 caller_identity自己公開とhost identity()照合を接続、qemuホスト確認。PM: h3a/b実ゲスト・CRT非依存marker受入 | ホスト確認・統合ゲスト未確認 | E9-2 / e9 | T2h の h3 (本人識別のゲスト照合は h3 の手順一式で) |

## E11-BUD

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT_2.md` (E11-BUD 記録: kheap_peak boot 3,784B → GUI・回帰後 134,872B (GUI 中の `kmalloc_peak_bytes` の物理読みと同値) → GUI 終了後も 134,872B、KHEAP 180,224B に対し余り 45,352B、resident_peak 131,656・fail 0)。決定は設計票 U5 (ユーザー決定 2026-10-10): **KHEAP 176KiB 維持**、実機の peak は `mem` で。測定と決定で閉じる (PASS ではなく記録)。

| ID | 何を | 種類 | 元の行 | 関門 |
|---|---|---|---|---|
| E11-BUD | kmalloc_peak_bytes は exec ヒープの確保も数える (kheap_alloc が全 KHeap 共通で更新) — GUI 後 1,286,752B。カーネルヒープの使用は `mem` で 2,544/180,224B。ピークはカーネルヒープだけに直した (e12)、測り直しは統合ゲストで、KHEAP を戻すか決める。b2 実測は b2_sizes.json | ホスト検証・統合ゲスト未実施 | `c1_sizes.json` | T2h 前 |

## E11-A2 (本体閉、E11-A2b を分割して現行台帳へ)

閉鎖 (2026-10-13): 本人識別 = E9-2 の証拠。attach 失敗の注入 = `evidence/2026-10-10/h_R1c/RESULT.md` (hook 6: about の初回 attach 失敗 → 次の attach で成功し窓は完全に描かれた)。非 owner shutdown = `2026-10-10/h_R1/RESULT.md` (R1b-shutdown: `shutdown_probe: PASS`、backend・surface・screen owner 不変。正規 owner の対照は未実施)。raw KAPI 待機利用者の明示 check は E11-A2b (渡す、T4)。全画面入力 4 点は E11-5 に集約。

| ID | 何を | 種類 | 元の行 | 関門 |
|---|---|---|---|---|
| E11-A2 | h3 本人識別のゲスト照合は h3 の手順一式で。attach 失敗の注入・非 owner shutdown・raw KAPI 待機利用者の明示 check はホストのみ。全画面入力 4 点は E11-5 に集約、日本語は受入済み | ホスト確認・統合ゲスト未確認 | e11a2 / guest_acceptance e11a2-* | T2h の h3 |

## E11-PERF

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT_2.md` (R1b-perf: bench_scale2x Test 1 = 320x200 40ms/100・640x400 10ms/100 で accept_e11 の基準と同値)、実機は票 §5-1 R7a (Test1/2 が更新前と ±10% 内)。present ごとの query の前後比較は基準が無く**測定のみ、合格判定ではない** (設計の期待どおり、PM 判断 2026-10-13)。比較値を採る目的は果たした (以後の基準は R1b の値)。

| ID | 何を | 種類 | 元の行 | 関門 |
|---|---|---|---|---|
| E11-PERF | present ごとの query の前後比較は基準値が無く未実施。bench_scale2x Test 1 の基準は 320x200 40ms/100、640x400 10ms/100 (accept_e11/RESULT.md)。比較値を採取する | 未実施の確認 | E11-1 の比較残件 | T2h 統合受入 |

## F-6

閉鎖 (2026-10-13): 17MB = `evidence/2026-10-10/h_R1/RESULT.md` (R1a-f6: mem_map_test の invalid / control PASS、R1a-h2: h2_stack512 `stack=524288 pages=104`・guard err=6、kstack high-water は `mem` の `kstack=` 欄で記録)、8MB = `2026-10-11/h_R3R6/RESULT.md` R4 (mem_map_test 2 PASS、free 987 復帰)、毒 AS の kill = `2026-10-10/h_R1c/RESULT.md` (hook 5a: 生存親が子の後 USER へ戻らず syscall 出口で kill、leftover = pages 100 (P3-c); hook 5b: PARKED の resume で kill、leftover 134 (P3-d); 次起動は新 owner で正常)。leftover==0 は全回の owner-exit 行。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| F-6 | f の受入一式の残り (f5b-mem-map の対照と pf は 2026-10-08 に合格、`~/os32-tmp/evidence/2026-10-08/accept_f5b_f9/`): 8MB / 17MB での伸長と unmap、512KiB stack、kernel stack high-water (f5a の AS +532B / map pending 256B、lease_selftest 1,676→2,732B (AS 2 個)・test_ledger 812→1,340B・test_appmem 1,232B を含む)、leftover==0、毒 AS の kill (syscall 帰路と resume)。f5a レビュー: 入れ子で親 A が毒の後に子の終了で A の PD が一時的に CR3 に載る (USER へは戻らず syscall 出口で kill — P3-c)、resume の中断も h3 の `syscall_abort` 地点に当たる (P3-d) — ゲストの h3 の数え方で確かめる | 未実施の確認 | 2369 |

## H-1 (部分閉)

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-h2: fixture 15 本の sha256 が manifest と一致、control-app entry +1、old-app / unknown-format / old-cpl0-flag は `unknown format or flags`、bad-abi / bad-memory / prev-generation は `generation or KAPI layout mismatch` で entry +0、guard `addr=8ff7f000 err=0x6`、owner-exit leftover 0)、`2026-10-10/h_R1c/RESULT.md` (R1c-2: 旧 shell は boot FATAL・entry 0、旧 shlib は `g_reject=1`・依存 app entry 0・CUI fallback、各 restore で verify-set 一致; R1c-1: 故障画像 `kernel-r1` の hook 7 本と製品 hash 不変)。残り (h2_stack512 の park/resume) は現行台帳。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-1 | h2: fixture の再生成と hash 照合、隔離媒体で旧 app / shell / shlib を拒否、owner 回収、park、guard の error=6 とカウンタ、故障画像・旧 shell の試験 | 未実施の確認 | 2463–2509、2909、2914 |

## H-2 (部分閉)

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT_2.md` (R1b-h3: h3fix 後の道具で pf / gp / de / ud / USER-loop / KAPI-loop の 6 本とも `h3 case PASS (PM trace + physical diagnostics + next launch)`、verify、h3b への交換、PARKED の arm (先に別窓を前景にして fixture を PARKED にしてから)、KAPI-loop は wm-kill・pending 0 = park 中の WM kill、loop-watch / 単独 stop / trace-watch の live、各 owner-exit leftover 0)。**閉じていない**: 実行中 OP_WAIT (RUNNING+in_wait) の arm は 1 回目 (pf) で `resume not captured`・arm=1 残り (失敗の記録、RESULT_2 R1b-h3 の「残る道具の不足」)、打鍵を重ねた STOP・前景以外の張本人は PASS の証拠が無い — 3 点は現行台帳。


| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-2 | h3: 台本経由の KAPI-loop、pf / gp / de / ud、verify、h3b への交換、PARKED の arm、実行中 OP_WAIT の arm、park 中の WM kill、打鍵を重ねた STOP、前景以外の張本人。h3fix4 の後のゲスト再実行。loop-watch / 単独 stop / trace-watch の live 実行 | 未実施の確認 | 2692–2830、2894–2905、3059–3069、3082、3365–3368 |

## H-3

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT_2.md` (R1b-h3 の KAPI-loop: wm-kill・pending 0、slot 全 0、owner-exit leftover 0 = ページ返却; R1b-stop の 3 試行で reclaim / stop_park の増分と slot 0)。「FIRING の後 約 1 秒は STOP が効かない」と制限 3 件は既知の制限として記録 (試験ではない)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-3 | GUI KAPI-loop の STOP: ホストだけで見た分岐群、ページ返却の観測、FIRING の後 約 1 秒は STOP が効かない、制限 3 件 | ホストのみ / 既知の制限 | 3218–3262、3278、3305、3367 |

## H-4 (部分閉)

閉鎖 (2026-10-13、構成 × 項目の対応表。ここに無い項目は現行台帳の H-4 に残る):
- 17MB planar (`evidence/2026-10-10/h_R1d/RESULT.md` + `2026-10-11/h_h9/RESULT.md` §2): guard A/B/E、db、kselftest 294/0 (記録)、gfx200_test 20 回・gfx_demo200、blit_test 11/11・bench_scale2x 全 Test (S-7)、GUI (gdi_test・gui_demo・STOP・GUI/CUI 往復)、全画面 DISPLAY と窓への復帰、SHM lock 4/4、font_load_test、旧 token 失効と両 SDK 再 attach。
- cirrus-off (`h_R1d`): `gfxmode cirrus` が pc98 planar へ fallback、gfx200_test・blit_test・db_test、drawing_failures 0・stale_lease_faults 0。
- 17MB Cirrus (`2026-10-11/h_R3R6/RESULT.md` R3 + `h_h9` §3): hal_test cirrus、guard A/B/C、db、blit_test ×20 (packed SKIP の往復) leftover 0、V86 往復後も cirrus、gdi_test (WAB 中継)、alias UC (リニア窓 512 PTE PCD=1・U=0)。
- 8MB planar (`h_R3R6` R4 + `h_h9` §4 + `2026-10-11/h_small/RESULT.md`): guard A/B、db、mem_map_test、hal_test ×20・blit_test 8 回、shapes i/ii/iii (final 512、P1=5) と非数 P1 の `PREP FAIL` $?=1 (入口前 ENOMEM は未実施)、**台本 M の全通し** (高位 HostDrv・親子・map・fault・V86・GUI・trim・STOP・全画面・FEP・次起動)、g の 8MB 台本 G-1〜G-9・R-1〜R-4 全部 PASS (G-8 = pool 0 から閉じる)、G-9 の CUI 回帰 (mem_map / sbrk_grow / malloc_arena / exec_heap / alloc_demo / db / hsync)。`e12-8mb-planar` の残り (V86 後 alias UC・SHM lock・font_load_test NOSYS 等) は未実施 → 現行台帳。
- 8MB PEGC (`h_R3R6` R5 + `h_h9` §5): guard A/B、db、shapes i/ii/iii の成功例だけ (非数 P1・入口前 ENOMEM は未実施 → 現行台帳)、hal_test ×20 leftover 0、G-1〜G-4 (pages +18)、guard D の保護分岐 err=7 (R4 の err=6 と両分岐)。
- 64MB + 音源 (`h_R3R6` R6 + `h_h9` §6): 66MB ONLINE・kselftest 295/0、`[pcm] CS4231 v=100` (SNDboard=64 の期待)、HostDrv 往復 790KB cmp 一致、親子 (shm_reuse_test 4/4・pipe_owner_test)、workspace の固定 10 枚 (sup/RW/WB/非配布) と 32MB 超の恒等写像の PTE、pcm_test 5 秒完走 → STOP → 次 open 成功、IMR の変化 (0xf3) と STOP 後の再マスク (0xf7)。
- 共通: 派生 ini t2h_cirrus / t2h_8mb / t2h_64mb_118 (原本 ini・本 NHD は不変)、各回 `gfxmode` 明示 → 読み戻し → reset → hal_test 一致。


| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-4 | **構成を変える試験の一括** (全段が 17MB・今の ini だけで受入した): 8MB、planar / PEGC / Cirrus の切替、音源 (PC-9801-118 の PCM)、Ra266 64MB。`gfx200_test` / `gfx_demo200`、e12 の構成別台本 (guest_acceptance.yaml の e12-8mb-* / e12-17mb-planar / e12-17mb-cirrus / e12-cirrus-off)、f12-8mb-startup (status deferred。f12の8MB証拠はK7ホスト94/768pageのみ、実ゲストはこの構成関門でPMが実行) | 構成持越し | 278、418、557、894、897、1009、1112、1276、1340、1431、1601、1887、2908、3364 |

## H-5 (部分閉)

閉鎖 (2026-10-13): 票 §5-1 R7a (2026-10-12、実機 Ra266 9545e27: 更新前に #PF で落ちた bench_scale2x / blit_test が正常、Test1/2 が更新前と ±10% 内 = UC 化で present が遅くならない)。残り (CG 窓の WB・後始末・kstack の転記) は現行台帳。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-5 | 実機 Ra266: UC 化で present が遅くならないかの計測と CG 窓の WB、表示の後始末 (GRCG / EGC・68h の残り)、kernel stack の high-water | 構成持越し (実機) | 709–712、790–793、2253、2338 |

## H-6 (H-6a 閉、H-6b を分割して現行台帳へ)

閉鎖 (2026-10-13): H-6a = `evidence/2026-10-10/h_R1/RESULT_2.md` (`v86 -b` / `v86 -d` / hal_test の前後で /api/gdc mode2 (6Ah) = 0x01・mode1 = 0x88・text 表示復帰が不変、phys E0000h / A8000h の読みも不変 — 記録)。6Ah の復元の未対処と 9801 構成の `gui_gate` は H-6b (渡す、T7)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-6 | V86 の出口で 6Ah の標準 / 拡張を戻していない (9821 で E0000h が MMIO のまま残り得る)。`v86 -d` / `-b` の後の表示確認、9801 構成での `gui_gate` | 未対処・未観測 | 788–789、3503 |

## H-8

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-guard: A `addr=0x8FFBF000 err=6` = 256KiB 底直下; 可変 stack は h2_stack512 の guard `addr=8ff7f000 err=0x6` = 0x90000000−512KiB−1page、設計票 §1-2 の期待どおり)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-8 | `ring3_guard` A の固定 target (`MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE`) は T2c 可変スタックで実 stack 直下と一致しないことがある。h 受入で A が実 stack 直下 NP を指すことを確認 | 未実施の確認 | e9 R6 |

## H-9

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-guard: B `addr=0x80000000 err=7 [shlib band, WRITE]`、shlib 読込み済み、h4a の `err=` 欄)。planar / Cirrus / 8MB でも同値 (`h_R1d`、`h_R3R6`)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-9 | `ring3_guard` B (shlib 帯) を shlib を読み込んだ AS で走らせ、RO の error=7 を確かめる (2026-10-06 の受入は未ロードで、帯の fault だけを確認。今のシリアル行は error_code を出さない) | 未実施の確認 | — |

## X-2

閉鎖 (2026-10-13): `evidence/2026-10-11/h_h9/RESULT.md` §2 (font_load_test: stat SKIP ではなく NOSYS の場合を実行、`result: -10`・`PASS 1/1`。「stat 段の SKIP」は今の版では出ない)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| X-2 | `font_load_test` の stat 段の SKIP 理由 → 次にその試験を回す前に確定 | 未実施の確認 / SKIP | 1250、1294 |

## X-21 (部分閉)

閉鎖 (2026-10-13): (b) だけ — `tools/tests/guest_acceptance.yaml` の g-R-4 は `hook_stop_park_delta: 1` / `done_stop_park_delta: 0` に試行別で書き分け済み (h1b)、`evidence/2026-10-10/h_R1/RESULT_2.md` R1b-stop と `2026-10-11/h_small/RESULT.md` R-4 で両試行 PASS。(a) G-4 / R-3 の trim 対象の `mem <id>` と teardown extent は**未採取** (h9 再回 §1 の as_extents は gui_demo で道具の経路を確かめただけ、台本 :1893 の対象ではない) — 現行台帳に残る。


| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| X-21 | T2g のゲスト受入の残り。**GK-3 (2026-10-09、063c515) と GK-4 R-1〜R-4 (2026-10-10、0ecaf98) は合格** (証拠 `~/os32-tmp/evidence/2026-10-09/g_x21/`・`2026-10-10/gk4/RESULT.md`)。残り: (a) G-4/R-3 の `mem <id>` と `as_extents_at_teardown` は未採取 (GUI 中に rshell が無い。fixture の自己観測で代用) — CUI から GUI アプリの extent を採る手段を作るときに、(b) 台本 g-R-4 の `stop_park_delta: 1` は hook 中の試行の期待。DONE 直後の STOP は back が自分の OP_WAIT 中で park を通らず +0 が設計どおり (`exec/appslot.c:521`) — 台本の期待を試行別に書き分ける (次に台本を触る段で) | 台本の整理 / 未採取 | — |

## S-1 (部分閉)

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R0/RESULT.md` (h-R0-winoptin: `test_np21w_trial.py --windows-parser` 31 本 OK で通常 skipped の 1 件を実行、`test_np21w_ini_live.py --windows-fixtures` 89 本 OK で通常 skipped の 4 件を実行 = 5 件 PASS)。集約 9 件のうち残り 4 件の所在は現行台帳。

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-1 | Windows opt-in のホスト試験 (全段で 5 件、集約では 9 件) | WSL から Windows 側の道具を既定で起動しない | 環境 | T2h 統合受入の前に 1 回、opt-in で |

## S-2

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-e11: `/etc/profile` 配置後 kout_test `0 skip(s)`、exit 0)。

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-2 | `kout_test` の 1 件 | ゲストに `/etc/profile` が無い | **環境不足** (合格ではない) | 次のゲスト受入で `/etc/profile` を置いて回す |

## S-3

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT.md` (R1a-e11: kout_test 2d / 3c は `N/A` と出る、h3b で試験側を修正)。

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-3 | `kout_test` の 2d / 3c | 設計上の適用外 | 適用外 | — (延期ではない。試験側で SKIP ではなく N/A と出すようにする) |

## S-7

閉鎖 (2026-10-13): planar = `evidence/2026-10-10/h_R1d/RESULT.md` (blit_test 正当性 11/11・`PASS 1/1`、bench_scale2x 全 Test 完走、Test 3 = 5 ticks)、PEGC の SKIP と他の計測の継続 = `2026-10-10/h_R1/RESULT_2.md` (blit_test `SKIP packed framebuffer` $?=2、bench_scale2x Test 3 SKIP で Test 1/2/4〜7 は計測)。Cirrus (packed) でも同じ SKIP (`h_R3R6` R3)。

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-7 | blit_test 全体 / bench_scale2x Test 3 | packed は planar 4 面試験の適用外 | 適用外 (PASS に数えない) | H-4 planar 構成で実行、PEGC は SKIP と他の計測の継続を確認 |

## S-8

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT_2.md` (R1b-bud: TTF 無しで `SKIP font not deployed` $?=2 (合格に数えない)、/data に ipaexg.ttf・ipaexm.ttf を置いて `$?=0`、終了後に削除して非配備に戻した。描画の目視は screenshot API が 98 面だけで PEGC 画面を採れず未確認)。

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-8 | font_test | /data/ipaexg.ttf は意図的に非配備 | 環境不足 | H-4 前に TTF を置いて実行、未配備は終了 2 と SKIP 行を確認。SKIP 行の `os32api::print` (kprintf) はリダイレクトを素通りする既知の制限 (Rust の fd1 helper ができるまで) |

## S-5

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1/RESULT_2.md` (R1a-matrix: e2test Test 3/4 で 372,632B ×2 (1 回書き・64KB 分割) を書いて読み戻し一致、owner-exit pages=340 leftover 0 — 「372KB×2 が取れない」は f の exec ヒープで解消)。

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-5 | e2test | 372KB × 2 のバッファが取れない | 環境の前提不足 | exec ヒープの上限を見直す f の段 |

## S-6

閉鎖 (2026-10-13): `evidence/2026-10-10/h_R1c/RESULT.md` (S-6: 原本 ini に `NP2NETSOCK=127.0.0.1:8026` の鍵あり、`tools/host_agent.py --listen` を起動して隔離構成で host_test `PASS 26/26`。agent 再起動の stale ケース (`host_test stale`) は未実施)。

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-6 | host_test | host_agent 不在 | 環境の前提不足 | T2h の Host Services の受入 |

## F-14 (台帳からは 2026-10-10 に消した行の追記)

閉鎖 (2026-10-10、a96a3a2): `evidence/2026-10-10/f14_filer/RESULT.md` (filer で /tmp/f14 の 300 件: PAGEDOWN 7 回で各頁 15 行が窓に収まり継ぎ目の欠け・重複なし、F5 → HOME でも同一。`MAX_ENTRIES=96` の `[truncated list]` は v1.2 CONTRACTS.md:205 の契約どおり。前後 kills 0・leftover 0)。先行分は `2026-10-09/` の f13_acc・f13_f14_shlib・f13_f14_installer。

| ID | 何を | 種類 | 関門 |
|---|---|---|---|
| F-14 | f12 受入の残り。2026-10-09 に resident・Rust filer (一覧/展開/F5)・generation-shlib・installer-cancel (install / cdinst とも FD 起動で確認画面まで進め n、NHD 全体のハッシュ不変) をゲストで観測 (`~/os32-tmp/evidence/2026-10-09/` の f13_acc・f13_f14_shlib・f13_f14_installer の RESULT.md)。残りは filer の数百件で窓境界から欠けないことだけ | ゲスト一部未受入 | f12 |

## X-19 (台帳からは 2026-10-10 に消した行の追記)

閉鎖 (2026-10-10、042bcab): `evidence/2026-10-10/x19_mcp/RESULT.md` (Claude Code 再起動後の MCP で `emu_gui_launch` が gui_demo / trim_back に ok:true と `since` を返し、`emu_wait_text ... since=` で起動中の行を拾い、`emu_type seq=Q hold=1500` で 1 回だけ終了)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| X-19 | GUI 受入の道具の live 確認。**2026-10-09 に state/screen_text/consink/wait_*/type/mouse、2026-10-10 に gtool3 の `Gui.launch` (12/12 ok:true)・`since` を wait_text に渡す形・`type(hold=)` を gui.py の直呼びで合格**。残り: **MCP 経由 (`emu_gui_launch`・`emu_type` の hold) の 1 回** — MCP サーバが旧コードのまま (Claude Code の再起動で読み込まれる)。次にセッションを立てたときに確かめる | 未実施の確認 | — |

## X-3 (台帳からは 2026-10-11 に消した行の追記)

閉鎖 (2026-10-11、3204b6d): 票 §5-1「X-3 を閉じた (2026-10-11)」— GitHub Actions の check が f7/f8 (2026-10-07) 以来 failure (CI のクロス無しで make dry-run が newlib の libc.a を要求)。ci-fix (d3c74ff) の push 後に check・build とも success。過去の run は合格にしない。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| X-3 | GitHub Actions の結果が票に無い。**2026-10-11 に照合: check (static-check の check-arch-asm) が f7/f8 (2026-10-07、f60a734) 以来ずっと failure** — CI はクロス無し (`OS32_CLANG_CROSS_FREE=1`) で、make dry-run が newlib の libc.a と i386-elf-gcc を要求するようになったと推定。build は success。→ ci-fix 段で直し、push 後の CI で確かめて閉じる。今の成功で過去を合格にしない | 未実施の確認 | 335–345、2003–2009 |

## H-10 (台帳からは 2026-10-10 に消した行の追記)

閉鎖 (2026-10-10、7bc7cfb): 票 §5-1「L-h 着地」(H-10 は h0t で閉じた)、`run/h/h0t/PACK_h0t.md`。実使用は R3〜R6 の派生 ini (`np21w_ini.py derive ... SNDboard 8→64`、`evidence/2026-10-11/h_R3R6/RESULT.md`)。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-10 | 音源ボード (SNDboard) の ini キーを `np21w_ini_live.py` 系が扱えない (emu-config §1 の対応キー外) → 道具の拡張 (sol) を h の音源構成の準備前に | 道具の不足 | — |

## GSERIAL-1

閉鎖 (2026-10-14、63b5b06): NP21/W の受入 (`~/os32-tmp/evidence/2026-10-14/gserial_np21/RESULT.md`) と実機 Ra266 d14a (ノート `~/os32-r7/d14a_*.txt`) で合格 — `gs:! ready`、ping・state・type・key (sent=4 lost=0)、`cui` の 2 段応答で CUI に戻り rshell が応答、`-t` の自動復帰 (NP21/W)、`sfs run os32gui` の拒否 (実機)。拒否の文言が逆に読める件は smallfix で直す。GUI=1 の直接起動 (serial 未初期化) は次段の票で扱う。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| GSERIAL-1 | GUI 中のシリアル受け口 (gshell の gserial、票 `~/os32-tmp/run/gserial/GSERIAL_DESIGN.r3a.md`) の実機 Ra266 での確認: rshell → `os32gui` で `gs:! ready`、`ping`・`state`・`key`/`type`・`cui` の 2 段応答で CUI に戻り rshell が再び応答する、`os32gui -t` の自動復帰。GUI=1 直接起動 (serial 未初期化) は次段。配送は best effort で `state lost=` に破棄件数 (保証外 3 件は票 §6-8) | 実機の受入 | — |

## GSERIAL-2

閉鎖 (2026-10-15、d702b51): 引数なしの `os32gui` は `GSERIAL_TIMEOUT` を 0 に戻してから GUI に入る (書けなくても入る)。ホスト試験 (`sh_status_host.c` の 3 条件、変異 1 本) と NP21/W (cfg に 60 が残った状態から `os32gui` → wd=0、cfg も 0。`~/os32-tmp/evidence/2026-10-15/dbgref_g1/RESULT.md`) で合格。実機では次の立会いで使うときに見る。

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| GSERIAL-2 | `os32gui -t N` の秒数が `/etc/system.cfg` の `GSERIAL_TIMEOUT` に残り、次の GUI 入場でも自動復帰が効く (d14c で 2 回、手で `-t 0` に戻した)。その回だけ効かせるか、自動復帰の後に消す | 道具の不足 (運用) | R7-1 の修正と同じ回の小さな直し |

## LB-R5-1

閉鎖 (2026-10-15、ee469f3): LAN LB-1 で R5 だけの診断 (整形・begin/end マーカー・64 語のダンプと u16[64]) を外し、EEPROM は幅 6 固定で語 0〜2 だけを読む形にした。Fable のレビューで「残すべきもの (窓の検査・EEPROM の読み・T/S の試験) を落としていない」ことを確認。

| ID | 何を残したか | 理由 | 渡し先 |
|---|---|---|---|
| LB-R5-1 | R5 の診断 (観測行・最終行の整形、begin/end マーカー、先頭 64 語のダンプと u16[64]) を本体から外し、EEPROM の語バッファを MAC 用の 3 語に縮める (約 1.5 KB、astra の内訳 `~/os32-tmp/run/lan/astra_impl.log`) | R5 は実機で 1 回採れば用が済む。本番に残すと予算を使う | LAN L-B の本番の依頼パック (R5 の実機の結果が出た後) |

閉鎖 (2026-10-16): LAN LB-2b・LB-2c の受入待ち。実機の受入は LB-2c (PCR→HELLO の線上受信) と LB-3 (`~/os32-tmp/evidence/2026-10-15/lb3_realhw/`)、LAN 版 /host の実機 (`~/os32-tmp/evidence/2026-10-16/lanfs_realhw/RESULT.md`) で済んだ。NP21/W の非搭載起動は main cd1e8dd で PASS (`~/os32-tmp/evidence/2026-10-16/lf_np21w/RESULT.md`、LF-NP21W も同時に閉じる)。LB-2b・LB-2c のレビューの P3 7 件 (`~/os32-tmp/run/lan/P3_CARRYOVER_LB2b_LB2c.md`) は LB-3 の書き直しで解消済み (幅の受け渡し `drivers/e82557.c:304`、`- mdi=` の表示 `:365`、模型の PCR read/write の timeout `tools/tests/e82557_drv_host.c:683-686`、ケースごとの待ちの上限 `:763`)。PHYVENDOR=0 の対照だけを PHYVENDOR-1 として台帳に残す。F3 (import の死荷重) は DBGREF S3 の依頼に入れた。

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| LAN-LB2b | Ra266のMDI/PHY、統計good1/good2、tcpdumpの#1/#2、KDEBUG=1の新4段、NP21/Wの非搭載起動、native host runner | 依頼パックは実装とqemu host検査まで。実機・エミュレータ操作を含まない | PMのLB-2b受入 (実装証拠: `/home/hight/os32-tmp/run/lb2b/`) |
| LAN-LB2c | Ra266 の PCR・HELLO 1 本の線上受信、PHYVENDOR=0 対照、NP21/W 非搭載起動・DBGREF/gserial/rshell、native runner | 依頼は実装と qemu host 検査まで。実機・エミュレータ操作禁止 | PM の LB-2c 受入 (実装証拠: `/home/hight/os32-tmp/run/lb2c/`) |

閉鎖 (2026-10-16、実機 Ra266 で ff6f372、証拠 `~/os32-tmp/evidence/2026-10-16/lanfs1_fix/`): LANFS-1 — 開始で受信の環を空けて RU の ready を待つ直し (ff6f372) で、空き 5 分・15 分の後の初回とも requests=2 resent=0 timeouts=0 (直す前は両方 resent=1)、pcap も HELLO 1 本。終了行の rnr は 0: 環が埋まって RU が止まるのは空いている間 (セッションの前) なので、開始時を基準にしたセッション中の差は構造上 0 になる (PM の依頼の作りの誤り)。「RU が止まっていた」は観測では裏付けておらず [推]、開始時に環を空けたことが効いた、までが事実。PHYVENDOR-1 — `E82557_PHYVENDOR=0` で `pcr=off`、LB3=PASS・bound のまま、`sfs run ls /host` は no answer to LAN HELLO (resent=3 timeouts=4、11.4 秒で有限に失敗)、ノートの pcap に HELLO が 1 本も来ない (送信が線に出ない)。戻すと `pcr=8060->8562`、ls は exit=0 resent=0。PCR の初期化 (6045a6e) が送信の不具合の原因だったことの対照が取れた。

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| LANFS-1 | lanfs_host.py の最初のセッションで resent=1 timeouts=1 (2026-10-16、段 3 の初回と段 4 の -n boot)。ノートの観測 (06:30): サーバを起動し直さずに約 4 時間空けたあとの初回でも同じ → 条件は「サーバの初回」ではなく「しばらく空いたあとの初回」[推]。ノートの NIC の省電力 (EEE・ASPM) か、ゲストの受信の初回が候補 | 実害は 1 要求の再送 (約 2.6 秒) だけ | ノートで切り分け中 (空き 1・5・15 分 × tcpdump、`~/os32-tmp/evidence/2026-10-16/lanfs1/`) |
| PHYVENDOR-1 | `E82557_PHYVENDOR=0` (DP83840A の PCR を書かない) で HELLO が線に出ないことの対照。PCR の初期化 (6045a6e) が送信の不具合の原因だったことの裏付け | LB-2c の受入では PCR=1 の合格だけを採った。対照は実機で設定を 1 行変えて戻すだけ | LANFS-1 の切り分けの後、ノートの担当で (画面不要・立会い不要) |

閉鎖 (2026-10-16): R7-1 — ユーザーが実機 Ra266 で 31k PEGC の GUI (`os32gui`) の表示を目視して合格 (ff6f372)。直しは 10/15 の R7FIX2 段 3b (1ab26d8)。同じ立会いで別件のマウスの止まり (GUISTALL-1) と filer の 2 回目の起動の不具合 (71169fc で直した) が見つかった。

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| R7-1 | GFX=pegc のまま 24kHz で起動すると、カーネルが PEGC 640x480 (31kHz 要) を選び GUI が真っ暗 (起動ログ `[pegc] hsync=24k`。10/12 に「gshell は exc=0 で即時終了」としたのは別アプリの owner-exit の読み違えで撤回、10/13)。`gfxmode pc98` + GRPH+1 (24kHz) で起動すれば GUI 正常。表示方式の選択が今の水平同期を見ていない — 24kHz なら PEGC 480 を選ばない (planar へ落とす) か、31kHz へ切り替えてから入るかをハード資料で決めて直す。31kHz 起動 (09a8=81) でも 9545e27 では `gshell: cfg OK` の後に応答が無く、24k 専用の問題とは限らない。観測用の計装 (L1〜L8、2f99a78) は実機に配備済み (10/13)。**10/15 d14g (1ab26d8、R7FIX2 段 3b 着地後)**: 原因確定 (16MB システム空間「切り離す」= 043Bh bit2=1 で F00000h が RAM) と直しの実機確認 — `[pegc] win=fff00000 sys16m=1 f00=skip fff=ok force=0 disp=ok`、GUI 入場で `[pegc] alias=ok` (FFF00000h はバンク窓 A8000h と同じ VRAM)、selftest 295/0、fault 0。**残りはユーザーの画面目視 1 回だけ** (31k の PEGC GUI)。**10/14 の立会い d14c (63b5b06)**: 原因は周波数でもモニタでもなく **Ra266 で PEGC の描画が表示に乗らない** — 24k PEGC はモニタが 640x480 31k に同期したまま真っ暗、31k PEGC もグラデーションだけ/真っ暗で UI が出ない、31k `gfxmode pc98` は正常 (写真・採取は `~/os32-tmp/run/crtfreq/realhw/`)。24k の真っ暗は io_disp 09A8h「切替後は GDC の SYNC を設定し直す」で説明できる。修正票 R7FIX2 (Fable 起草中、`~/os32-tmp/run/r7fix/`): 31k を主経路に PEGC の列を実機 BIOS に合わせる、24k は pc98 に落とす (既定 31k はユーザー決定)。**10/14 (63b5b06、実機 d14a)**: 24k 起動でも GUI 入場で 09a8=81 に切り替わり、`[gfx] init be=pegc-256`・first present・`[gshell] loop`・`gs:! ready` まで進み、gserial のキー注入も通った (ノート `~/os32-r7/d14a_*.txt`) — ソフトは止まっていない。残るのは画面に映るかの目視だけ (真っ暗ならモニタの 31k 追従など表示信号の側)。**カーネル層の不具合 — T3 より先に直す候補 ([POLICY_DEV §1](../../POLICY_DEV.md))、順序は反省会で (LAN 転送と合わせて)**。周波数は起動時の GRPH+1 (24k) / GRPH+2 (31k) で切り替わる | 実機の不具合 (カーネル層) | 修正の段 → 次のゲスト受入 + 次の実機セッション (24kHz での起動は画面確認が要る) |

閉鎖 (2026-10-16、NP21/W 64MB で 7c914fd と 6d5549f、証拠 `~/os32-tmp/evidence/2026-10-16/guest8/`): 次のゲスト受入の回の 8 行のうち 5 行。E11-5 (全画面の WAIT_POLL と先行入力) PASS、H-1 (512KiB stack の park → resume) PASS、PRIO-3 (TVRAM の世代が GUI に入るたびに進む) PASS、X-21 (a) (trim の前後の owner_pages と teardown) PASS、X-6 (tvdump の転送) は 7c914fd で平仮名の 0x04 が EOT とぶつかり FAIL → 6d5549f の TVD2 (0x04・0x1B の逃がし) で PASS。E9-1・E11-9 は前提が v3 で成り立たず H-7 の関門へ、H-2 は (3) だけ残して後続へ (台帳)。

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| X-6 | `tools/tvdump_recv.py` の HTTP・実機シリアル共通 TVD2 (EOT/ESC 逃がし) の長さ・寸法・全セルを照合 | X-6 修正はホスト試験のみ。依頼により NP21/W・実機は未操作 | 次のゲスト受入で平仮名を含む CUI と実機 rshell を照合 |
| E11-5 | 全画面の WAIT_POLL と先行入力の 2 点 (端末の子・隠れた sh は端末アプリが v3 に無く H-7) | R1b で時間の都合で未実施。Run からの全画面の 1 キー終了と窓への復帰は 17MB planar (h9 再回) と実機 R7b で PASS | 次のゲスト受入 / 端末の 2 点は H-7 |
| H-1 | h2_stack512 の park → resume → 終了 owner 0 (512KiB stack のまま OP_WAIT に入れる) | R1a は run と guard だけ (h2_stack の park は GUI 前提で、park/resume は h3 側で実施)。fixture の再生成・hash 照合・旧 app / shell / shlib の拒否・故障画像は閉じた | 次のゲスト受入 |
| PRIO-3 | GUI→CUI→GUI の往復で TVRAM generation の値が進むことの明示観測 | R1d / h9 再回の往復は表示と rshell 応答で判定し、generation の値を読んでいない。シリアル待ちの CTRL+STOP (`audit_test serial`、$?=130) は閉じた | 次のゲスト受入 |
| X-21 | (a) G-4 / R-3 の trim 対象 (C の trim_back・Rust の trim_back_rs) の返却前後の `mem <id>` と teardown の `as_extents_at_teardown` | 未採取。h9 再回の as_extents は gui_demo (別アプリ) で道具の経路を確かめただけで、台本 (`guest_acceptance.yaml` の X-21(a) 行) が求める trim 対象の観測ではない。GUI 中の `mem <id>` は rshell が無く、診断キーの全 slot 行は owner_pages だけ。(b) の台本の書き分けは閉じた | 次のゲスト受入: trim_back / trim_back_rs を G-4 / R-3 の手順で回し、DONE の前後を診断キー (owner_pages) で、teardown を bp で採る |

閉鎖 (2026-10-16、T3ab を 6b4e37e で main へ。NP21/W 64MB の受入 7 項: ①③⑤⑦ 合格、② 合格、④ 未判定 (T3AB-4)、⑥ 条件つき合格 (LGY98-L12 は既存)。証拠 `~/os32-tmp/evidence/2026-10-16/t3ab_np21w/`): 段 A〜C で足した「旧新のホスト検査まで、ゲストは未」の行。実機は T3AB-R へ。

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| T3AB-C | 段Cは旧新のpaging製品ソース取込ホスト試験とリンクまで。実CR0/CR3、旧新の起動/AS生成のゲスト受入は未実施。実memory_bootの新台帳供給はD、DMA装置の可否はEへ | パック指定で配備・エミュレータ/実機操作なし、make all・変異・全体検査はG後 | T3ab D/Eの結線、G後のレビュー/ゲスト受入 ([C票](../../tasks/v3/t3ab/C_PAGING.md)) |
| T3AB-B | 段Bの入口/BSS/stackは旧新ホスト検査のみ。CR0/LTR、実際のfault teardown・SQLite DB/FEP、起動/復帰のゲスト観測とR1一式は未実施 | パック指定で配備/エミュレータ操作なし。ホストは特権操作とexec teardownを代替 | T3ab G後のレビュー/ゲスト受入 ([B票](../../tasks/v3/t3ab/B_STACK_ENTRY.md)) |
| T3AB-A | 段Aは旧/新の定義・ELFリンク・製品ローダーのホスト検査まで。新配置の入口/ページング/台帳/DMA/shell実行時結線、既定0の実起動、V5と通常/R1の一式でのゲスト受入は未確認。実木V5のmanifestは旧shell写しを拒否する (新配置manifestの正常対照はshell値も揃えたfixture) | Aの依頼は配備・エミュレータ操作禁止、実行時の移行はB〜F、世代切替はG | [T3ab B〜G](../../tasks/v3/t3ab/README.md) で実装/各段の製品host検査を進め、G後のOpusレビュー・指定統合検査・NP21/W受入で閉じる。実機回帰はT3bR |

閉鎖 (2026-10-17、実機 Ra266 の T3bR 全項目 PASS。FD 起動から最小インストールで 6b4e37e に入れ直し、LAN の hsync で全体を配備 (errors=0、--verify 一致)。PEGC `win=fff00000`・`alias=ok`、82557 LB3=PASS・LAN の sfs、mem の監査 fail 0、kdebug の selftest 228/228、`[crt]` は DBGREF S3 と一致。証拠 ノートの `~/os32-tmp/evidence/2026-10-17/t3bR_reinstall/`): R1 像は T3AB-R1 へ。

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| T3AB-R | T3ab (6b4e37e) の実機 Ra266 の回帰 T3bR: PEGC の最上位の窓 (`win=fff00000`・`alias=ok`)、82557 (LB3・LAN の sfs)、監査・自己診断、`diag:`。R1 像の一式のゲスト受入も未実施 | NP21/W 64MB の受入 (7 項) は 2026-10-16 に済み (`~/os32-tmp/evidence/2026-10-16/t3ab_np21w/`) | ノートで実施中 (立会い不要)。R1 像は次の R1 の回で |

閉鎖 (2026-10-17、T3b1 cbdf9b8 の NP21/W 64MB 受入: `[selftest] slack_cycle free=245 busy=11 taken=245 fail=0`、`[mem] pages ... ok diff=0`。証拠 `~/os32-tmp/evidence/2026-10-17/t3b1_np21w/RESULT.md`)

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| T3AB-4 | T3 票 §4 T3ab の受入 ④「SLACK の全ページを一度だけ配って返す」のゲストでの観測: ゲストに専用の自己診断が無い。ホストの模型 (段 D、`tools/tests/memory_boot_host.c`) では確認済み、ゲストでは空きページ数の不変と leftover 0 が傍証 | 未判定 (専用の観測口が無い) | T3b1 (kdebug・監査の段) で、ゲストの自己診断に SLACK の一巡を足すかを決める |

閉鎖 (2026-10-17、ユーザー決定「エミュレータで成功していれば良い」— FDRECOV-1 は NP21/W の受入 7 項の合格で完了とし、実機の LAN の部分は行わない。証拠 `~/os32-tmp/evidence/2026-10-17/fdrecov_np21w/RESULT.md`)

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| FDRECOV-1 | FD 起動から LAN で HDD を直す経路 ([TASK_FDRECOV](../../tasks/realhw/TASK_FDRECOV.md)) の**実機の部分**: 82557 の初期化 → シリアル操作 → auto peer の LAN 転送 → 最小インストールなしで HDD 再起動、必要時の明示 peer、wrong peer / ケーブル抜けからの再試行 | 実装 1611c76・39ee7dd (main 3dd05eb) は NP21/W で受入済み (2026-10-17、7 項合格: 新旧 FD の復旧・世代の門・FD 無書込み・書込み禁止の文言、`~/os32-tmp/evidence/2026-10-17/fdrecov_np21w/RESULT.md`)。NP21/W は 82557 を再現しない | 次にノートで実機を回すとき (立会い不要) |

閉鎖 (2026-10-17、FDWP-1 を main 7c54bd9 で直した: FDC の NW を STA_PROTECT として FatFs に伝え、書込み系の失敗でボリュームをマウントし直して汚れた窓を捨てる。NP21/W の受入 4 項に合格、証拠 `~/os32-tmp/evidence/2026-10-17/fdwp_np21w/RESULT.md`)

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| FDWP-1 | 書込み禁止の FD から起動すると、起動ログ (/var/log/boot.new・mkdir /var) の書込み失敗のあと `FATAL: shell.bin load failed` で止まる (NP21/W の --readonly で再現、6b4e37e の FD でも同じ) | FDRECOV-1 の受入で見つけた既存の不具合 (FS の層)。復旧用の FD を書込み禁止にする使い方で起動できない | 原因の調査と直し (カーネル層の不具合なので次の段より先、POLICY_DEV §1) |

閉鎖 (2026-10-17、EXECNEST-1 を main 6d580fd で直した: Ring3CallContext に esp0 を足し着地で復元。NP21/W で T3c2 の 6 命令が 20 回・ネストまで通り、esp0 は毎回同じ値に戻る (ネストの段ごとに一定の 0x6DC)。証拠 `~/os32-tmp/evidence/2026-10-17/t3c2_t3d1_np21w/RESULT.md`。行の「filer も踏む見込み」は誤りだった — filer は常駐 shell の内蔵 (CPL0) で、この形を踏まない)

| ID | 何を残したか | 理由 | 次の関門 |
|---|---|---|---|
| EXECNEST-1 | CPL3 のアプリが KAPI exec_run で子を繰り返し起動すると、子が終わるたびに親のカーネルの入口 (TSS esp0) が戻らず約 1,756 B ずつ深くなり、14 回目に 40KiB の shell stack を溢れさせて triple fault (#DF、cr2=004f5fd0 = shell stack の guard)。子の終わり方 (正常・fault・STOP) によらない。T3c2 の fixture で初めて踏んだ (NP21/W 64MB、`~/os32-tmp/evidence/2026-10-17/t3c2_np21w/RESULT.md`) | カーネル層の不具合 (POLICY_DEV §1、ほかの段より先に直す)。filer など子を繰り返し起動するアプリも同じ形を踏む見込み [推] | 原因の調査と直し → T3c2 の受入をやり直す |

## T5完了後の台帳照合 (2026-10-19)

| ID | 何を閉じたか | 閉じた日 | 根拠 |
|---|---|---|---|
| T4C1-CORE | engine stack 32KiB + NP 1頁の既存受入照合 (core増分+4,072 Bの実測は保持) | 2026-10-19 | `~/os32-tmp/evidence/2026-10-18/t5-0b_np21w/RESULT.md` — §2: 32KiBで全負荷high-water 4,668 B、残28,100 B。深さ100はhost実測24,388 Bとユーザー決定のまま |
| T4C1-FD | T5部品追加後の両FD残量と容量対策の要否 | 2026-10-19 | `~/os32-tmp/evidence/2026-10-19/t5c456_np21w/RESULT.md` — §FD の空き: 2HD 84,992 B・1.44MB 304,128 B、lan/cirrus込み、下限64KBを満たす。T6b-Mは不要。集積拡張T6b-1/2は凍結、圧縮像が508KiBの8割の406.4KiB (≈416kB) に近づいたら起票 |
| T4B7-GUI | GUI端末のdb done/stop・区間中log stop | 2026-10-19 | `~/os32-tmp/evidence/2026-10-18/t5a3_np21w/RESULT.md` — ①GUI端末db done/stopと区間中log stop (B像) 合格。節目でもdb done/stop合格、log試験像は省略 (`~/os32-tmp/evidence/2026-10-19/t5_milestone_np21w/RESULT.md`) |
| IMEDUP-1 | 学習後の同一候補の重複なし | 2026-10-19 | `~/os32-tmp/evidence/2026-10-18/t5-0a_np21w/RESULT.md` — ④CUI/GUIで変換→学習→再変換、重複0・学習語が先頭。節目でも合格 (`~/os32-tmp/evidence/2026-10-19/t5_milestone_np21w/RESULT.md`) |
| E11-A2b | raw KAPI待機利用者のゲスト確認 | 2026-10-19 | `~/os32-tmp/evidence/2026-10-18/t4c_fix3_np21w/RESULT.md` §4・§6 (CUI 8口done/stop、GUI 6口done/stop、区間中log stopの試験像) と `~/os32-tmp/evidence/2026-10-18/t5a3_np21w/RESULT.md` の①db・①区間中log stop (GUI端末db done/stop・区間中log stop) で残る利用者を照合 |
| H-6b | V86 の出口での 6Ah の標準/拡張の復元と 9801 構成の gui_gate | 2026-10-19 | `~/os32-tmp/evidence/2026-10-19/t7a2c_np21w/RESULT.md` — 6Ah の復元は T7a-2c で実装し 9821 相当 (PEGC 480・planar) で合格。9801 構成は**ユーザー決定 2026-10-19 で確かめない** (「PEGC オフの 24kHz なら実質 9801、9821 は後方互換があるので互換を担保しなくてよい」) |
| E10-4 | `v86 -g` の採取の途中の kill/STOP で g/tv・gcap_ops・TVRAM・CR3 が戻ること | 2026-10-19 | `~/os32-tmp/evidence/2026-10-19/t7a2d_np21w/RESULT.md`・`t7a_milestone_np21w/RESULT.md` — 採取 active 印を読んでから V86 中に CTRL+STOP: HOTKEY で戻り g/tv 差 0・rec/ops/owned/tv/keep_if 0・CR3 復帰・直後の V86 成功。#PF/#GP の kill 経路 (kills+1) は host (v86_gcap_abort_host) で確認 |
| T7-V86CTX | v86 の import の例外文脈の印と 2 回目のセッションの page0 照合 | 2026-10-19 | T7a-1c (import に normal,exception の印、生成 metadata で検証) と `~/os32-tmp/evidence/2026-10-19/t7a1bc_np21w/RESULT.md`・`t7a2b_np21w/RESULT.md` — V86 2〜3 回の後の物理 page0 4096B が原本と一致 (NP21/W 本体の書く 0x4F1-0x4F3・0x5BB を除く) |
