# 持越し台帳 — 延ばした試験・SKIP・ホストだけの合格・未結線

> 状態: **実装中 (2026-10-06)** — 初版。[TASK_T2D_T2H](v3/TASK_T2D_T2H.md) の実行記録 (4,013 行) に埋もれていた未完了を関門ごとに集めた。
> 規則: **未完了だけを持つ**。終わった行は、証拠の所在を票に 1 行書いてからここから消す。実行ログを貼らない。同じ延期を票・引き継ぎ・memory に写さない。
> ここに行が無い SKIP・未実行は合格に数えない ([ROLES §4](agents/ROLES.md))。FAIL・crash・timeout を延期に書き換えない。関門に着いたら、その関門の行を全部消すまで次へ進まない。

「元の行」は切り離す前 (`b486494`) の TASK_T2D_T2H.md の行番号 — `git show b486494:docs/tasks/v3/TASK_T2D_T2H.md | sed -n 'N,Mp'` か、
[archive/v3/TASK_T2D_T2H_RECORDS.md](../archive/v3/TASK_T2D_T2H_RECORDS.md) の同じ範囲の見出しから読む。
拾い出しの作業表 (全件、約 290 行。Opus サブエージェントが全行を読んで作成、PM は未照合) は `~/os32-tmp/evidence/2026-10-06/t2dh_open_items/`。

## 1. 関門: e9 / e10b / e10c

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E9-1 | TVRAM の RW 化、アプリと sh.bin (CPL3) 経由の tvdump をゲストで確かめる (常駐シェルの tvdump は 2026-10-06 に受入済み) | 未実施の確認 | 1185、1571 |
| E9-2 | h3 の本人識別・前景の証拠 writer を正式な経路へ切り替える (h3 の初期化も) | 申し送り | 2605、2747–2893 |
| E9-3 | TVRAM 範囲外の呼び出しの拒否をゲストで (`6e4df78` はホスト試験だけ) | ホストのみ | 1432–1548 |
| E10-1 | e10b: V86 session の全出口と PDE USER、出口の高さ / flip の整合、`paging_v86_map_range` が session を見ない | 未実施の確認 | 605、686、888、1007、1599、1624 |
| E10-2 | e10c: 全 AS の alias 照合、3 段検査、live==0 の合間の USER、`shm_set_rw` の検査、TVRAM の再取得と RO view の残り | 未実施の確認 | 606、686、1185、1274、1599、1624 |

## 2. 関門: e11 (公開 KAPI の一括、版の更新は 1 回)

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E11-1 | 未結線のまま入っているコードの結線と受入: `surface_query` / `query_source`、`surface_lease`、`lease_bundle`、`gfx_surface_source`、`gfx_reinit_surfaces` の publisher、内部 port (本番 NULL) と compat bridge、Unicode 面の port、帰路 check の失敗経路 | 未結線 | 223–257、430–455、571、584–585、654–656、821–823、847、911、949、1023–1040、1069、1089、1105–1108、1143、1204–1207、1367–1368 |
| E11-2 | 結線時にサイズを再計測する (予算の実測ゲート) | 申し送り | 289–291、457–470、538、688、1240 |
| E11-3 | 旧低位 USER / 共有 USER 化の撤去、`ring3_ptr_ok` の VRAM 直書き、`exec_map_shared_bb`、Cirrus DISPLAY NONE→RW | 未実施の確認 | 599、605–606、707、826、887–892、1006、1043、1573–1576、1762–1763 |
| E11-4 | **`sys_ls` の callback を CPL0 で呼ぶ** (ring0 を取れる。`man -l` の crash もこれ)、`shm_lock` / `shm_free` の所有者照合、生ディスクの授権 | 既知の不具合 | 1501–1504、1546、1600、1694、1887 |
| E11-5 | Run から起動した全画面アプリにキーが届かない — 受入 4 点 (キーで終わる / 端末の子に二重に注入しない / 窓へ漏れない / WAIT_POLL 型にも届く) | 既知の不具合 | 1167–1169、1431 |
| E11-6 | e5〜e8b・e10a のレビュー P3 の残り (pre-init USER の食い違い、bridge が `__cdecl` でない、版の門、`gfx_shutdown` の門、utf8 pointer の初期値、Unicode の 2 回取得、`kcg_init` が漢字フラグを消す、shlib token の残り、wait 帰路 check 4 件) | 申し送り | 998–1002、1111、1156–1163、1274、1338、1361、1396、1429、1624 |
| E11-7 | KAPI 文書への注記: STALE / INVAL の推測可能性、callback / scheduling 禁止の契約 | 申し送り | 258、375–377、573、1004 |
| E11-8 | 監査していない KAPI の区分を洗い直す (pipe、redirect、host_*、exec_* / launch_* / appslot、ime_*、gui_call / register、con_sink)。pipe・旧 DB slot の他 owner 操作、範囲検査レビューの P3 群 | 未実施の確認 | 1502、1504、1546 |
| E11-9 | tvdump に使う `tvram_readchar_at` を CUI 全画面の所有者だけに授権 (公開 KAPI の意味変更は e11 の 1 回に集約) | 申し送り | e9 (2026-10-02 PM 決定) |
| E11-10 | `ring3_guard bb` (E) を拒否期待へ反転し、正規 CLIENT lease で生き残る対照を追加。e9 は旧 E 生存を保持 | 申し送り | e9 / T2d〜h §2-5 |
| E11-11 | `db_v50_test` に未貸与 VRAM 拒否の期待を追加 (低位 USER 撤去後)。e9 は実 RAM 最終 byte 成功と guard 越境拒否 | 申し送り | e9 (2026-10-02 PM 決定) |
| E11-12 | h3 の本人識別を値で返す経路と writer 初期化を結線 (E9-2)。e9 では PM 決定 (A) を維持 | 申し送り | E9-2 / e9 |
| E11-13 | SHM lock の実効性 (全ページが RO となり CPL3 書込みを拒否するか) をゲストで確認。e9 のホスト試験は lock 呼出しと結果判定のみ | 未実施の確認 | e9 R4 |
| E11-14 | SHM ブロック長・ページ長の公開定数を整理。e9 は既存の DB_SHM_BLOCK_SIZE と私有 PAGE_BYTES を使用し、公開 SDK は変更しない | 申し送り | e9 R5 |

