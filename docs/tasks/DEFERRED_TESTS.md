# 持越し台帳 — 延ばした試験・SKIP・ホストだけの合格・未結線

> 状態: **現行 (2026-10-14、反省会の決定と GSERIAL-1・GSERIAL-2 の閉鎖を反映。h9 で T2h の照合済み)** — R0〜R7 (NP21/W の 5 構成 + 実機 Ra266) の結果で閉じた行は [閉鎖台帳](../archive/v3/DEFERRED_CLOSED.md) の「T2h R0〜R7」へ移した (閉 21・部分閉 10)。残るのは **渡す 21** (次のゲスト受入 8、後続の段 12、実機 1)・**持越し 3** (決定)・**実機の所見 4**。分類の一覧は `~/os32-tmp/run/h/h9/H9_CLASSIFY.md`。
> **T5 着手前の再照合 r5 (2026-10-18)** — T5-0a/0b・T5a-3の受け口、CD接続取消と凍結、FD容量ゲートを同期。状態行は過去の記録のまま。
> 規則: **未完了だけを持つ**。終わった行は、証拠の所在を票に書いてから [閉鎖台帳](../archive/v3/DEFERRED_CLOSED.md) へ移す。実行ログを貼らない。同じ延期を票・引き継ぎ・memory に写さない。
> ここに行が無い SKIP・未実行は合格に数えない ([ROLES §4](agents/ROLES.md))。FAIL・crash・timeout を延期に書き換えない。関門に着いたら、その関門の行を全部消すまで次へ進まない。

