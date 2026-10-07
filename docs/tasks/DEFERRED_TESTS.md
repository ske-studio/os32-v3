# 持越し台帳 — 延ばした試験・SKIP・ホストだけの合格・未結線

> 状態: **実装中 (2026-10-06)** — 初版。[TASK_T2D_T2H](v3/TASK_T2D_T2H.md) の実行記録 (4,013 行) に埋もれていた未完了を関門ごとに集めた。
> 規則: **未完了だけを持つ**。終わった行は、証拠の所在を票に 1 行書いてからここから消す。実行ログを貼らない。同じ延期を票・引き継ぎ・memory に写さない。
> ここに行が無い SKIP・未実行は合格に数えない ([ROLES §4](agents/ROLES.md))。FAIL・crash・timeout を延期に書き換えない。関門に着いたら、その関門の行を全部消すまで次へ進まない。

「元の行」は切り離す前 (`b486494`) の TASK_T2D_T2H.md の行番号 — `git show b486494:docs/tasks/v3/TASK_T2D_T2H.md | sed -n 'N,Mp'` か、
[archive/v3/TASK_T2D_T2H_RECORDS.md](../archive/v3/TASK_T2D_T2H_RECORDS.md) の同じ範囲の見出しから読む。
拾い出しの作業表 (全件、約 290 行。Opus サブエージェントが全行を読んで作成、PM は未照合) は `~/os32-tmp/evidence/2026-10-06/t2dh_open_items/`。

## 関門: 新機能より先 (優先段)