## 3. 関門: f5 以降 (T2f の結線と受入)

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| F-1 | f2〜f4 (`appmem` / `paging_app` / `appmem_unmap`) はカーネル未結線 — **ホスト合格だけで、完成扱いにしない**。私有エラー値の翻訳、AS への extent 埋込み、LIBC_INITIAL の実登録 | 未結線 | 2146–2202、2211–2235、2282–2319 |
| F-2 | f2 P3: exec_heap が空のとき 0x88000000 をまたぐ併合、shlib / lease 窓の hint | 申し送り | 2205、2231、2293 |
| F-3 | f4 P3: 生き残る変異 3 種、free の失敗で AS が壊れたまま残る | 既知の不具合 | 2363 |
| F-4 | f6 (CRT の `_sbrk` 集約、link_guard、`check_link` が提供元をファイル名だけで判定)、f7 (arena routing)、f8 (Rust `Os32Alloc`)、f9 / f10 (内部の伸長口、EXEC_* の返却)、`mem_alloc` が偽の BlkHdr を信用する | 未結線 / 申し送り | 1503、2037、2045–2059、2101、2103、2163、2355 |
| F-5 | malloc 系の入口の結線と最小初期量の切替 | 未結線 | 2010–2017、2055、2141、2365 |
| F-6 | f の受入一式: 8MB / 17MB での伸長と unmap、512KiB stack、leftover==0 | 未実施の確認 | 2369 |

