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


### f5a (2026-10-07、ホスト確認。ゲストは F-6)

| ID | 閉じた項目 | 証拠 |
|---|---|---|
| F-2 | 空 exec_heap の予約境界をまたぐ併合を禁止、shlib/lease/image/stack/guard の hint を INVAL。 | `tools/tests/appmem_host.c`、`/home/hight/os32-tmp/run/f5/report.md` |
| F-3 | remove_count 単独・空 PT SURFACE・PDE before PT free の負例と変異。部分 free 失敗の隔離・計数・中断要求はホスト確認、実 kill はゲスト F-6。 | `tools/tests/appmem_map_host.c`、同 report。ゲストは F-6 |