PM 起票の候補。公開 KAPI の形と版を保つ修正を e11 より先に配備・受入する。手段と受入条件は [T2票 §2-4 結線表の注記](v3/TASK_T2D_T2H.md#2-4-user切替の一括境界)。

| ID | 何を | 種類 | 関門 |
|---|---|---|---|
| PRIO-3 | シリアルの待ちの CTRL+STOP (`audit_test serial` — rshell の通信路とぶつかるのでキーボードから起動)、GUI→CUI→GUI の TVRAM generation | 未実施の確認 | e11 統合受入 |
| PRIO-1 | install / cdinst の通しの実行 (授権が通り、区画・format・書込みまで) を別のディスクイメージで — 取り込み済みの DISK-AUTH の受入の残り | 未実施の確認 | 次の構成試験 (h の前) |
| LZSS-1 | 修正した圧縮器の PKG (種250の2ファイル) を cdinst で入れて照合。実物 pkg.c の32ビット qemu ホスト試験は合格、依頼範囲に配備・NP21/W操作は含まない | ホストのみ | PRIO-1 のインストール受入 |

## 1. 関門: e9 / e10b / e10c

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E9-1 | TVRAM の RW 化、アプリと sh.bin (CPL3) 経由の tvdump をゲストで確かめる (常駐シェルの tvdump は 2026-10-06 に受入済み) | 未実施の確認 | 1185、1571 |
| E9-2 | h3 の本人識別・前景の証拠 writer を正式な経路へ切り替える (h3 の初期化も) | 申し送り | 2605、2747–2893 |
| E9-3 | TVRAM 範囲外の呼び出しの拒否をゲストで (`6e4df78` はホスト試験だけ) | ホストのみ | 1432–1548 |
| E10-1 | V86 の `-d` / `-b` の正常・失敗・STOP 出口と K1 (session 中の CPL0 例外) のゲスト確認 — 画像と注入手段が要る (`-t`・`-g -t` は 2026-10-06 受入済み) | 未実施の確認 | e10c |
| E10-4 | `v86 -g` の採取の途中の kill で g/tv の解放・gcap_ops・TVRAM 30 行が戻ることのゲスト確認 (ホストのみ。採取が 1 秒未満で途中を狙えない — 長い採取か注入が要る) | ホストのみ | e11 統合受入 |
| E10-5 | V86 session の end 中の再例外での停止の印 (シリアル 1 行と `exec_stop_count`) のゲスト確認 (ホストのみ、注入の手段が要る) | ホストのみ | e11 統合受入 |
| E10-6 | V86 の INT 80h を反射する間 IF=0 のゲスト確認 (ホストのみ、INT 80h を出す V86 の画像が要る) | ホストのみ | e11 統合受入 |
| E10-8 | gfx が台帳のレコード (`ledger_resources[rid].map_*`) を直接書き換えている — pgalloc に範囲を検査して設定する口を作る | 改善 | e11a |
| E10-9 | 監査 (launch・GUI 移譲・V86 帰路) の失敗が計数だけで表示されない — 最初の 1 回だけシリアル 1 行か tag を残す | 改善 | e11a |

## 2. 関門: e11 (公開 KAPI の一括、版の更新は 1 回)

| ID | 何を | 種類 | 元の行 | 関門 |
|---|---|---|---|---|
| E11-A1 | e11c で CPL3 を lease VA に切替: SDK の port/世代照合で再取得、CLIENT regen 時の互換 token を同一 VA に再結線か失効か決定、窓アプリ→全画面→復帰→再描画で生存、正規 CLIENT ケース F (marker/yaml、gfx_shutdown) を追加。描画4本/V86/kselftest/DB・E10-8/9 のゲストと native 補完、予算帰属/再配分は PM (`~/os32-tmp/run/e11/a1_fix1_report.md`)。c で公開口を足すとき DISPLAY の授権の変異をゲスト受入でも見る、gfx_core.c:1019 のコメントを直す | 未結線・ホストのみ・予算超過 | e11a1 レビュー修正1 (2026-10-07) | e11c 切替と PM 統合受入、予算は公開前 |
| E11-1 | surface query/lease/bundle・gfx source/再init publisher・NULL port/互換橋・Unicode・帰路失敗を結線。準備だけで隔離合格としない | 未結線 | 223–257、430–455、571、584–585、654–656、821–823、847、911、949、1023–1040、1069、1089、1105–1108、1143、1204–1207、1367–1368 | e11a/c準備 → 統合 |
| E11-2 | 結線後のkernel/SDK/shlibサイズを再実測し、§6のe枠・圧縮・8MB私有量を確認。撤去の減少を先取りしない | 申し送り | 289–291、457–470、538、688、1240 | e11a/b/c → 統合 |
| E11-3 | 低位/共有USER化・VRAM例外・exec_map_shared_bbを撤去、共有PT操作を拒否。Cirrus DISPLAYは授権leaseでNONE→RW、e11bで窓PDEもUSER禁止へ | 未実施の確認 | 599、605–606、707、826、887–892、1006、1043、1573–1576、1762–1763 | e11a/b → 統合 |
| E11-4 | 上記KAPI-CALLBACK/OWNER/DISK-AUTHの受入を前提に、値返し列挙KAPIとcaller移行・必要ならOS32X授権flag、子が親のredirect先fdを閉じられるFD所有を一括接続 | 契約接続待ち | 1501–1504、1546、1600、1694、1887 | e11b/c → 統合 |
| E11-5 | Run全画面のowner 1または専用KAPIでキー配送。終了・二重注入なし・窓漏れなし・WAIT_POLLを確認。WM直接注入はtranslate()のASCIIのみ (矢印・機能キーは捨て、rawはe11cの専用KAPI)、KAPI追加はcで版一括 | 既知の不具合 | 1167–1169、1431 | e11a/b/c → 統合 |
| E11-6 | P3残: pre-init USER・cdecl橋・版/終了門・utf8初期値/Unicode二重取得・kcg漢字旗・shlib token・wait帰路4件 | 申し送り | 998–1002、1111、1156–1163、1274、1338、1361、1396、1429、1624 | e11a/b/c → 統合 |
| E11-7 | KAPI文書にSTALE/INVALの推測可能性とcallback/scheduling禁止を明記し、公開契約・生成物と照合 | 申し送り | 258、375–377、573、1004 | e11c → 統合 |
| E11-8 | 上記KAPI-AUDIT-FIX/OWNERの受入・分類を反映。pipe_get_bufのkernel番地返却・pipe_get_lenの他owner照会の意味変更と範囲検査P3変異を接続 | 契約接続待ち | 1502、1504、1546 | e11c → 統合 |
| E11-9 | tvdumpのtvram_readchar_atをCUI前景所有者だけに授権。checked copyとTVDM wireを維持し非所有者拒否を確認 | 申し送り | e9 (2026-10-02 PM 決定) | e11b/c → 統合 |
| E11-10 | ring3_guard bb (E)を旧生存から拒否期待へ反転し、正規CLIENT leaseの生存対照を追加。e9の旧期待は準備時のみ | 申し送り | e9 / T2d〜h §2-5 | e11a/b → 統合 |
| E11-11 | db_v50_testに未貸与VRAM拒否を追加。e9の実RAM最終byte成功・guard越境拒否も維持して低位USER撤去後に確認 | 申し送り | e9 (2026-10-02 PM 決定) | e11b → 統合 |
| E11-12 | h3の本人識別を値返しにしwriter初期化を結線 (E9-2)。e9のPM(A)から切替え、CRT非依存markerも確認 | 申し送り | E9-2 / e9 | e11a/c → 統合 |
| E11-13 | SHM lockで全ページRO・CPL3書込み拒否をゲスト確認。ホストの呼出し/結果判定だけで閉じずfree/exit後の次AS成功も対照 | 未実施の確認 | e9 R4 | e11b → 統合 |
| E11-14 | SHMブロック長・ページ長の公開定数を整理しcaller追随。e9のDB_SHM_BLOCK_SIZE/私有PAGE_BYTESから一括移行 | 申し送り | e9 R5 | e11c → 統合 |
| E11-BUD | c1: KHEAP_SIZE=176KB、像予算 +16KB の一時増枠 (17MB/NHD main peak 21,328B、281/0) | b2 撤去後と T2h 前に再計測し戻すか決定 | `c1_sizes.json` | b2 後 → T2h 前。統合ゲストで e11 の kernel の `kmalloc_peak_bytes` を測る (P3-8) |
| E11-A2 | 全画面入力4点・日本語保持・wait失敗回復・非所有shutdownのゲスト受入とnative補完 (a2はqemu/ホストのみ)。本人識別のh3 identity()との照合はe11cとゲストへ持越し (a2はcaller_access模型との照合のみ)、cのslot接続後にh3自己公開とhost読値を照合、raw KAPI待機利用者は明示check契約を統合確認 | 未配備・自己公開未結線 | e11a2 / guest_acceptance e11a2-* | e11c → e11統合受入 (PM) |
| E11-B1 | TVRAM/font低位USER撤去後の描画・日本語・CUI/GUI/WM TVDM、DB42件、SHM lockwrite先頭/末尾CPL3 PFと再利用、PT0・V86全出口3段監査・kselftest、native補完 | 未配備、qemuホストのみ (lockwriteは境界stubで制御フロー確認、CPL3保護の実効性未確認) | guest_acceptance e11b1-*、b1_results.json | b1準備確認 → c → b2 → e11統合受入 (PM) |
| E11-A3 | 全画面 owner の同期の子 (slot.parent) は kernel が読ませるが WM の起床の手がかりに入らない。端末由来のバイト (宛先 0) は全員が読める。**PM 決定 (2026-10-07): 専用 KAPI `kbd_inject_to` は e11c に入れない** — 全画面への矢印・機能キーの配送とともに持ち越し | 改善 | T2h 前に要否を再判断 |


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
| H-7 | apps / game: v3 では組まない (ユーザー決定 2026-09-30)。T2h では再開時ゲート (caller 追随・再ビルド・受入の一覧) の引渡しを確かめる。v3 の完了条件ではない | 再開時ゲート | 1113、1248、1810–1826、1868、3463 |
| H-8 | `ring3_guard` A の固定 target (`MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE`) は T2c 可変スタックで実 stack 直下と一致しないことがある。h 受入で A が実 stack 直下 NP を指すことを確認 | 未実施の確認 | e9 R6 |
| H-9 | `ring3_guard` B (shlib 帯) を shlib を読み込んだ AS で走らせ、RO の error=7 を確かめる (2026-10-06 の受入は未ロードで、帯の fault だけを確認。今のシリアル行は error_code を出さない) | 未実施の確認 | — |
| H-10 | 音源ボード (SNDboard) の ini キーを `np21w_ini_live.py` 系が扱えない (emu-config §1 の対応キー外) → 道具の拡張 (sol) を h の音源構成の準備前に | 道具の不足 | — |

## 5. 関門の記載が無いもの (PM が関門を決めて上の表へ移す)

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| X-1 | NUL 終端の文字列入力と `kprintf` 可変引数の B1 化 → T4・T5a の担当境界で確定。ページ 0 の NP 化 → T7 | 既知の制限 | 1499、1647、1757、1885 |
| X-2 | e8a の RO 拒否のゲスト確認 → e11 統合受入。`font_load_test` の stat 段の SKIP 理由 → 次にその試験を回す前に確定 | 未実施の確認 / SKIP | 1250、1294 |
| X-3 | GitHub Actions の結果が票に無い (e1 の CI 修正後、f1a の初回 run) → 次の統合判定 (e11) の前に過去の run を照合。今の成功で過去を合格にしない | 未実施の確認 | 335–345、2003–2009 |
| X-4 | 検査の整理の残り: ci-stab2 の P2-B の後の全体 check-changed と native の記録、6 時間超の対照の刈り取り、pending の 1 時間回収、生き残る弱い変異、`net_link` の TMPDIR 長、選択の取りこぼし 3 種 | 既知の制限 | 3551、3627、3705、3740、3754–3768、3776、3800–3807、3828–3832 |
| X-6 | `tools/tvdump_recv.py` は名前付きパイプ前提で今の NP21/W に接続できない → `/api/cmd` で生バイトを取り TVDM の長さ・寸法・内容を照合する形に (計画 3 番、受入索引と同枠) | 道具の不具合 | — |
| X-11 | 監査分類 (`~/os32-tmp/evidence/2026-10-07/audit-classification.md`) の「意味変更 (e11c)」のうち pipe (E11-8) を除く 12 件 — IME 9 本 (trygetchar/toggle/set_mode/switch_dict/user_delete/user_export/user_clear/trygetkey/feed_key)・exec_last_result・gui_call・con_sink_read の授権/本人別の契約 → **T4 の設計票で扱う (ユーザー決定 2026-10-07)** | 契約の整理 | 分類表 :33/:43-55/:57/:60 |
| X-10 | 正常対照 21 本の省略と 6 本の実行順を修正し、段の包含を回帰試験化。取り込みから `make check-fast` を除去 → PM の統合 `make check` で受入 | 実装済み・統合受入待ち | 次の取り込み |

## 6. SKIP の登録 (ここにあるものだけを「延期」と数える)

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-1 | Windows opt-in のホスト試験 (全段で 5 件、集約では 9 件) | WSL から Windows 側の道具を既定で起動しない | 環境 | T2h 統合受入の前に 1 回、opt-in で |
| S-2 | `kout_test` の 1 件 | ゲストに `/etc/profile` が無い | **環境不足** (合格ではない) | 次のゲスト受入で `/etc/profile` を置いて回す |
| S-3 | `kout_test` の 2d / 3c | 設計上の適用外 | 適用外 | — (延期ではない。試験側で SKIP ではなく N/A と出すようにする) |
| S-4 | 外部 apps / game の再結線 | v3 では組まない決定 | 適用外 → H-7 (再開時ゲート) | T2h で引渡しの確認 |

## 7. 一括ゲスト一覧に入っていない受入

`tools/tests/guest_tests.txt` (一括) と `tools/emu_agent/tasks/regress.txt` (6 項目) には `d0a_test`・`kout_test`・fault 系・STOP / park-resume・GUI 受入が無い。
一覧を回しただけでは受入一式の代わりにならない — これらは段の受入で個別に回し、結果を票に 1 行書く。
一覧への追加と「台帳に無い SKIP・未起動の必須試験を失敗にする」照合は [REVIEW_2026-10-06 §5 の b](agents/REVIEW_2026-10-06.md)。