## 4. 関門: h の最終一式 / T2h 統合受入

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-1 | h2: fixture の再生成と hash 照合、隔離媒体で旧 app / shell / shlib を拒否、owner 回収、park、guard の error=6 とカウンタ、故障画像・旧 shell の試験 | 未実施の確認 | 2463–2509、2909、2914 |
| H-2 | h3: 台本経由の KAPI-loop、pf / gp / de / ud、verify、h3b への交換、PARKED の arm、実行中 OP_WAIT の arm、park 中の WM kill、打鍵を重ねた STOP、前景以外の張本人。h3fix4 の後のゲスト再実行。loop-watch / 単独 stop / trace-watch の live 実行 | 未実施の確認 | 2692–2830、2894–2905、3059–3069、3082、3365–3368 |
| H-3 | GUI KAPI-loop の STOP: ホストだけで見た分岐群、ページ返却の観測、FIRING の後 約 1 秒は STOP が効かない、制限 3 件 | ホストのみ / 既知の制限 | 3218–3262、3278、3305、3367 |
| H-4 | **構成を変える試験の一括** (全段が 17MB・今の ini だけで受入した): 8MB、planar / PEGC / Cirrus の切替、音源 (PC-9801-118 の PCM)、Ra266 64MB。`gfx200_test` / `gfx_demo200` | 構成持越し | 278、418、557、894、897、1009、1112、1276、1340、1431、1601、1887、2908、3364 |
| H-5 | 実機 Ra266: UC 化で present が遅くならないかの計測と CG 窓の WB、表示の後始末 (GRCG / EGC・68h の残り)、kernel stack の high-water | 構成持越し (実機) | 709–712、790–793、2253、2338 |
| H-6 | V86 の出口で 6Ah の標準 / 拡張を戻していない (9821 で E0000h が MMIO のまま残り得る)。`v86 -d` / `-b` の後の表示確認、9801 構成での `gui_gate` | 未対処・未観測 | 788–789、3503 |
| H-7 | apps / game の再ビルドと追随 (v3 の開発中は組まない決定 — 統合受入で必ず行う) | 未実施の確認 | 1113、1248、1810–1826、1868、3463 |
| H-8 | `ring3_guard` A の固定 target (`MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE`) は T2c 可変スタックで実 stack 直下と一致しないことがある。h 受入で A が実 stack 直下 NP を指すことを確認 | 未実施の確認 | e9 R6 |
| H-9 | `ring3_guard` B (shlib 帯) を shlib を読み込んだ AS で走らせ、RO の error=7 を確かめる (2026-10-06 の受入は未ロードで、帯の fault だけを確認。今のシリアル行は error_code を出さない) | 未実施の確認 | — |

## 5. 関門の記載が無いもの (PM が関門を決めて上の表へ移す)

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| X-1 | NUL 終端の文字列入力と `kprintf` 可変引数の B1 化 (T2 B1 追補 / T4・T5a と書かれている)、ページ 0 を NP にしていない | 既知の制限 | 1499、1647、1757、1885 |
| X-2 | e8a の RO 拒否のゲスト確認、`font_load_test` の stat 段の SKIP (環境か適用外かが不明) | 未実施の確認 / SKIP | 1250、1294 |
| X-3 | GitHub Actions の結果が票に無い: e1 の CI 修正後、f1a の初回 run | 未実施の確認 | 335–345、2003–2009 |
| X-4 | 検査の整理の残り: ci-stab2 の P2-B の後の全体 check-changed と native の記録、6 時間超の対照の刈り取り、pending の 1 時間回収、生き残る弱い変異、`net_link` の TMPDIR 長、選択の取りこぼし 3 種 | 既知の制限 | 3551、3627、3705、3740、3754–3768、3776、3800–3807、3828–3832 |
| X-5 | 16000B stride / 200 行の再登録についてのユーザーへの報告 | 未実施 | 1109–1110 |
| X-6 | `tools/tvdump_recv.py` は COM1 の名前付きパイプ `np21w_com1` 前提で、今の NP21/W (デバッグ HTTP サーバ内蔵) では接続できない。`/api/cmd` で生バイトを取る形に直すか退役させる | 道具の不具合 | — |

## 6. SKIP の登録 (ここにあるものだけを「延期」と数える)

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-1 | Windows opt-in のホスト試験 (全段で 5 件、集約では 9 件) | WSL から Windows 側の道具を既定で起動しない | 環境 | T2h 統合受入の前に 1 回、opt-in で |
| S-2 | `kout_test` の 1 件 | ゲストに `/etc/profile` が無い | **環境不足** (合格ではない) | 次のゲスト受入で `/etc/profile` を置いて回す |
| S-3 | `kout_test` の 2d / 3c | 設計上の適用外 | 適用外 | — (延期ではない。試験側で SKIP ではなく N/A と出すようにする) |
| S-4 | 外部 apps / game の再結線 | v3 の開発中は組まない決定 | 適用外 → H-7 | T2h 統合受入 |

## 7. 一括ゲスト一覧に入っていない受入

`tools/tests/guest_tests.txt` (一括) と `tools/emu_agent/tasks/regress.txt` (6 項目) には `d0a_test`・`kout_test`・fault 系・STOP / park-resume・GUI 受入が無い。
一覧を回しただけでは受入一式の代わりにならない — これらは段の受入で個別に回し、結果を票に 1 行書く。
一覧への追加と「台帳に無い SKIP・未起動の必須試験を失敗にする」照合は [REVIEW_2026-10-06 §5 の b](agents/REVIEW_2026-10-06.md)。
