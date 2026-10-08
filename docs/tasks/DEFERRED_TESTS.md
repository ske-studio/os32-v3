# 持越し台帳 — 延ばした試験・SKIP・ホストだけの合格・未結線

> 状態: **実装中 (2026-10-06)** — 初版。[TASK_T2D_T2H](v3/TASK_T2D_T2H.md) の実行記録 (4,013 行) に埋もれていた未完了を関門ごとに集めた。
> 規則: **未完了だけを持つ**。終わった行は、証拠の所在を票に書いてから [閉鎖台帳](../archive/v3/DEFERRED_CLOSED.md) へ移す。実行ログを貼らない。同じ延期を票・引き継ぎ・memory に写さない。
> ここに行が無い SKIP・未実行は合格に数えない ([ROLES §4](agents/ROLES.md))。FAIL・crash・timeout を延期に書き換えない。関門に着いたら、その関門の行を全部消すまで次へ進まない。

「元の行」は切り離す前 (`b486494`) の TASK_T2D_T2H.md の行番号 — `git show b486494:docs/tasks/v3/TASK_T2D_T2H.md | sed -n 'N,Mp'` か、
[archive/v3/TASK_T2D_T2H_RECORDS.md](../archive/v3/TASK_T2D_T2H_RECORDS.md) の同じ範囲の見出しから読む。
拾い出しの作業表 (全件、約 290 行。Opus サブエージェントが全行を読んで作成、PM は未照合) は `~/os32-tmp/evidence/2026-10-06/t2dh_open_items/`。

## 関門: 新機能より先 (優先段)

