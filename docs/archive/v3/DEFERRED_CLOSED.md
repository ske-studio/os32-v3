# 持越し台帳の閉じた行 — e11 統合受入

状態: **完了記録 (2026-10-07)**。元の行は閉鎖時のまま保持。
証拠の基点: `~/os32-tmp/`。統合受入は `evidence/2026-10-07/accept_e11/RESULT.md`。
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