「元の行」は切り離す前 (`b486494`) の TASK_T2D_T2H.md の行番号 — `git show b486494:docs/tasks/v3/TASK_T2D_T2H.md | sed -n 'N,Mp'` か、
[archive/v3/TASK_T2D_T2H_RECORDS.md](../archive/v3/TASK_T2D_T2H_RECORDS.md) の同じ範囲の見出しから読む。
拾い出しの作業表 (全件、約 290 行) は `~/os32-tmp/evidence/2026-10-06/t2dh_open_items/`。T2h の受入の結果は [TASK_T2D_T2H §5-1](v3/TASK_T2D_T2H.md#5-1-条件成果物)。

## 関門の表

| 関門 | 行 | 進む条件 |
|---|---|---|
| T3 (帯変更) | F-9、H-4 (a) | 着手前に扱いを決める |
| **v3 の最終段 (搭載メモリ量ごとの動作確認)** | H-4 の構成別の残り (8MB・17MB・Cirrus など、メモリ量・構成ごとの M の全通し) | ユーザー決定 2026-10-14: 搭載メモリ量ごとの確認は v3 の最終段まで後回し。それまでのゲスト受入は NP21/W 64MB (`np21x64w.ini` の ExMemory=64、実機 Ra266 と同じ) の 1 構成で回す。8MB の最低条件 (V3_PLAN §2-2) は目標のまま、その判定を最終段で行う |
| 次に `check_select.py` / ci-stab を触る保守の段 (遅くとも T3 の最初の取り込みの全体検査) | X-4 (残り) | 記録の分はその全体検査で残す |
| T4 | X-11、E11-A2b、X-1 の一部 | 設計票で扱う |
| T5-0a / T5-0b | T4C1-GUEST、T4B-EXT2、T4C1-CORE、KSTK-DF、IMEDUP-1 | T4証拠照合・fixture短縮/計測→実guard受入→stack採否 |
| T5a-3 | T4B7-GUI | GUI端末と入力・IME・STOPの観測 |
| T5の各部品追加 / 次のVFS・ISO境界変更 | T4C1-FD / T4B-CD | FD容量でT6b-M挿入 / 凍結コードの削除要否判断 |
| T5a / T7 | X-1 の一部、E10-4、H-6b、H-5 (CG 窓 WB・68h) | — |
| [TASK_PCM_CS4231](v3/TASK_PCM_CS4231.md) | R7-2、H-4 (b)(c) | — |
| 保守 (道具・媒体) | LZSS-1、PRIO-1、S-1、X-20、PRE-1 | 次にその道具・媒体を触る段 |
| 決定による持越し | H-7、S-4、E11-A3 | 再開時ゲート (H-7) / U6 で足さない (E11-A3) |
| 記録 | R7-3、R7-4 | — |

## 2. 渡す — 後続の段

| ID | 何を残したか | 理由 | 渡し先 |
|---|---|---|---|
| T3AB-R1 | T3ab の R1 像の一式のゲスト受入 | 実機の回帰 T3bR は 2026-10-17 に全項目 PASS (閉鎖台帳)。R1 像だけ未実施 | 次の R1 の回 |
| T3C2-GUI | T3c2 の 6 命令を GUI の端末から回す (sentinel・20 回・ネスト) と、H-5 の Ra266 64MB での同条件比較 | v3 に GUI の端末アプリが無い (H-7)、H-5 は実機の条件。CUI の 6 命令・CUI/GUI の往復・PT の監査は NP21/W で合格 (2026-10-17、`~/os32-tmp/evidence/2026-10-17/t3c2_t3d1_np21w/RESULT.md`) | GUI の端末は H-7 の再開時、H-5 はハードウェアの実機の回 (T4T7-RH と同じ回) |
| T4C1-GUEST | T4c-1のゲスト項目のうち未照合: 欠落/混在拒否・対更新・KDEBUG ON/OFF・監査、注入/QUARANTINED | 通常DB 9/9・42/42とservices順/diff0、部品なしMINIMALはT4c-2受入記録あり (`~/os32-tmp/evidence/2026-10-18/t4c2_np21w/RESULT.md`)。項目別に突合し、T4全体の状態行だけで一括閉鎖しない | T5-0aで証拠照合。未証拠の注入/隔離・同/異世代更新/別root復旧等は残す |
| T4B-CD | 不変CD専用RO SQLiteのコード/host試験を凍結保持。削除費用と像の差の判断だけ残す | ユーザー決定2026-10-18: vfs_sqlite_allow_immutableのT4c/T5b製品接続予定を取消。CD root/FEP/settings/permit/媒体交換の製品試験は方針変更で対象外 (合格ではない) | 次にVFS/ISO境界を触る段で削除要否を判断。通常CD読取/cdinstはT5b-5。ext2排他は別行 |
| T4B-EXT2 | 実ゲストの同一DB排他 (親子/park/RESIDENT/別名) の証拠照合 | T4b-4/4cのhost成功やCD接続取消でゲスト完了にはしない | T5-0aでT4受入証拠と照合し、未証拠分を明記して実施済みだけ閉鎖 |
| T3D2-D15 | D15 (前景 1 本で起動 2 秒・入力→描画 100ms、P100/32MB) の判定。T3d2 の時間は NP21/W の参考値だけ (`~/os32-tmp/evidence/2026-10-18/t3d2_np21w/RESULT.md`) | エミュレータの時間は実機の時間ではない。票 T3d2 の取り決めで「未測定として台帳へ」 | 実機の回 (T4T7-RH と同じ回)、T7b の日本語の描画の計測と合わせて |
| T4T7-RH | T4〜T7 の実機 (Ra266) での確認: T5c-4 の 82557 の回帰、T6b-2 の Ra266 の欄、T7a-2c の 9821 の再描画と CG 窓 (H-5・H-6b)、T7a-2e2 の `v86 -g`/boot2/DOS、T7 の Ra266 の計測 | ユーザー決定 2026-10-17: ソフトウェアの変更は NP21/W で合否を決め、ハードウェアに触るカーネルの変更は実機のテストをまとめて行う | ハードウェアに触る段がたまった所でまとめて行う実機の回 (時期は PM が提案)。新しい段で実機の項目が出たらこの行に足す |
| T4C1-FD | T4c-1時点のFD残量: 2HD 157KB・1.44MB 369KB、SQLite分離で約136KB減 | 過去の値。VK32圧縮減と非圧縮部品/prefix/cluster丸めの差を再計測する必要がある | T5-0aと各追加前後に両FD実残量を測る。ATAPI/iso9660・lan.modも計上。不足する追加段の前へT6b-Mを挿入。T6b-1/2の集積拡張だけでは解消しない |
| T4C1-CORE | engine stack 128KiB の見直し: host の最大使用量は 4,604 / 131,072 B (init/CRUD/診断だけ)。core 増分 +4,072 B は削らず見積を実測に直した (ユーザー承認 2026-10-18) | ゲストの大きい文 (db_v50_test) での最大使用量が未測定 | T5-0aで128KiBのままdb_v50_test等のhigh-water/残余を計測。T5-0bのKSTK-DF受入後に32KiB採否を記録し、縮小時は同じ負荷/rollback/監査を再確認、不足なら128KiB維持 |
| KSTK-DF | カーネル・SQLite engine のスタックがガード頁に当たると、#PF の例外フレームも NP 頁に書くので #DF → トリプルフォルト (リセット) になり、診断の 1 行が出ない。#DF を専用 TSS の task gate で受ければ止まる前に診断を出せる (T4c-1 の直しの astra レビュー P1) | T4c-1 以前の固定ガード・カーネルのスタックと同じ振る舞い (`kernel/isr_stub.asm:28`)。黙った破壊ではなく停止なので T4c-1 の範囲外 | T5-0b。専用TSS/task gateでkernel/engineの実guard到達をゲスト注入し、診断→停止 (reset/cleanup0) を受けてからstack縮小へ |
| FEPLEARN-1 | 学習した語は `fep.db` の `dict_user` にあり、配備 (nhd_deploy sync) や FEP が閉じている間の hsync で `fep.db` を入れ替えると配布の状態に戻る (T5-0a の受入で観測) | T4 以前からの作り。FEP が開いている間の hsync は BUSY で置き換えを断る (`userland/system/hsync.c:1622`) ので普段の更新では起きない | 学習を別ファイルに分ける DICT_META の段で解消。実機で FD 復旧 (FDRECOV) をすると学習が消えることを手順書に書くのはその段まで保留 |
| T4B7-GUI | T4b-7 の GUI 端末からの db done/stop と、区間中の log stop (試験ビルド) | db の fixture (4^12 行の集計) が 1 回 12〜19 分かかり、GUI 端末の回は未実行。通常の像の log stop は子が先に終わる。CUI では両方合格 (`~/os32-tmp/evidence/2026-10-18/t4c_fix3_np21w/RESULT.md`) | T5-0aで製品SQL設定のままfixtureを数分へ縮め期待結果/試験を同期 (文の形を保ち、行数でstack深さは変わらない)。T5a-3でGUI端末のdone/stop・区間内log stopと入力/IME/STOPを観測。端末実体/世代/起動経路を固定しH-7/T5A-ACCとは分ける |
| IMEDUP-1 | 学習後に同一候補が重複しないことのゲスト確認 | コードは着地、C側の重複排除で候補32件未満を許す。ゲスト未確認 | 最初のゲスト受入T5-0aで変換→学習→再変換を観測 |
| LGY98-L12 | LGY-98 の L1 (131/200 フレーム) と L2 (66,132/131,072 B) が FAIL — main 9aaaf39 でも同じ数値で FAIL (T3ab の後退ではない)。台本 `tools/net_l0..l3_test.py` は urllib で 127.0.0.1:8025 を直接叩くので、WSL の NAT では届かない (受入では手元の shim で `np21w_client` の curl 経路へ回した) | 既存の失敗、原因は台本か host_agent v2 の側 [推] | 次に LGY-98 / net の台本を触る段: 台本を np21w_client に寄せ、L1/L2 の欠落の原因を調べる |
| E9-1 | CPL3 sh 経由の tvdump (CUI 対照つき) | v3 の sh.bin はシリアル系コマンドを持たず (D2(a))、sh の中の tvdump は「external programs need the GUI terminal」で $?=126 — 台本の前提が v3 で成り立たない。代わりに CPL3 の CUI 前景で `audit_test tvram` が PASS (2026-10-16、`~/os32-tmp/evidence/2026-10-16/guest8/E9-1/`) | H-7 (端末アプリの再開) の再開時ゲートで |
| E11-9 | WM の tvdump 0 埋めのゲスト確認 | v3 の GUI には rshell が無く gserial にも tvdump が無い。代わりに GUI の Run から `audit_test tvram` を回すと FAIL 12 (範囲外の読み 10 回の番兵が 0 で上書き + 'Q' 2 件) — GUI 中の TVRAM の読みは 0 埋めという設計と整合するが、試験の期待との関係は未確認 (`guest8/E11-9/`) | H-7 の再開時ゲートで。`audit_test tvram` の GUI での期待はその前に確かめる |
| H-2 | (3) 前景以外の張本人の明示観測 (猶予中に別アプリをクリックしても前景が移らず、形を作れなかった)。(1) は 2026-10-16 に道具を組み替えて PASS、(2) の STOP の連打は 6d5549f で直して PASS (`guest8/H-2b/`) | 観測の形が作れない | 次に gshell の入力・前景を触る段で、観測の手順から考える |
| REPRO-1 | CI の成果物の `bin/gshell.bin` が、材料の変わっていない ce30be3 → 5971615 で content_changed になった (ノートの hsync -n、2026-10-15)。2026-10-16 の調べ (sol、`~/os32-tmp/run/tools1/repro.txt`): 手元の clean ビルド 2 回と CI 相当のパスで gshell.bin は一致、配備物 204 個中 202 個が一致 (差は `vmkernel.lz4`・`kdebug.bin` のビルド時刻と CRC で、想定どおり)。再現せず、CI 側の道具の版の変化 [推] | 配備のたびに無関係な差分が出ると、実機に何が入ったかの照合がぶれる | 次に CI の成果物で材料不変の content_changed が出たとき、その 2 回の CI 成果物と rustc の版を比べる |
| CI-FLAKY-1 | `check-gfx-attach-host`・`check-gfx-kernel-fb-host` の SDK 段が CI の 32 ビット native でたまに落ちる。**原因確定 (2026-10-16、wt/guistall の CI run 38836386732)**: 固定窓 `0xFFF00000`+512 KiB の確保が、ASLR で `ffee5000-fff07000` に置かれた 32 ビットのスタックと重なり EEXIST (f7d8673 の NOREPLACE が出した maps で判明) | たまの失敗が残る | 道具の小さな直し: native の 32 ビット試験を `setarch -R` (配置をランダムにしない) で起動する |
| GUISTALL-1 | 実機 Ra266 の 31k PEGC の GUI で、マウスが時折約 1 秒止まる (体感 3 秒、2026-10-16)。ff6f372 で 82557 結線ありのとき出て、`E82557=0` で出ない。efc5bbe の LAN の速度の直し (送信完了をその場で拾う) を含む 8fa6f09 では、観測あり・`E82557_WATCH=0` とも出なかった (マウス・キーボード)。観測版では 82557 の遅い処理・割り込みの嵐・回復・状態変化は 0 件。GUI 中は送信しないはずで、なぜ消えたかは未解明 [推] | ユーザー判断 (2026-10-16): ループ処理の深掘りは後回し | 再発したら観測版 (ブランチ `wt/guistall`、`E82557_WATCH`) を当てて止まった時刻の行を見る。証拠 `~/os32-tmp/evidence/2026-10-16/gui_stall/` |
| LANFLOW-1 | LAN 版 sfs の連続送信: ゲスト → ホストは応答を待たずに流す、ホスト → ゲストは受信バッファの閾値でゲストが「待って」を返す流量制御 (ユーザー提案 2026-10-16)。いまは 1 要求ずつの往復 (平均 0.85 ms、上限 1024 B) | ユーザー判断 (2026-10-16): T3 以降のどこかへ | T3 の後: 設計票 (astra 起草・Opus レビュー)。82557 の受信の環 8 枚と取り出しの速さの見積もりが中心 |
| SYS16M-1 | 16MB システム空間 (`F00000h`〜`FFFFFFh`) を「切り離す」設定 (`043Bh` bit2=1、PCI 機は `pci_cbus.md` の 44h bit5 と 40h bit1 も) のときだけ、1 MB を RAM として池に足す (上位票の D6 の改め)。いまは設定に関わらず配らず、実機 Ra266 (`sys16m=1`) では 1 MB を捨てている。DMA には使わない (C バスの DMA がブリッジ越しにこの帯の RAM へ届くかは資料に記述が無く未確認) | ユーザー承認 2026-10-16 (DMA の池は T3 で低位の池へ移す、案 5)。配置の固定部分には載せず、2 通りの配置にしない | T3 の後: 小さな依頼 (起動時に `043Bh` とブリッジの設定を読み、RAM のときだけ台帳へ登録)。実機 Ra266 で確かめる |
| DISKPERF-1 | ゲストのディスク書き込みが遅い: 実機 Ra266 で 10 MiB の `cp /data → /data` が 53.6 秒 (0.20 MB/s、2026-10-16)。IDE の HDD としては 1 桁以上遅い [推]。疑わしい所は IDE の転送方式 (PIO・1 セクタずつ)、ext2 の書き込みごとのメタデータの書き戻し | 現状の使用には足りている (ユーザー 2026-10-16) | V3_PLAN の柱 P5 (追加) ディスクの速さ。T3 の後に、NP21/W と Ra266 で内訳を測ってから票を起こす |
| DOCAUDIT-1 | 文書の精査 (2026-10-17) で見つかった、生きている文書から追えなくなっていた未決 3 件: (1) `check-privileged` が `--strict` 無しで呼ばれ落ちない (`build/checks.d/check-privileged.mk:5`)、(2) hsync の保護の試験が inode の比較を見ない (`tools/tests/test_hsync_protect.py:10`)、(3) multiapp の模型 `tools/tests/multiapp_model_host.c` の整理 (R1) | 検査の強さ・試験の網羅の問題で、製品の不具合ではない | 次に検査の道具・hsync・R1 を触る保守の段 |
| T5A-ACC | t5a_display (端末の部品) の未確認: 最終の ELF/BSS/stack の配置、再起動、構成別の表示と性能 (README から外した PM 向けの指示を台帳へ) | 受入の証拠と照合して、未実施の分だけ残す | H-7 (端末アプリの再開) の再開時ゲートで |
| LZSS-1 | 修正した圧縮器の PKG (種 250 の 2 ファイル) を cdinst で入れて照合 | cdinst は MINIMAL / GUI / NORMAL / DEBUG の名前だけを展開し、試験 PKG を媒体に載せる道具の経路が無い。R0 の Full install (225 files、`hsync -n --verify` で内容一致) は代替の観測で PASS に数えない。host の `test_lzss_pkg.py` は合格 | 保守: 次に ISO / PKG の道具を触る段 |
| PRIO-1 | `install` (FD 起動、CD でない方) の通し (授権・区画・format・書込み) | R0 は cdinst Full だけ (閉鎖台帳)。install は F-14 で確認画面まで (n で中断) | 保守: LZSS-1 と同じ機会 |
| E10-4 | `v86 -g` の採取の途中の kill で g/tv の解放・gcap_ops・TVRAM 30 行が戻ること | R1b で観測不能: 起動 0.25s 後の CTRL+STOP でも採取が先に終わる (`E end`、gcap alloc 2・free 2・kills +0)。採取の途中を狙う hook (長い採取か注入) が要る | T7 |
| F-9 | 副 arena が要求量に応じた大きさで作られるため、小さい割当ての継続で arena が N 個に増え、伸長失敗の syscall が N+1 回・free が O(N) になる。f8 の大塊も同じ: 全 free/realloc が大塊一覧を O(N) 走査し、TOPDOWN の大塊が隣どうし併合されるため虫食いの free で extent 表 (32 本) が満杯になると unmap が EFULL で返却されない (一覧は復元、exit で回収)。大塊 realloc で旧 unmap だけ失敗すると成功を返しつつ errno=ENOMEM と旧ブロックが残る (f8 レビュー P3-a/b/c)。f9 の USER exec_heap は 1 回の alloc/free が O(所属 arena のページ数 + ブロック数) で、INITIAL 1 本を走査する (f12ではheap_sz=0の初期量64KiB、旧avail/2は撤去) (f9 レビュー R1)。f10 の trim は全 USER arena (INITIAL 含む) のページ/ブロック検証 + ARENA の末尾走査で O(全 arena のページ数 + ブロック数)。LARGE は非併合・extent/base 識別。最小 arena サイズまたは EXACT 失敗の印を検討・検証する。f12-consumers の実測は終了時 extent 最大 4/32・副 arena 最大 1、heap_sz=0 の 64KiB fallback は確認済み (F-5)。R11性能: 基点/f12の所要時間・mem_map回数は取得不可。cfg_benchホストfixtureはtickを固定刻みで模擬しUSER AS/allocatorを通さず、sqlite_standaloneはKernelAPI/VFSを要するOS32XのためLinux/qemu-userで実行不可。ホストSQLite時間を代用しない。調査証拠: ~/os32-tmp/run/f12/f12b_perf.log **T3c4 (2026-10-18) で副 arena の最小を 64KiB に**: NP21/W 64MB で小割当 N=256 の割当 1.57 秒 → 0.10 秒、EXACT 失敗 32,640 → 426、arena 256 → 20 (`~/os32-tmp/evidence/2026-10-18/t3b3_t3c4_np21w/RESULT.md`)。残り: 主 arena の EXACT 伸長が割当ごとに 1 回失敗する (O(N))、大塊一覧の O(N) 走査、虫食いの EFULL、大塊 realloc の旧 unmap 失敗で errno=ENOMEM と旧ブロックが残る、64KiB の確保が ENOSPC で丸め量へ再試行する経路と生きたブロックを持ったままの trim の余り頁の返却はホストの試験だけ (ゲストで未観測) | 既知の限界 (EFULL・realloc の errno) は性能の良否で閉じない。主な性能の問題は T3c4 で解消 | 実アプリの実測で問題が出たとき、または P6 (実行モデル) の割当器の見直し |
| H-4 | 構成別に閉じていないもの — **17MB Cirrus**: 台本 M の全通し (FEP・高位 HostDrv・lease 切替・map 枯渇・trim・STOP)、`e12-17mb-cirrus` の残りの observe (R3 は guard C・20 回・V86 往復・gdi_test だけ)。**17MB planar**: V86 往復後の alias UC の PTE 照合 (h9 再回 §2 は全画面 DISPLAY・SHM lock・font_load_test・旧 token 失効だけ、alias UC は PEGC と Cirrus で採った)。**cirrus-off (fallback)**: 日本語 present・CLIENT/DISPLAY geometry・旧 token revoke・両 SDK 再 attach・alias UC (R1d は gfx200_test・blit_test・db_test だけ。planar での旧 token / 再 attach は h9 再回 §2 だが fallback 経路での実施ではない)。**8MB planar**: shapes の入口前 ENOMEM の対照、`e12-8mb-planar` の残り (台本 `guest_acceptance.yaml:759` の V86 後の alias UC・SHM lock・font_load_test NOSYS 等 — h9 再回 §4・h_small §1 に観測が無く、17MB planar の証拠では代用しない)、`f12-8mb-startup` (同等の試験 mem_map/sbrk_grow/malloc_arena/exec_heap/alloc_demo は h_small G-9 で PASS、台本 entry そのものは未実行)。**8MB PEGC**: 台本 M の全通し、shapes の非数 P1 (`PREP FAIL`) と入口前 ENOMEM の対照 (台本 `:2472`。R5 の記録は i/ii/iii の成功だけ、非数 P1 は R4 planar でのみ確認)、`e12-8mb-pegc` の残りの observe、`f12-8mb-startup` (R5 は guard・db・shapes 成功例・20 回、h9 再回 §5 は G-1〜G-4 と guard D)。**64MB**: (a) 高位 buffer の通過の証明、(b) GUI での再生の park/resume、(c) PEN (CS4231 I9 bit0) と 8237 DMA マスクのレジスタ読み | 時間の都合で各回の M を 17MB PEGC と 8MB planar でしか通していない (h9 再回 §1・§4)。(a) はアプリのページが pool の下から割り当てられ (32MB 超 0 本) 32MB 超に I/O buffer を置く fixture が無い、(b) は GUI で鳴らす fixture が無い (CUI の pcm_test は OP_WAIT に入らない)、(c) は読む API が無い (IMR の変化と STOP 後の解除は PASS)。閉じた分 (R1d・R3〜R6・h9 再回・h_small の対応表) は閉鎖台帳 | Cirrus / planar / cirrus-off / 8MB の残り → [関門の表](#関門の表) の「v3 の最終段」 (派生 ini は t2h_cirrus / t2h_8mb、M は `guest_acceptance.yaml` の h-R*-* 行)。(a) → T3 (帯変更、fixture と一緒)、(b)(c) → TASK_PCM_CS4231 |
| H-6b | V86 の出口で 6Ah の標準 / 拡張を戻していない (9821 で E0000h が MMIO のまま残り得る)、9801 構成での `gui_gate` | 未対処。NP21/W では `v86 -b` / `-d` / hal_test の前後で 6Ah=0x01・mode1=0x88・E0000h/A8000h の読みが不変 (H-6a、記録) で表示は壊れていない。9801 構成はこの NP21/W に無い | T7 (V86 復元検査)。表示が壊れる構成が見つかれば e10b の fix 段 |
| E11-A2b | raw KAPI 待機利用者の明示 check | host だけで、ゲストに観測が無い。T4b-7 の [fixture・PM 手順](../../tools/tests/raw_wait_tdd.md) を用意 | T4b-7 の PM ゲスト受入 |
| X-1 | NUL 終端の文字列入力と `kprintf` 可変引数の B1 化 → T4・T5a の担当境界で確定。ページ 0 の NP 化 → T7 (元の行 1499、1647、1757、1885) | 既決の既知の制限 | T4 / T5a / T7 |
| X-4 | 検査の整理の残り: ci-stab2 の P2-B の後の全体 check-changed と native の記録、6 時間超の対照の刈り取り、pending の 1 時間回収、生き残る弱い変異、f5a の full 候補検査に残る 5 SKIP、`net_link` の TMPDIR 長。**選択の取りこぼし 3 種は 2110924 で閉じた** (設定ファイルの変更は full、成果物読みと動的な生成物パスを lint で検出)。そのとき残った指摘: `check-disk-auth-host`・`check-ci-stab-host` の対応が広すぎる (余分な候補、見逃しではない、astra 3 往復の後に PM 判断) | T2h に観測が無い | 次に `check_select.py` / ci-stab を触る保守の段 (遅くとも T3 の最初の取り込みの全体検査) |
| X-11 | 監査分類 (`~/os32-tmp/evidence/2026-10-07/audit-classification.md`) の「意味変更 (e11c)」のうち pipe (E11-8、閉鎖) を除く 12 件 — IME 9 本 (trygetchar/toggle/set_mode/switch_dict/user_delete/user_export/user_clear/trygetkey/feed_key)・exec_last_result・gui_call・con_sink_read の授権/本人別の契約 (分類表 :33/:43-55/:57/:60) | **T4 の設計票で扱う (ユーザー決定 2026-10-07)** | T4 |
| X-20 | gtool1 再レビューの P3: (N1) OCR の読めないセルと本物の `?` が文字列上で区別できず `\?`/`.` が一致する — 読めないセルにも私用文字を割り当て一致させない、(N2) は gtool3 で閉じた (launch が `since` を返す)。gtool2 レビューの P3 (G2-4 i686 記述子の照合を check に、G2-5 動いている像と ELF の照合)。abort_target を読む道具 (G-7 で導出に頼った)。h3fix レビューの P3 (mem_stat_test を os32_test_summary に、h3 の layout に gshell.elf の sha、単独 stop の後は stop の capture を trace に、v1 経路の試験)、h1fix の P3 (配備元は target/ の外の注記)。ci-r7 の P3 (build.yml の照合失敗時に両方の sha256 を CI ログへ tee、ISO / PKG 内の vmkernel.lz4 の照合)、h1c の P3 (PRUNE_STALE に HOSTDRV_DIR を渡す、08_build の deploy-kernel 行に「make all 済みの木で」と gen_deploy_set の OSError 文、deploy.mk の recipe 内コメント)。gtool3 レビューの P3: launch が送信前の consink の例外で止まる (fail closed、害なし)、hold 中に np21w の core_lock が失敗すると鍵が押下のまま残る (`core_busy`、台本 g-G-3 に「ok:false なら同じ seq を hold 無しで 1 回送って離す」を足す、Fable 再レビュー R-2)。R1c の気づき: 試験像 (`kernel-r1`) では `Gui.launch` に `OS32_KERNEL_ELF=build/out/r1/kernel.elf` が要る (注記候補)。h3 の道具: 前景 (RUNNING+in_wait) の fixture を arm するときは先に別窓を前景にする手順が要る | 道具の改善 | 保守 |
| PRE-1 | `tools/tests/test_preflight.py` がどの `make check-*` にも `tools/check_map.d` にも載っていない (smallfix の Fable レビュー S-2、2026-10-14、それ以前から)。preflight を変えても全体検査で走らない | 道具の不足 | 次に preflight を触る段で `check-preflight-host` と対応表を足す |
| S-1 | Windows opt-in の集約 9 件のうち所在を特定できない 4 件 | R0 で 5 件 (`test_np21w_trial.py --windows-parser` 1 + `test_np21w_ini_live.py --windows-fixtures` 4) は PASS。残り 4 件は台本の数え方の確認から | 保守: 次に Windows opt-in を回す前 |

## 3. 持越し (決定)

| ID | 何を | 種類 | 関門 |
|---|---|---|---|
| H-7 | apps / game: v3 では組まない (ユーザー決定 2026-09-30)。T2h では再開時ゲート (caller 追随・再ビルド・受入の一覧) の引渡しを確かめた。v3 の完了条件ではない。再開時は **c2 以後の SDK で再ビルド必須** — 旧 libos32gfx.a の外部バイナリは v70 で互換 token の VA を得て取り直さず、200 ライン化後に kill され得る (e11c2 レビュー R6)。**memory_layout=2 (e11b2) 以後、再ビルド前の apps/game の成果物はロード時に世代不一致で必ず拒否される** (旧 shlib の拒否は R1c-2 で実証)。外部の Makefile は T2c 以後の `link_guard.py` 経由のリンクにも未追随 (2026-10-08 の `make external` で mkos32x が拒否)。f7 以後は SDK の CRT が `libos32nano.a` を要るので、外部 Makefile のリンクに `-los32nano` を `-lc` より前に足す (f7 レビュー P2-1)。**端末アプリに依る未実施をここに束ねる**: E11-5 の端末の子・隠れた sh、E11-9 の GUI 端末の tvdump 0 埋め (元の行 1113、1248、1810–1826、1868、3463) | 再開時ゲート | apps / game の再開時 |
| S-4 | 外部 apps / game の再結線 | 適用外 → = H-7 | 同上 |
| E11-A3 | 全画面 owner の同期の子 (slot.parent) は kernel が読ませるが WM の起床の手がかりに入らない。旧「端末由来は全員が読める」はGUIRUNKEY-1 (2026-10-18) で変更済み。読み手とslot/launchの起動連鎖だけが取得する。全画面の起床手がかりの残件は維持。**PM 決定 (2026-10-07): 専用 KAPI `kbd_inject_to` は e11c に入れない**、**U6 (ユーザー決定 2026-10-10): 足さない** — 全画面への矢印・機能キーの配送は E11-5 の実測で判断 (残り 2 点は E11-5 行) | 改善 (決定による持越し) | 全画面の入力に不足が出たとき |

## 4. 実機 Ra266 (R7、2026-10-12、9545e27)

| ID | 何を | 種類 | 関門 |
|---|---|---|---|
| H-5 | 実機の残り: CG 窓の WB、表示の後始末 (GRCG / EGC・68h の残り)。kernel stack の high-water は **10/16 に T3 の票 §0 の表へ転記して閉じた** (86ba40c で kstack=3,544 B、10/14 は 4,348 B)。UC 化で present が遅くならないことは閉じた | 構成持越し (実機) | CG 窓の WB と 68h の後始末は実機に読む口が無い (設計票 §8 (m) で公開の物理 read は作らない) → **読む道具が要る、T7 (V86 / 表示の段) へ渡す** |
| R7-2 | 実機 Ra266 の PCM: pcm_test は under=0・rep=0・resync=0・close 0 で 5 秒鳴ったが、**不規則な音切れ**をユーザーが聞いた (カウンタに出ない欠け — DMA 境界・auto-init の折返し・CS4231 FIFO / XTAL2・IRQ の遅れが候補)。起動行は `[pcm] CS4231 v=101 irq 10 dma 1 fmt 0x5B` (NP21/W の 64MB + SNDboard=64 では v=100) | 実機の不具合 | TASK_PCM_CS4231 |
| R7-3 | 実機 Ra266 の GUI で、KANA をオンにしてマウスキーでカーソルを動かすと、スピーカーからうっすら「ププププ」とクリック音のような音がする (ユーザー観測 2026-10-12、使用感に問題なし)。キーごとのビープ / ポート 37h・35h の操作 / PIT ch2 の扱いが候補 | 実機の事象 (記録) | — |
| R7-4 | 実機 Ra266 の GUI で、起動の初めに画面中央に描かれたマウスカーソルが残る (ユーザー観測 2026-10-12)。**動かした後のカーソルは残像を出さない** — 残るのは起動時の最初の 1 回の描画だけ (初回の背景の保存より前に描いた、または最初の合成がカーソルの位置を描き直していない)。gshell の起動時の `cursor` の初期位置 (`lib.rs` の st.mouse_x/y = 画面中央) と、最初の合成・カーソルの保存/復元の順が候補。NP21/W では未観測 | 実機の事象 (記録) | — |
| R7FIX2-1 | R7FIX2 段 1 の受入 (6) のうち、CTRL+ESC の後にカーネル側の `[gfx] px` 行 (n>n0) が a の bb/vr を新値で出すこと (GUI 中のシリアルを NP21/W で読めず未確認。gserial の px は変化を確認済み) | 試験の未実施 (道具) | 実機の段 2 の採取 (シリアルで読める) |

## 5. 一括ゲスト一覧に入っていない受入

`tools/tests/guest_tests.txt` (一括) と `tools/emu_agent/tasks/regress.txt` (6 項目) には `d0a_test`・`kout_test`・fault 系・STOP / park-resume・GUI 受入が無い。
一覧を回しただけでは受入一式の代わりにならない — これらは段の受入で個別に回し、結果を票に 1 行書く。
一覧への追加と「台帳に無い SKIP・未起動の必須試験を失敗にする」照合は [REVIEW_2026-10-06 §5 の b](agents/REVIEW_2026-10-06.md)。
SKIP の登録は閉鎖台帳の S-2・S-3・S-5〜S-8 (T2h で閉鎖) と上の S-1・S-4 だけ — ここに無い SKIP は延期ではない。