PM 起票の候補。公開 KAPI の形と版を保つ修正を e11 より先に配備・受入する。手段と受入条件は [T2票 §2-4 結線表の注記](v3/TASK_T2D_T2H.md#2-4-user切替の一括境界)。

| ID | 何を | 種類 | 関門 |
|---|---|---|---|
| PRIO-3 | シリアルの待ちの CTRL+STOP (`audit_test serial` — rshell の通信路とぶつかるのでキーボードから起動)、GUI→CUI→GUI の TVRAM generation。e11 受入ではローカル起動が必要で未実施 | 未実施の確認 | T2h 統合受入 |
| PRIO-1 | install / cdinst の通しの実行 (授権が通り、区画・format・書込みまで) を別のディスクイメージで — 取り込み済みの DISK-AUTH の受入の残り。e11 受入では別イメージが必要で未実施 | 未実施の確認 | T2h の構成試験の前 |
| LZSS-1 | 修正した圧縮器の PKG (種250の2ファイル) を cdinst で入れて照合。実物 pkg.c の32ビット qemu ホスト試験は合格、依頼範囲に配備・NP21/W操作は含まない | ホストのみ | PRIO-1 のインストール受入 |

## 1. 関門: e9 / e10b / e10c

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| E9-1 | CPL3 sh 経由の tvdump をゲストで確かめる。常駐 CUI は受入済み。sh に -c が無く対話入力はキーボードのため、入力の道具が要る | 未実施の確認 | 1185、1571 |
| E9-2 | h3 の本人識別・前景の証拠 writer を正式な経路へ切り替える (h3 の初期化も) | 申し送り | 2605、2747–2893 |
| E9-3 | TVRAM 範囲外の呼び出しの拒否をゲストで (`6e4df78` はホスト試験だけ) | ホストのみ | 1432–1548 |
| E10-1 | V86 の `-d` / `-b` の失敗出口と K1 (session 中の CPL0 例外) のゲスト確認。正常・STOP 出口は e11 統合受入済み、故障画像と注入手段が要る | 未実施の確認 | e10c |
| E10-4 | `v86 -g` の採取の途中の kill で g/tv の解放・gcap_ops・TVRAM 30 行が戻ることのゲスト確認 (ホストのみ。採取が 1 秒未満で途中を狙えない — 長い採取か注入が要る)。e11 受入の持越し理由: 採取が 1 秒未満で途中を狙えない | ホストのみ | T2h 統合受入 |
| E10-5 | V86 session の end 中の再例外での停止の印 (シリアル 1 行と `exec_stop_count`) のゲスト確認 (ホストのみ、注入の手段が要る)。e11 受入の持越し理由: 故障画像と再例外の注入手段が要る | ホストのみ | T2h 統合受入 |
| E10-6 | V86 の INT 80h を反射する間 IF=0 のゲスト確認 (ホストのみ、INT 80h を出す V86 の画像が要る)。e11 受入の持越し理由: INT 80h を出す V86 の画像が要る | ホストのみ | T2h 統合受入 |

## 2. 関門: e11 (公開 KAPI の一括、版の更新は 1 回)

| ID | 何を | 種類 | 元の行 | 関門 |
|---|---|---|---|---|
| E11-5 | Run からの全画面の 1 キー終了は受入済み。端末の子・WAIT_POLL・隠れた sh・先行入力の 4 点だけゲスト確認 | 既知の不具合 | 1167–1169、1431 | T2h 統合受入 |
| E11-8 | pipe_get_buf の kernel 番地返却・pipe_get_len の他 owner 照会の意味変更と範囲検査はホスト確認のみ。ゲストで本人成功・他 owner 拒否の対照を確認 | 契約接続待ち | 1502、1504、1546 | T2h 統合受入 |
| E11-9 | CUI の tvdump は受入済み。GUI 端末と WM の 0 埋めのゲスト確認 (ホストのみ)、V86 後の DISPLAY/TVRAM 再 lease と全 alias UC の明示観測 (e12-17mb-pegc-v86-release) | 申し送り | e9 (2026-10-02 PM 決定) | T2h 統合受入 |
| E11-12 | c3: h3 caller_identity自己公開とhost identity()照合を接続、qemuホスト確認。PM: h3a/b実ゲスト・CRT非依存marker受入 | ホスト確認・統合ゲスト未確認 | E9-2 / e9 | T2h の h3 (本人識別のゲスト照合は h3 の手順一式で) |
| E11-BUD | kmalloc_peak_bytes は exec ヒープの確保も数える (kheap_alloc が全 KHeap 共通で更新) — GUI 後 1,286,752B。カーネルヒープの使用は `mem` で 2,544/180,224B。ピークはカーネルヒープだけに直した (e12)、測り直しは統合ゲストで、KHEAP を戻すか決める。b2 実測は b2_sizes.json | ホスト検証・統合ゲスト未実施 | `c1_sizes.json` | T2h 前 |
| E11-A2 | h3 本人識別のゲスト照合は h3 の手順一式で。attach 失敗の注入・非 owner shutdown・raw KAPI 待機利用者の明示 check はホストのみ。全画面入力 4 点は E11-5 に集約、日本語は受入済み | ホスト確認・統合ゲスト未確認 | e11a2 / guest_acceptance e11a2-* | T2h の h3 |
| E11-A3 | 全画面 owner の同期の子 (slot.parent) は kernel が読ませるが WM の起床の手がかりに入らない。端末由来のバイト (宛先 0) は全員が読める。**PM 決定 (2026-10-07): 専用 KAPI `kbd_inject_to` は e11c に入れない** — 全画面への矢印・機能キーの配送とともに持ち越し | 改善 | T2h 前に要否を再判断 |

| E11-PERF | present ごとの query の前後比較は基準値が無く未実施。bench_scale2x Test 1 の基準は 320x200 40ms/100、640x400 10ms/100 (accept_e11/RESULT.md)。比較値を採取する | 未実施の確認 | E11-1 の比較残件 | T2h 統合受入 |

## 3. 関門: f5 以降 (T2f の結線と受入)

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| F-4 | f7 / f8 の実装分は閉鎖 (host: arena routing / hash 検査 / morecore 単一化、C 大塊直接map、Rust `Os32Alloc` 整列adapter、実ゲストは F-8)。f9 の小 arena 伸長・EXEC 内部返却・偽 BlkHdr は host 検査で閉鎖。残り: f10 の大塊 / trim、f9 の実ゲストは F-10 | 未結線 / 申し送り | 1503、2037、2045–2059、2101、2103、2163、2355 |
| F-5 | USER malloc 入口は f7 で結線。残りは f12 の最小初期量切替 (resident は libc nano / 固定 sbrk を維持) | 一部未結線 | 2010–2017、2055、2141、2365 |
| F-6 | f5b の `mem_map_test` 対照 (exit 0) と `mem_map_test pf` (unmap 後の同じ VA で PF/kill)、KAPI 71 のゲスト未受入。手順 `tools/tests/guest_acceptance.yaml` の f5b-mem-map。f の受入一式: 8MB / 17MB での伸長と unmap、512KiB stack、kernel stack high-water (f5a の AS +532B / map pending 256B、lease_selftest 1,676→2,732B (AS 2 個)・test_ledger 812→1,340B・test_appmem 1,232B を含む)、leftover==0、毒 AS の kill (syscall 帰路と resume)。f5a レビュー: 入れ子で親 A が毒の後に子の終了で A の PD が一時的に CR3 に載る (USER へは戻らず syscall 出口で kill — P3-c)、resume の中断も h3 の `syscall_abort` 地点に当たる (P3-d) — ゲストの h3 の数え方で確かめる | 未実施の確認 | 2369 |
| F-7 | f6 の USER/resident CRT の実ゲスト受入 (`f6-sbrk-grow`: 初期 mapped_end を越える連続伸長と書込み、INT_MIN・負増分の下限、EXACT 衝突時に break 不変、終了後の kill 差 0・leftover 0、db_test/db_v50_test、親子入れ子で親の break 保持) | 未実施の確認 | f6 受入
| F-8 | f8 の実ゲスト受入 (`f8-large-aligned`: 65535/65536/65537、calloc全byteゼロ、直接map free後の全域EXACT再map、alloc_demo align4096・zeroing・tail書込み。Rust余白込みmem_alloc実byteのkernel分類はf10)。f7 の実ゲスト受入 (`f7-malloc-arena`: 強制副 arena、跨ぎ realloc、空副 arena の EXACT 再map、printf、kill 差0・leftover 0) と heap_test / sbrk_grow_test / db_test / db_v50_test。配備・ゲスト操作は依頼範囲外のため PM が取り込み時に実施。SDK 利用 apps/game の再ビルドも PM 統合時。 | 未実施の確認 | f7 / f8 受入 |
| F-10 | f9 `exec_heap_test` (親子とも `/usr/bin/exec_heap_test.bin`、64KiB 初期 heap) の EXEC_ARENA 伸長・全バイト保持・改竄後生存、resident used 前後一致 / kill 差 0 / leftover 0、heap_test (64KiB mem_alloc が最低1個成功・exit 0) / db_test / db_v50_test 回帰 | 実ゲスト未実行 (実装者は配備しない)。PM の f9 受入で `guest_acceptance.yaml` の f9-exec-heap を実施 | `/home/hight/os32-tmp/run/f9/f9_report.md` |
| F-9 | 副 arena が要求量に応じた大きさで作られるため、小さい割当ての継続で arena が N 個に増え、伸長失敗の syscall が N+1 回・free が O(N) になる。f8 の大塊も同じ: 全 free/realloc が大塊一覧を O(N) 走査し、TOPDOWN の大塊が隣どうし併合されるため虫食いの free で extent 表 (32 本) が満杯になると unmap が EFULL で返却されない (一覧は復元、exit で回収)。大塊 realloc で旧 unmap だけ失敗すると成功を返しつつ errno=ENOMEM と旧ブロックが残る (f8 レビュー P3-a/b/c)。f9 の USER exec_heap は 1 回の alloc/free が O(所属 arena のページ数 + ブロック数) で、既定の INITIAL 1 本 (avail/2 ≒ 1MiB) では毎回 256 ページ超を検証する (f9 レビュー R1)。f11/f12 で最小 arena サイズまたは EXACT 失敗の印を検討・検証する (f7 は性能改善を延期)。 | 性能課題 / 申し送り | f11 / f12 |
| X-14 | `test_ci_stab.py` の sigterm 試験 (:790,807-815) が並行負荷で空の report を読んで落ちる flake。子が `write_text` で開いてから書く隙間 (f9 レビュー R2)。子を一時ファイル + rename にするか、親が空でないまで待つ | 未修正 (既存) | f9 取り込み時に 1 回 |

## 4. 関門: h の最終一式 / T2h 統合受入

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| H-1 | h2: fixture の再生成と hash 照合、隔離媒体で旧 app / shell / shlib を拒否、owner 回収、park、guard の error=6 とカウンタ、故障画像・旧 shell の試験 | 未実施の確認 | 2463–2509、2909、2914 |
| H-2 | h3: 台本経由の KAPI-loop、pf / gp / de / ud、verify、h3b への交換、PARKED の arm、実行中 OP_WAIT の arm、park 中の WM kill、打鍵を重ねた STOP、前景以外の張本人。h3fix4 の後のゲスト再実行。loop-watch / 単独 stop / trace-watch の live 実行 | 未実施の確認 | 2692–2830、2894–2905、3059–3069、3082、3365–3368 |
| H-3 | GUI KAPI-loop の STOP: ホストだけで見た分岐群、ページ返却の観測、FIRING の後 約 1 秒は STOP が効かない、制限 3 件 | ホストのみ / 既知の制限 | 3218–3262、3278、3305、3367 |
| H-4 | **構成を変える試験の一括** (全段が 17MB・今の ini だけで受入した): 8MB、planar / PEGC / Cirrus の切替、音源 (PC-9801-118 の PCM)、Ra266 64MB。`gfx200_test` / `gfx_demo200`、e12 の構成別台本 (guest_acceptance.yaml の e12-8mb-* / e12-17mb-planar / e12-17mb-cirrus / e12-cirrus-off) | 構成持越し | 278、418、557、894、897、1009、1112、1276、1340、1431、1601、1887、2908、3364 |
| H-5 | 実機 Ra266: UC 化で present が遅くならないかの計測と CG 窓の WB、表示の後始末 (GRCG / EGC・68h の残り)、kernel stack の high-water | 構成持越し (実機) | 709–712、790–793、2253、2338 |
| H-6 | V86 の出口で 6Ah の標準 / 拡張を戻していない (9821 で E0000h が MMIO のまま残り得る)。`v86 -d` / `-b` の後の表示確認、9801 構成での `gui_gate` | 未対処・未観測 | 788–789、3503 |
| H-7 | apps / game: v3 では組まない (ユーザー決定 2026-09-30)。T2h では再開時ゲート (caller 追随・再ビルド・受入の一覧) の引渡しを確かめる。v3 の完了条件ではない。再開時は **c2 以後の SDK で再ビルド必須** — 旧 libos32gfx.a の外部バイナリは v70 で互換 token の VA を得て取り直さず、200 ライン化後に kill され得る (e11c2 レビュー R6)。**memory_layout=2 (e11b2) により、再ビルド前の apps/game の成果物はロード時に世代不一致で必ず拒否される**。外部の Makefile は T2c 以後の `link_guard.py` 経由のリンクにも未追随 (2026-10-08 の `make external` で mkos32x が拒否)。f7 以後は SDK の CRT が `libos32nano.a` を要るので、外部 Makefile のリンクに `-los32nano` を `-lc` より前に足す (f7 レビュー P2-1) | 再開時ゲート | 1113、1248、1810–1826、1868、3463 |
| H-8 | `ring3_guard` A の固定 target (`MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE`) は T2c 可変スタックで実 stack 直下と一致しないことがある。h 受入で A が実 stack 直下 NP を指すことを確認 | 未実施の確認 | e9 R6 |
| H-9 | `ring3_guard` B (shlib 帯) を shlib を読み込んだ AS で走らせ、RO の error=7 を確かめる (2026-10-06 の受入は未ロードで、帯の fault だけを確認。今のシリアル行は error_code を出さない) | 未実施の確認 | — |
| H-10 | 音源ボード (SNDboard) の ini キーを `np21w_ini_live.py` 系が扱えない (emu-config §1 の対応キー外) → 道具の拡張 (sol) を h の音源構成の準備前に | 道具の不足 | — |

## 5. 関門の記載が無いもの (PM が関門を決めて上の表へ移す)

| ID | 何を | 種類 | 元の行 |
|---|---|---|---|
| X-1 | NUL 終端の文字列入力と `kprintf` 可変引数の B1 化 → T4・T5a の担当境界で確定。ページ 0 の NP 化 → T7 | 既知の制限 | 1499、1647、1757、1885 |
| X-2 | `font_load_test` の stat 段の SKIP 理由 → 次にその試験を回す前に確定 | 未実施の確認 / SKIP | 1250、1294 |
| X-3 | GitHub Actions の結果が票に無い (e1 の CI 修正後、f1a の初回 run) → 次の統合判定 (e11) の前に過去の run を照合。今の成功で過去を合格にしない | 未実施の確認 | 335–345、2003–2009 |
| X-4 | 検査の整理の残り: ci-stab2 の P2-B の後の全体 check-changed と native の記録、6 時間超の対照の刈り取り、pending の 1 時間回収、生き残る弱い変異、f5a の full 候補検査に残る 5 SKIP、`net_link` の TMPDIR 長、選択の取りこぼし 3 種 | 既知の制限 | 3551、3627、3705、3740、3754–3768、3776、3800–3807、3828–3832 |
| X-6 | `tools/tvdump_recv.py` は名前付きパイプ前提で今の NP21/W に接続できない → `/api/cmd` で生バイトを取り TVDM の長さ・寸法・内容を照合する形に (計画 3 番、受入索引と同枠) | 道具の不具合 | — |
| X-11 | 監査分類 (`~/os32-tmp/evidence/2026-10-07/audit-classification.md`) の「意味変更 (e11c)」のうち pipe (E11-8) を除く 12 件 — IME 9 本 (trygetchar/toggle/set_mode/switch_dict/user_delete/user_export/user_clear/trygetkey/feed_key)・exec_last_result・gui_call・con_sink_read の授権/本人別の契約 → **T4 の設計票で扱う (ユーザー決定 2026-10-07)** | 契約の整理 | 分類表 :33/:43-55/:57/:60 |


## 6. SKIP の登録 (ここにあるものだけを「延期」と数える)

| ID | 試験 | SKIP の理由 | 区別 | いつ必ず回すか |
|---|---|---|---|---|
| S-1 | Windows opt-in のホスト試験 (全段で 5 件、集約では 9 件) | WSL から Windows 側の道具を既定で起動しない | 環境 | T2h 統合受入の前に 1 回、opt-in で |
| S-2 | `kout_test` の 1 件 | ゲストに `/etc/profile` が無い | **環境不足** (合格ではない) | 次のゲスト受入で `/etc/profile` を置いて回す |
| S-3 | `kout_test` の 2d / 3c | 設計上の適用外 | 適用外 | — (延期ではない。試験側で SKIP ではなく N/A と出すようにする) |
| S-7 | blit_test 全体 / bench_scale2x Test 3 | packed は planar 4 面試験の適用外 | 適用外 (PASS に数えない) | H-4 planar 構成で実行、PEGC は SKIP と他の計測の継続を確認 |
| S-8 | font_test | /data/ipaexg.ttf は意図的に非配備 | 環境不足 | H-4 前に TTF を置いて実行、未配備は終了 2 と SKIP 行を確認。SKIP 行の `os32api::print` (kprintf) はリダイレクトを素通りする既知の制限 (Rust の fd1 helper ができるまで) |
| S-4 | 外部 apps / game の再結線 | v3 では組まない決定 | 適用外 → H-7 (再開時ゲート) | T2h で引渡しの確認 |

## 7. 一括ゲスト一覧に入っていない受入

`tools/tests/guest_tests.txt` (一括) と `tools/emu_agent/tasks/regress.txt` (6 項目) には `d0a_test`・`kout_test`・fault 系・STOP / park-resume・GUI 受入が無い。
一覧を回しただけでは受入一式の代わりにならない — これらは段の受入で個別に回し、結果を票に 1 行書く。
一覧への追加と「台帳に無い SKIP・未起動の必須試験を失敗にする」照合は [REVIEW_2026-10-06 §5 の b](agents/REVIEW_2026-10-06.md)。
| S-5 | e2test | 372KB × 2 のバッファが取れない | 環境の前提不足 | exec ヒープの上限を見直す f の段 |
| S-6 | host_test | host_agent 不在 | 環境の前提不足 | T2h の Host Services の受入 |
