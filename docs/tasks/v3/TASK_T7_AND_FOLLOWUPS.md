# TASK_T7_AND_FOLLOWUPS — 低位解放・OpenType と v3 後半への接続

> 上位: [V3_PLAN](V3_PLAN.md)

> 状態: **受入完了・実機確認待ち (2026-10-20、T7a・T7b の全段が main に着地し NP21/W で受入、残りは実機 Ra266 での確認 = 台帳 T7B-REALHW)** — **T7b-b5 受入 (`~/os32-tmp/evidence/2026-10-20/t7bb5a/RESULT.md`・`t7bb5b/RESULT.md`)**: b5a で Latin-1 を半角・前景移動の EV_FOCUS。b5b で ①計測 (NP21/W: cold 1000 字 seq 2.9 秒・scatter 6.8 秒、最初の text() 0.14 秒、gshell の main→最初の合成 0.29 秒、FD peak 5/16、gshell stack 10,184/40,960 B) ③FD 起動 (SQLITE_ONLY・MINIMAL で lease は NOTFOUND→KCG、CUI で確認) ④破損・短読・上限超過で操作継続 ⑤x87 復帰 (fp_scope が呼び手の例外フラグを消していたのを FNSTENV/FLDENV に直して合格) ⑥trim・設定変更・終了 各 20 回で増加 0、S7 の残り (頁戻り 16 頁、STOP) 合格。未確認は ② D15 (P100 で)・FEP の半角カナ・gshell の guard callback 中の STOP → 台帳 T7B-REALHW。論点: 16px slot 682 に 1000 字の画面は入らない。 — **T7b-b1/b2 受入 (S3b、`~/os32-tmp/evidence/2026-10-20/t7bs3b/RESULT.md`)**: lease・limit・other/子、leak20+fault 後 FD は fep.db 1 本で lease 全 FREE、fixture=OK、probe は INVAL、GUI 中の hsync 置換は BUSY。**T7b-b3/b4 受入 (S7、`~/os32-tmp/evidence/2026-10-20/t7b34s7/RESULT.md`、main の S7 取り込み)**: b3①②③④⑦・b2⑥・b4①〜⑥ 合格 (③④⑥ は条件付き)、受入中に U+00A5 が `\` に見える誤りを直した。未確認 (b5 で回す): b3⑤ のゲスト、b3④ の頁戻り、b3⑥ の STOP と 20 回、FEP の半角カナ。**b5 で直す (PM 決定、推奨どおり)**: Latin-1・Latin 拡張 (U+00A0〜U+024F) を半角 8px にして Inconsolata で描く (U+00A5 は ROM の ¥ 8px、ギリシャ・キリルは全角のまま) — 今は 2 バイト UTF-8 が全角の枠で「Å ngströ m」と隙間が出る。字を持つ app の終了で前景が移ったら EV_FOCUS を出す (NOSPC の app の再試行)。 — **T7b-a6 (2026-10-20、`~/os32-tmp/evidence/2026-10-20/t7ba6/RESULT.md`、NP21/W clk_mult 140、P100 の値ではない)**: a5 の 50 秒超は東雲の open 29 秒 (cmap format 4 の 4 配列が read cache 2 枠を奪い合い read_at 16,479 回) — `READ_CACHE_SLOTS` 16・`READ_CACHE_SLOT_BYTES` 1024 にして open 0.13 秒、1000 字 seq 2.8 秒・scatter 11.7 秒 (旧 17.9・18.6)、FONTCHK は不変。FD は待機中 1 本 (fep.db)、アプリの SQLite 書込みで最大 3 本。**PM 決定 (Fable の指摘でアプリの平文 open 2 本を足す)**: `VFS_FONT_FD_RESERVED`=9・`_MAX_SHELL`=2・`_MAX_APP`=2・`_MAX_APP_TOTAL`=2 (9+2+2=13、park 中と前景の両方が font を開けば後の方は NOSPC→KCG)。**採る (PM の推奨、票の「a6 で決める」の範囲)**: (a) 製品の Face は値で返さず static/arena にその場で開く (17KB、gshell の stack 40KiB、試作で 32×2KiB が stack あふれ) (b) cmap を常駐 (東雲 +33.5KB、未 cache 字 11.7→約 6ms の見込み)。(a)(b) は b1 S1 の前の小段 a7 で入れる。未測: FEP 学習の journal・`<` redirect・入れ子 loader (exec.c:2030-2046 で main 前に close するので同時 1)、shlib の open。帯は shell 枠 741,376B に gshell 271,552B + font 見込み 154KB で約 185KB 残る。 — **T7b-a5 受入 (2026-10-20、main 4c3b633、`~/os32-tmp/evidence/2026-10-20/t7ba5_np21w/RESULT.md`)**: ホストで受入①〜⑤ (東雲 16ppem は EBDT 原本 7,059 字と全一致、全 corpus で代替 0、変異 15 本 RED、386 命令検査、x87 と SSE で 1bpp 反転 0)、NP21/W で `font_test -c` の FONTCHK 3 行が期待値と一致し見本 16/32px を目視。観測: 3 書体 1,483 字の逐次読み raster に 50 秒超 → a6 で測る。持越し: t5a_display.elf の bswap 1 件と `check_isa386.py` を全成果物へ広げる件、短い dst を倍化前に断る改善・SSE 綴りの明示禁止 (Fable の軽 3・4)。**一時体制 (2026-10-20、ユーザー指示)**: 設計は Fable が書き Opus がレビュー、実装は Opus が書き Fable がレビュー (Fable の枠を節約、sol/astra の枠切れ)。b1・b2 の詳細設計は §3 の表の後。b2 S3a 着地 (aef747a、Fable レビュー可)。 — **ユーザー決定 2026-10-19: GUI の標準フォントは 1bpp** (グレー・KCG ROM の 16px 和文は採らない。きれいさの目安は Win98 相当)。**ユーザー決定 2026-10-19: GUI の標準の和文フォントは JF ドット 東雲ゴシック 16** (配布セット 20150527、TTF 1,954,728 B、sha256 `5e265e45…7cc`、東雲 0.9.11p1 由来で実質 public domain、JF は独自の権利を主張しない)。16ppem・unhinted・閾値 128 で原本 BDF と画素一致 (JIS 6,879 字中 ¢£¬ の 3 字だけ cmap が半角形を選ぶ)、上限超過 0 (輪郭最大 24・点 184)、JIS X 0208 100%。**32px は 16px の 1bpp を最近傍で 2 倍** (輪郭を 32ppem で描き直さない — 半画素の境目が黒く入る)、字形 cache の 32px slot は使わず arena を 16px slot に回す (設計 T2)。欧文は Inconsolata (東雲の半角は `\`→円・`~`→上線の旧 JIS 割当のため採らない)。IPAex は標準ではなくなる (配布を続けるかは b3 で)。調べと比較の見本は `~/os32-tmp/run/shinonome/`。IPA を選んだ理由は経産省のフォントという以外に無かった (ユーザー)。**字形の bitmap cache の設計は確定** (astra 起草・Opus 3 往復、`~/os32-tmp/run/glyphcache/DESIGN.final.md`): owner ごと (gshell と前景アプリ、「KCG 相当 64KiB × 2 系統」) に 64KiB を一括確保する arena、中は 16px 用・32px 用の固定 slot (20×21・36×37、1bpp) と age による近似 LRU、key は (font_set, codepoint, px) で hit なら cmap を通らない、KCG/代替へ落ちた字も印の entry、face/設定の変更や age の満了は arena 全体を無効化、gshell は trim で arena を手放さない (app は返す)、slot を超えた字は uncached で描いて数える。a5〜b3 はこの設計に従う。システムフォントは入れ替えても成立させる — 条件は TrueType glyf・cmap 4/12・字ごとの上限内 (超える字は KCG/代替)・配布してよいライセンス、切替は §2-3 の安全点の手順と face 世代で。埋め込みビットマップ (EBDT) の対応は範囲外 (後から足せる)。 **ユーザー決定 2026-10-19: font の読み込みは逐次読み (案 A) の系譜 + キャッシュ** — 一度は「小さい表だけ常駐」(案 B) を選んだが、A が実装・全符号位置の対照試験済みで B でも glyf の切出しと directory の自前は残る (差は数百行) こと、B は IPA 1 書体で約 336KB 常駐することを見直して A に戻した。速さはキャッシュで補う: read cache は LRU で枠の数を定数 1 か所で変えられる形にし、cmap を常駐させるか枠を増やすかは T7b-a6 の cold/warm 1000 字の実測で決める。IPA は 2 書体とも扱える。 — **ユーザー決定 2026-10-19: 欧文フォントは Inconsolata Regular 3.000** (SIL OFL 1.1、108,684 B、sha256 `127875d2…12b7c`、取得元 googlefonts/Inconsolata v3.000 の fonts_ttf.zip)。候補 5 本 (Inconsolata・Vera Sans Mono・DejaVu Sans Mono・JetBrains Mono・Source Code Pro) の表・実測・1x 見本は `~/os32-tmp/run/t7b-a1/report.md`。16ppem の送りがちょうど 8px で横縮小が要らない。T7b-a1 の調べ: Latin 191 字と JIS X 0208 6,879 字 (IPAex) の上限案 (点 512・輪郭 32・component 8/深さ 4・span 2KiB・bbox 40px、作業域 32KiB+出力 2KiB) を超える字は 0。ttf-parser は既存 0.21.1 を MIT で vendor 化し、公開の loca/glyf API に字形単位の一時 table を渡す薄層で read_at の読み込みが作れる (自前 reader 約 900〜1,400 行)。**残る設計点**: EM=16px を 16px セルに入れると全候補で Å の上・ç の下が欠ける — line-height と EM の扱いを a5/b4 で決める (縦縮小や個別補正を黙って入れない)。**T7a の節目の受入 — T7a 完了** (dea3edd、`~/os32-tmp/evidence/2026-10-19/t7a_milestone_np21w/RESULT.md`): T7a-1a〜2e の合格項目を最終版で 1 回ずつ回して全部合格 (HDD 通常・kcg.mod 無し・planar・Cirrus・FD 2HD 書込み禁止)、T5 の最小の回帰と IME の約束も合格、製品の異常 0。FD 空き 2HD 82KB / 1.44MB 295KB。記録: NP21/W の `[display] A9 code=28 dot=28 restored=28 vsync=0/0/0` (CODE/dot の差は実機で)、GUIRUNKEY の通知まで 16.6 秒 (T5 は約 7 秒、原因未調査)。全体検査 b6a686b。台帳 E10-4・T7-V86CTX・H-6b は閉鎖。**T7a-2e・KBDMOD-1 受入** (dea3edd、`~/os32-tmp/evidence/2026-10-19/t7a2e_np21w/RESULT.md`): 通常の V86 が直接低位で `-t`・`-g -t`・STOP とも成功、session の間の空き頁は不変 (旧 backing 159 頁を取らない、V86 時の peak −636KiB — 起動直後の空きは前と同じ)、GUI→CUI→V86→GUI 20 回で空き頁・KHEAP・leftover・restore_mismatch・shift 状態が不変、生成地図の V86_LOW (160 PFN、FIXED/kernel/WB、eligible 0) とゲストの区間表が一致。KBDMOD-1: V86 の前の STOP と V86 中の SHIFT の後も OS32 の shift 状態は 0。未実行: `v86 -b` (DOS の NHD 無し)、09A0h の直読み (API に I/O 読みが無く mode2=01 で代用)。最後の全体検査は b6a686b (T7a の節目)。**T7a-2d 受入** (ebed6de、`~/os32-tmp/evidence/2026-10-19/t7a2d_np21w/RESULT.md`): 採取 active 印 (rec!=0・keep_if=1・g/tv 差 2) を読んでから V86 中に CTRL+STOP → HOTKEY で戻り kills 0、g/tv 差 0・rec/ops/owned/tv/keep_if 0・CR3 復帰、直後の `v86 -t`・`-g -t` 成功、正常の `-g -t` は表示に触らない。hostdrv の名札は build/out の版。未実行: #PF/#GP → kills+1 (ゲストで採取中に例外を起こす口が無い、host で確認済み)。**STOP が V86 の外で届くと ABORT 経路で kills+1** (後始末は同じく効く) — ② はこの 2 経路。**見つけた不具合 KBDMOD-1**: V86 の外の STOP の後、CTRL の離しだけが次の V86 ゲストへ渡り OS32 の shift 状態に CTRL が残る (kbd_deliver が V86 中は修飾キーも全部ゲストへ回す) → 修正中。**T7a-2c 受入** (cbb79d4、`~/os32-tmp/evidence/2026-10-19/t7a2c_np21w/RESULT.md`): 新 ABI の kcg.mod が PAIR rc=0、V86 3 回 (通常・STOP・-g -t) の後に kcg の cache が全失効し、読み直しで misses +100・字形の hash は前と同一 (CODE 復帰)、GUI の日本語の再描画も正常、kcg.mod 無しは CODE だけ戻る、9821 相当 (045Ch=40・GRCG/EGC あり) の PEGC 480 と planar で V86 後の画面が正しい。NP21/W の固定診断 `[display] A9 code=00 dot=00 restored=00`・CG 窓 WB・VRAM UC (記録)。⑤ 9801 の gui_gate は確かめない (ユーザー決定 2026-10-19: 9821 は後方互換があり 9801 構成の互換は担保しない、台帳 H-6b は閉鎖)。気づき: 固定診断の A9h が 'A' の空白のラインを読んでいて CODE/dot を見分けられない → 実機の回の前に字とラインを変える。**T7a-2b 受入** (27296d3、`~/os32-tmp/evidence/2026-10-19/t7a2b_np21w/RESULT.md`): 通常の page0 は NP (PTE[0]=0x2) のまま起動・GUI⇔CUI・FEP・filer・edit_gui・hsync・PEGC/planar で fault 0、page0 を読んでいた sysclk・memory_boot・kbd・crt_diag の値は前と同じ、V86 3 回 (通常・STOP・-g -t) の後で空き頁/KHEAP/leftover 不変・restore_mismatch 0・物理 page0 は原本と一致 (NP21/W 本体の書く 4B を除く)。気づき: hostdrv sync の名札が build/out ではなく git HEAD を出す (道具)。**T7a-2a・T7a-1 の後追い 受入** (e51b5b8、`~/os32-tmp/evidence/2026-10-19/t7a2a_np21w/RESULT.md`): CPL3 から `v86 -t`・`-g -t` が成功し後の KAPI も正常、OK→STOP→OK の 3 回で空き頁・KHEAP・leftover が前後で不変 (pin/staging 残り 0)、STOP 直後の再入 2 回とも OK。KHEAP は mailbox で常駐 +880 B、`-g -t` の peak +9,352 B (見込みどおり)。KCG 無しの理由の 1 行が GUI を求めたとき画面とシリアルに残り、断った描画アプリは exit 1 で fault 0、旧 mincho.kcgfont は prune で消えた。気づき: prune_stale.py が知らない引数を黙って捨てて dry-run になり NHD をマウントしたまま残す (道具の癖)。**T7a-1b・T7a-1c 受入** (d6fc417、`~/os32-tmp/evidence/2026-10-19/t7a1bc_np21w/RESULT.md`): 旧 font は配備の prune で NHD・HostDrv から消え、KCG 部品だけで日本語 (File Manager の名前・IME の変換) を表示、実 ROM の固定 1000 字は cold 37.0ms (hit 90%)・warm 31.1ms (100%)、kcg.mod 無しは起動継続・os32gui と描画アプリは描画せず CUI へ、KCG 部品で常駐 +17 頁。PEGC/planar の識別・boot-sync の値は前と同じ、V86 3 回 (通常・STOP・-g -t) の後の物理 page0 は原本と一致 (NP21/W 本体が書く 0x4F1-0x4F3・0x5BB を除く — エミュレータの仕様、⑥ の照合から除外)。後追い: KCG 無しの理由の 1 行がシェルの起動で消える・断った描画アプリが [Process crashed] になる・HostDrv に旧 mincho.kcgfont が残る。未実行: boot2/DOS (DOS の NHD 無し)。**T7a-1a 受入** (fdb7783、`~/os32-tmp/evidence/2026-10-19/t7a1a_np21w/RESULT.md`): planar の BB を boot の連続 pool 32 頁 (0xED0000) に、gshell への移譲は最初の GUI 入りの 1 回だけ (BOOT→GSHELL)、GUI⇔CUI 2 往復で空き頁の増減 0・leftover=0、KDEBUG=1 の selftest 229/229 (test_gfx_ledger 含む)、PEGC・Cirrus の往復も回帰なし。既定構成の起動直後の空きは T5 節目から −32 頁。気づき (合否外): planar の起動ログ `[mem] bb=64` (planar 分 32 頁の 2 倍、数え方未確認)、配備直後の最初の起動だけ exec_peak 65544。全体検査で旧 BB 前提の host 試験 2 本の追従漏れ (fdb7783 で直し)。以下は設計の版の記録 — 本文を全部持つ実装用設計案。本版は **r6 (2026-10-19、T5後の再照合反映)**。共通依頼の状態行は維持し、旧版の Opus 5.5 第3回 Approve は本版の承認・実装・受入とは区別する。
> **T5後の再照合 r6 (2026-10-19)** — 基点 `846dbeb`、VK32 220,607B・core raw 362,288B・常駐末尾0x18c000。照合記録は `~/os32-tmp/run/t7recheck/RECHECK.md`。部品枠10/10、T7は既存部品・gshell静的リンク/shlibを使い、診断は既存kdebug/試験像、splashはcoreのまま。低位番地・頁数は一致し、NP化・予約統合・backing撤去はT7に残る。T7bの既存ゲートは維持し、shell1MiB/stack40KiB・私有2MiB・FD peak・P100/32MB性能は未測定。engine stack32KiBのhigh-water 4,668B・残28,100Bは入口実績で、T7経路の深さを保証しない。T7a-2cの9821再描画・6Ah/68h/CODE・CG窓WB、-2eのRa266 -g/boot2/DOS/page0原本引渡しと実機計測はT4T7-RHへ送り、段のNP21/W受入を止めない (D15は最終段、Ra266値と別)。台帳の10月19日実績 (LAN hsync・実音・v86 -t・GUI・FD SQLITE_ONLY) は維持し、#DF試験像・STOP後再open・-g -t/DOS/boot2/page0は残件、PCMCRACKLE-1は別件延期。LANFS/L-F完了はT7の入口条件にせず、CDはFD/HDD起動後のインストール元だけとする。
> **ユーザー決定 (2026-10-17)**: 「T4 から T7 までは実機でテストを行わず、エミュレーター上だけで確認を取れれば大丈夫」。真意は「ソフトウェアのコード変更はエミュレータでの確認で十分。ハードウェアに触るカーネルの変更は実機のテストをまとめて行う」— 各段の合否は NP21/W (64MB) で決める。本文で実機の観測を求めている項目 (T7a-2c の 9821 実機の再描画と H-5 の CG 窓、T7a-2e の Ra266 `-g`/boot2/DOS、T7 §3-2 の Ra266 の計測) は、ハードウェアに触る変更がある程度たまった所でまとめて行う実機の回 (台帳 T4T7-RH) へ送り、段の受入を止めない。本文の「実機未確認を NP21/W だけで閉じない」系の記述は、この行が上書きする。
> **レビューの経過 (PM、2026-10-16)**: astra が起草 (r2・r3) → Opus 5.5 が独立レビュー 2 往復 (r1「直してから」、r2「確定してよい (未決の争点はユーザーへ)」)。往復の記録は `~/os32-tmp/run/t3design/T7_AND_FOLLOWUPS/` (`opus_r1.md`・`astra_r2_response.md`・`opus_r2.md`)。F5 は return PC 検査を維持、F7 は CPU 命令だけならメーカー資料必須・既存実装任意に決定済み (§7)。本文へ反映済み、実装と未測定ゲートは残る。
> 設計: Codex。再照合基点 `846dbeb` (2026-10-19、main、作業木差分なし)。旧調査基点 `3c4171a`、元の設計レビュー基点 `59c4285`。D1〜D36・契約・順序は変更しない。file:line はこの再照合基点、相対リンクは正典の `docs/tasks/v3/` 配置を基準とする。
> 決定本文は [MEMMAP_V3](../../decisions/MEMMAP_V3.md)、柱/範囲/順序は [V3_PLAN](V3_PLAN.md)。前提は `TASK_T3_LAYOUT` と `TASK_T4_T6_MODULES`。後半の既存票を置換せず、接続契約と未起票項目の着手ゲートを定める。

## 0. 再照合

単一利用者・単一タスク・協調切替、攻撃者を想定しない。検査はバグ・装置の誤動作からの復元と像の予算のためで、認証・暗号・権限分離強化は追加しない。性能は P100/32MB で D15 を満たす範囲に限る。上位決定は `MEMMAP_V3.md:17-54`、再照合規則は `DOCS_REORG_T3.md:60-63,86-93`、装置の根拠は `docs/CONSTRAINTS.md:92-98` [HW3] に従う。

| 元票の前提・着手条件 | 分類 | 今の根拠 / r6 での扱い |
|---|---|---|
| T2 の e5〜e12 の再init/revoke/再attach・200行・Unicode橋は引渡し待ち | 吸収済み | T2e 受入済み (`TASK_T2D_T2H.md:190-227`、閉鎖台帳 (アーカイブ: `docs/archive/v3/DEFERRED_CLOSED.md`))。公開leaseを基盤として引継ぐ |
| f2〜f13/g の map・allocator・trim は引渡し待ち | 吸収済み | 閉鎖台帳 (アーカイブ: `docs/archive/v3/DEFERRED_CLOSED.md`) の F/G 各行。通常文脈の安全点を再利用し、font独自の借用guardは追加する |
| h の V86/STOP 復元が未確定 | 要改訂 | 既存session/正確なPTE復元・通常文脈回収は `kernel/v86/v86_mem.c:50-214`、`mm/paging.c:656-687`。未観測は台帳に残し §1 で受ける |
| T3→T4→T5-0→T5a→T5b→T5c→T7a→T7b | r6で更新 | T5-0はstack計測・KSTK-DF。engine stack 32KiB + NP 1頁で決定済み (深さ100のhost実測24,388Bが収まる、SQLITE_MAX_EXPR_DEPTH=100のまま、ユーザー決定2026-10-18)。T5bはcore FAT回帰とPOST_ROOT CD、登録核T5c-0をCDより前へ。FD容量不足時だけT6b-Mを先行。EARLY/HostDrv分離段は削除。集積拡張T6b-1/2は凍結し、圧縮像が508KiBの8割 (406.4KiB≈416kB) に近づいたら起票。T7はT5c-1のKCG facade/cache・T5c-3の原本/V86復帰口とT5最終一式を受ける (T6b-Mは不要) |
| planar BB の旧低位 backing をpoolへ移す | 維持 | `include/memmap.h:122-123`、`mm/memory_boot.c:208-220`。CLIENT/SURFACE契約は維持してbackingを替える |
| Unicode の暫定RO leaseをT7まで残す可能性 | 要改訂 | 撤去はT3の組表/KAPI化と同時 (経緯: `docs/archive/v3/DOCS_REORG_T3.md:60`)。T7aは撤去済みを照合する |
| `.kcgfont` 廃止とKCGのpool cache (64KiBは計上値) | 維持 | 旧低位cache/boot読込は `drivers/video/kcg/kcg.c:41-44,196-203` に存続。T5c module化とT7a caller撤去の境界を照合 |
| 旧font KAPIをNOSYSにする | 吸収済み | boot終了後のNOSYSは `drivers/video/kcg/kcg.c:188-203`。T7aはboot読込も撤去し旧slotのNOSYSを保つ |
| CUI表示復帰と両ページ消去 | 吸収済み | `kernel/v86/v86_io.c:238-271`、`gfx/gfx_core.c:899-912` とT2受入。KCG CODE/cache、6Ah標準/拡張、CG窓WBの残件とは別 |
| page0 snapshot は0x600、全ページ化に+2560B | 吸収済み | `kernel/v86/v86_snapshot.c:9-26`、`kernel/v86/v86_mem.h:115-121` は既に4096B。+2560Bは再計上しない |
| 起動時不変snapshot・page0通常NP・BIOS直読撤去 | 一部吸収済み | 起動時原本は保存済み (`init/kernel.c:197-198`、`kernel/v86/v86_snapshot.c:9-26`)。T7は原本不変の回帰と、残る3経路 (`drivers/video/pc98/backend_pegc.c:477-514,571`、`gfx/gfx_core.c:379`) の直読撤去・NP化・直接低位化を担当 |
| 636KiB backing撤去とmailboxの全session排他 | 未測定 | `kernel/v86/v86_mem.c:66-103`、`include/memmap.h:127-128` (868B)。U7/U14は §3 の実証後だけ閉じる |
| KCG cold 1字34 I/O | 要改訂 | `drivers/video/kcg/kcg_body.c:42-65` はOUT34+IN32=66回、さらに `kcg_wait` 34回 (:18)。時間は未測定 |
| 診断・gcapの所在は旧kernel一体 | 要改訂 | DBGREF S0〜S3受入済み (`TASK_DBGREF.md:3`)。gcap本体は `kdebug/v86_gcap.c`、`kernel/v86/v86_gcap.c:6` はops呼出し/NOSYSの受け口。結線契約も §3 で引継ぐ |
| PEGCの低位直読撤去で既存port probeを残す | 維持 | R7FIX2後の窓はFFF00000h (`include/memmap.h:335,351`、閉鎖台帳 R7-1)。F00000hへ戻さず、保存BIOS値との比較だけ差し替える |
| font read leaseと使用中の更新拒否 | 更新 | FD/owner世代は検証する。FEP辞書と同様に使用中のhsyncはBUSY、新フォントは次の起動で読む (§2-4) |
| parser公開API・欧文選定・corpus実測上限・1MiB/2MiB・FD peak・x87 | 未測定 | `userland/rust/font_test/Cargo.toml:10-12` と `userland/rust/font_test/src/lib.rs:274-282` は全ファイルread/Face::parseの試作。製品streamingの証拠ではない |
| P3/P5 NIC とnetwork /hostは後続のみ | 要改訂 | LAN LB-3・LANFS/L-Fの実機hsync受入済み (`docs/tasks/realhw/TASK_LAN_82557.md:3,8`)。§4の接続先として記録し、T7の入口条件にしない |
| 8MB/17MBを各段で受入 | 要改訂 | V3_GOALS §3 (確かめる時期)、台帳:17。途中はNP21/W 64MB、容量別はv3最終段。Ra266固有の観測とD15目標は免除しない |

**2026-10-15時点の実測 (履歴、T7の現予算ではない)**: `build/out/kernel.map:1576-1578,4452,8011` は load=0x100000、max=0x99000=626,688B、`__bss_end=0x1940C0`。像=text/data/BSS/整列込み **606,400B**、残り **20,288B** (=0x199000−0x1940C0)、ページ切上げ後の余白は16,384B。DBGREF後の30,176B (`TASK_T2D_T2H.md:547`) から正味 **9,888B増**。LANだけの増分とは断定しない。map更新日時2026-10-15 23:47:40 JST、SHA256=`b83f79dff899041aba6070037e620f14bdae5baf4e5cec06deb1e6e03d9c4a23`。
同時に存在する `build/out/vmkernel.lz4` は500,430B、`boot/boot_defs.h:93` の508KiB上限まで19,762B、`build/out/kdebug.bin` は44,840B (file長、常駐量ではない)。再ビルドはせず読取計測のみで、配備像・同一設定の再現性は未確認。済み (入口の値は :192)。core・module・私有領域・圧縮像は別勘定にする。予定の撤去益を先取りしない。

T7への引渡しはT5c-1のKCG facade (`drivers/video/kcg/kcg_module.c`)・ROM/cache (`drivers/video/kcg/kcg_body.c`、64KiBは部品内訳) と、T5c-3のbootinfo/page0原本・V86必須PTE/例外復帰口。page0通常NP・低位FIXED統合・旧font撤去はT7で行い、coreのCODE復帰口と搭載時cache失効は既存T7a-2cで実装する。T5aのB2受入だけでX-1全体を閉じない。

## 1. T7a 低位の所有権切替

**台帳から受けるID** (正典 `docs/tasks/DEFERRED_TESTS.md:25,68,71,73,92`、経緯: `docs/archive/v3/DOCS_REORG_T3.md:25,51,74`)。台帳の本文・進捗は複製せず、閉鎖証拠の受け口だけを定める。

| ID | 本票の関門 |
|---|---|
| E10-4 | T7a-2d: 採取active中を注入で捕まえ、kill後のg/tv解放・gcap_ops解除・TVRAM30行復元を観測 |
| H-6b | T7a-2c: 6Ah標準/拡張の復元 (合格、閉鎖台帳)。9801でのgui_gateはユーザー決定 2026-10-19 で確かめない (9821 の後方互換) |
| H-5 (CG窓WB・68hのみ) | T7a-2c: 固定診断でCG窓属性とCODE復帰/cache失効を実機観測。kstackは本票へ移管しない |
| T7-V86CTX | T7a-1c: importの例外文脈の印と2回目のセッションのpage0照合 |
| X-1 (page0 NPのみ) | T7a-1c / -2b: boot原本、直読consumer退去、通常NPと全出口復元。文字列/kprintf部分はT4/T5a |

T7aの到達点は「低位を一般poolへ追加」ではなく、bootinfoの保存済み写しとmailboxを扱ったうえで **V86が低位を直接使える状態**。`kernel/v86/v86_mem.c` の636KiB backingを消すかは上位票U7の実証後に決める。高位lease窓をV86へ渡さない。

| 現利用 | 切替先 / 条件 |
|---|---|
| planar BB | boot時pool連続確保→gshell所有、T2のSURFACE経由。全planeポインタを更新し旧0x6A000参照をゼロにする。撤去担当はT7a-1aが描画側と `kdebug/kselftest.c` の自己診断 (対応済み)、T7a-1bが `drivers/video/kcg/kcg.c:192`、T7a-2e (低位予約の統合) が `mm/memory_boot.c:216,419`・`userland/tests/ring3_guard.c:25`・`tools/gen_memmap.py:270-276`、個別host試験は未割当 |
| Unicode展開表 | T3のkernel組表+KAPI。T2暫定RO leaseが残っていれば先に撤去 |
| `.kcgfont` 展開/読込scratch | 廃止。T5c-1のtyped KCG facade・pool上のキャッシュ (64KiBは計上値、受入ではhit率を記録)へ。coreのCODE復帰口と搭載時cache失効はT7a-2cで実装する |
| bootinfo/BDA | IVT/BDAの起動時原本とbootinfoはcoreへコピー済み (`init/kernel.c:197-198`)。残る低位直読を§1-1の順で撤去 |
| `V86_TEST_MAGIC_ADDR` (現0x8C000) | 低位V86領域内の試験語。開始ごとにゼロ化し当該試験だけが設定。通常kernel stateの置場にしない |
| mailbox | 0x90000の内容をセッション前後で退避/復元。host側排他はツールの契約。kernelがhost接続を検出して拒否できるとはしない |

### 1-1. V86 状態遷移

> 契約の正典は [09_exec.md](../../09_exec.md) の「V86 セッションの入場と呼出元」と低位の復元の段落 (2026-10-23 に移した)。下は設計時の記録。

`CUI_IDLE → PREPARING → V86_ACTIVE → RESTORING → CUI_IDLE`。呼出元はCPL=3の `userland/cmds/v86.c` (`v86 -t/-b/-g` のKAPI呼出し) であり、**そのASの存在自体は拒否理由にしない**。入場はCUI・非再入・呼出元以外の通常ASなし (parked/親ASも含む) に限定する。resident shellは通常ASとは別。T2の「CUI呼出元ASは停止中」の例外をここで具体化する。親ASを伴うCUI入れ子はBUSYで副作用なく拒否し、単独起動へ案内する。親を含む集合のpin/復帰は本案に含めない。GUIと他ASありの拒否は低位PTEを触る前に行う。

PREPARINGではT2のuaccessでpath/secondをkernelの上限付きstagingへコピーし、`V86Gcap`等の出力範囲をcaller PDで検証する。NULL可否は既存API契約どおり。未終端/範囲外は副作用前に失敗。callerのslot/generation・PD・KAPI復帰状態を固定して、slotにV86呼出し中の印を付ける。caller所有のPD/backingをpinし、park/resume、trim配送、別アプリ起動とcaller teardownを禁止する。この停止は他アプリへのスケジューリングではない。

coreの復帰状態、低位PTE、mailbox/boot dataを低位外へ保存 → **caller CR3を保存し、masterでも有効なcore stack/TSS.ESP0を確認してmaster CR3へ一時切替** → V86用低位PTE/TLBを更新 → 実行、の順。master切替後はcaller VAを直接参照せず、path・出力・captureを全てkernel stagingで扱う。ファイルopen/loop deviceにもstaged pathを渡す。V86セッション中はcaller ASが存在しても実行せず、通常ASの地図検査を停止する。低位PTはmasterと各PDが共有しているため、そのUSER変更は全PDへ及ぶ。callerを停止するのはこのためで、通常map APIからはこの専用変更口を呼べない。ページ0の実物写像はPREPARINGからRESTORING完了までに限定する。

正常終了・#PF/#GP・STOP・準備途中失敗の全出口は、到達した段の印を持つ同じRESTORINGを通す。**実物IVT/BDA等をRW写像中に復元 → 通常低位PTE/SHM・trampolineのPDE権限を復元 → KCG復帰 → master側TLB反映・地図検査 → 保存したcaller CR3へ復帰 (TLB更新) → caller generation確認 → kernel stagingからchecked copyout → pin/呼出し中印を解除 → KAPI復帰**。V86が変えた低位データをkernel stateとして信用しない。STOPによるcaller終了要求は復元を飛ばさず、復元後のT2安全点で通常回収する。復元に必要なcode/stack/保存領域を低位へ置かず、master切替をまたぐreturn先もcaller VAに置かない。既存kernel stackで足りるかhigh-waterを測り、不足なら固定帯を無断拡大せず設計を再確認する。復元検査失敗時はcallerへ戻らず停止する。

masterの検査は共有低位PTの復元検査でもあり、caller復帰時のCR3ロードでそのTLBも更新する。KCG復帰はT7a-2cでcoreのCODE復帰口と搭載時cache失効を実装する (現状は `drivers/video/kcg/kcg_body.c:101` のinit時CODE設定だけ)。既存kcg部品のfacade (`KcgModuleApi`) に失効口を足し `KCG_MODULE_ABI_GEN` を上げる (`drivers/video/kcg/kcg_body.c:99` の再初期化はBUSYを返すため使えず、新しい段・部品は設けない)。既存定数 `MODE_FF1_PORT/MFF1_KCG_CODE` でコードアクセスモードを再設定し、ROM/CG RAM由来のキャッシュを全失効する (module未ロード時はcoreの復帰口でmodeだけ設定し、次のmodule initは空cache)。68h=0Bhを残した時のA9hの値は未確認であり、本設計の根拠を実測済みとはしない。

ページ0をNPにする前に、`pegc_identify`、`pegc_boot_sync_record` (`drivers/video/pc98/backend_pegc.c:477-514`) のBIOSフラグ、`gfx_identify_candidates` (`gfx/gfx_core.c:379`) の3経路を起動時の保存済みcore写しへ変更する。PEGCの実port probeは存続しBIOS値との比較だけ写しを使う。V86用原本はページ0全体 `[0, PAGE_SIZE)` (4096B、0x600〜0xFFFを含む) の上書きされないboot snapshotとする。既存の `real_lowmem[PAGE_SIZE]` (`kernel/v86/v86_mem.h:121`) はT5c-3で起動時に一度だけ採る唯一のcore原本へ転用済み (`kernel/v86/v86_snapshot.c:9`)。bootinfo保存口もcoreに残る (`init/bootinfo.c:57`)。`real_saved` は原本有効フラグへ役替え済みで、session reset/部品回収・セッション終了で消さないことを回帰確認する。T7はT5c-3の同じ原本を共有し、4096B配列を重複保持しない。page0用の編集stagingは置かず、追加配列は0B (既存の2560B増は吸収済み)。退避は低位consumerの書換え/NP化より前、復元も全ページが対象。開始はmailboxを退避 → 実物低位640KiBをゼロ化 → 原本から物理page0へ直接4096Bをコピー → `v86_bios_setup` が物理page0上だけでguest BIOS情報を編集 (`kernel/v86/v86_bios.c:244`) → guest image配置 → 実行の順。終了時も同じ原本から物理page0全体を戻し、原本自身は変更しない。これにより前回guest/旧font/boot stackを再公開しない。終了時にmailbox原内容を復元する。ゼロ化の前に全boot stack/bootinfo consumerの退去を確認する。

`memory_boot_fixed` の旧FONT/UNICODE/BB/残余の低位行は、`[0, MEM_CONV_END)` の単一予約 **LEDGER_R_FIXED / LEDGER_OWNER_KERNEL / WB** (用途名V86_LOW、一般poolへ供給しない) に統合する。新しい台帳型やセッションownerへの移譲は不要で、V86状態が排他使用権を表す。BBのpool移動は別のSURFACE_BACKING記録にする。memmap定数・`tools/gen_memmap.py`・`02_memory.md` の説明を同じ差分で更新し、`make docs-gen` で `build/out/MEMMAP.md` を確認し、開始/終了でこの予約の型/owner/PFN数不変を検査する。

mailboxの868B (`include/memmap.h:127-128`) は起動時に低位外のKHEAPへ1回確保し、core所有で保持する。固定BSSへ配列は足さず、pointer等の管理分はcore管理枠、868Bとallocator管理/整列はKHEAP実占有に計上する。未確保ならV86入場を低位変更前に失敗させる。H-7 (`docs/tasks/DEFERRED_TESTS.md:84`) でgameはv3ビルド外だが、hostツールとの既存の保存/復元契約は維持する。path/出力stagingの確保失敗も低位変更前に返す。mailboxはDOSへ申告する640KiBの内側なのでV86中はguestが上書きしてよい。保存・実行・復元の**全セッション中**にhostが読書きしないことをツール側のセッション所有で保証する。`game/tools/autoplay/driver.py` の変更が必要なら外部引継ぎ事項として記録し、外部リポジトリの実装を完了したと扱わない。

H-6b/H-5の復元は6Ah標準/拡張、68h CODE、GRCG/EGC残状態、CG窓WBを一組で扱う。CGを根拠なくUCへ変更しない。9801で存在しない9821用I/Oを発行しないことも観測する。[HW3]に必要なメーカー資料とBSD/MIT実装の版・ファイル・行はT7a-2cの開始時に固定 (根拠対: UNDOCUMENTED `io_disp.md:207-218・253-328・1068-1081`、`memsys.md:4044` 付近・4100-4137、Bible §2-6-5 `2-6_テキスト.md:349-371`、FreeBSD releng/10.4 `sys/pc98/cbus/gdc.c:601・1095-1107・1356-1364`。NP21/W は振る舞いの確認だけ)。既存根拠の `kernel/v86/v86_io.c:238-271` と `drivers/video/kcg/kcg_body.c:97-105` に対し、資料との不一致と実機結果を記録する。読む口は対象固定の診断とし、公開の任意物理read KAPIを追加しない。

### 1-2. OpenType前の中間段を動かす

T7a→T7bの間もGUIの日本語を失わない。**KCG ROMの字形取得を唯一の文字供給にして受け入れる**。CUIは引き続き内蔵フォント、GUIはKCG KAPI/facade経由で描画。KCG moduleのコード/キャッシュは低位の外、未収録字は固定の代替字形。`.kcgfont` の旧KAPIスロットはNOSYSで残し、旧ファイルを読み直すfallbackを作らない。フォントファイルのないFD正常起動 (SQLITE_ONLY、FEP stub・予約0) と部品等の実失敗によるMINIMALでも英数入力・編集と文字表示を確認する。KCG moduleのロード/init失敗時はGUIへの切替を失敗させCUIを維持する (日本語を欠いたGUIを成功扱いしない)。T7a-1でcold/warm各1000字、ROM I/O数、cache hitと入力→描画時間をD15への参考値として記録する。cold全角1字はOUT34+IN32=66 I/Oとwait34回 (`drivers/video/kcg/kcg_body.c:42-65`)。I/Oとwaitを別計数し、実測前に速度合格とはしない。

## 2. T7b OpenType ストリーミング

### 2-1. 実装範囲と読込 API

最初の対応は配布するIPA原本と選定した欧文フォントが使うTrueType `glyf` 輪郭。CFF、可変font、複雑なscript shapingの一般対応を「OpenType」という名で暗黙に約束しない。対応外formatはKCG/代替字形へ。欧文フォントの選定とライセンス確認は実装前ゲートであり、本票で新しい配布物を承認済みとしない。日本語fontは既存の同意付きfetch経路から配布物へ入れ、リポジトリへ追加しない。

D36どおり依存はMITソースをvendor化し、版・license・差分・最終386命令列を記録する。共有Rust crate案 `userland/lib/font/` をgshell静的リンクとlibos32gui shlibの両方で利用する。API案は以下。ハンドルは各owner内のslotと再利用しないu32世代を持ち、満了時はslotをretireする。

- `font_open(source, limits) -> FaceHandle` / `font_close(face)` / `font_trim(face)`。
- `font_lookup(face, codepoint) -> GlyphToken {face_slot, face_generation, glyph_id}`。close/reopen後のtokenは描画・寸法取得ともSTALEで拒否する。
- `font_metrics(face, px) -> {ascent, descent, line_gap}`、`font_advance(token, px) -> advance`、`font_rasterize(token, px, target) -> bitmap_bounds`。寸法は符号付き26.6固定小数点、pxは16/32、座標/丸め/overflow規則を共通化する。bboxとadvanceを混同しない。

既存libos32guiのG2 (`text`/`measure_text` の半角8・全角16・高さ16) は維持する。GUI adapterはUnicode幅判定を共通にして固定cellへ配置し、筆位置はcell幅だけ進める。glyphはraster前のbboxで幅を測り、cellを超える欧文 (W/M/m等) は横方向をcell幅へ縮小して中央配置する。横縮小はadvanceを変えず、縦は共通baselineを維持する。cell clipは最終安全境界に限定し、縮小後の字形が欠けた結果を受け入れない。欧文の比例advanceはfont APIから取得できるが既存widgetへ直結しない。baselineはface metricsから同じ丸めで求め、KCG fallbackも同じcellへ置く。T7b-aで欧文fontの横縮小後の1x可読性・上下の欠けなし (W/M/m、アクセント、descenderを含む) を確認し、読めなければ等幅候補の再選定をゲートとする。T7b-aでG2維持の承認とcaller一覧 (text/measure_text、edit/caret、clip、FEP候補窓) を照合し、T7b-bで画像と測定を一致させる。比例幅GUIへ変更したい場合はG2改訂の別判断が必要。

sourceは `read_at(offset, dst, len)`、file_size、**owner・FD generation**を持つ。seek/readの短読は成功扱いせず、他ownerのFDを借りない。gshellのopen/reopenはshell自身の通常文脈だけで行い、`vfs_fd_set_protect` を設定する。GUI op中は設定変更要求だけ記録し、そのopからopenしない。描画時の遅延openもしない。shell shutdownでは明示closeする。アプリのfont FDはそのownerでopenして通常終了回収に任せる。

read_atは `offset <= file_size && len <= file_size-offset` を先に判定し、要求長を返した場合だけ完全成功とする。len=0は有効な範囲で0、短読/媒体エラーでは当該glyphを捨てる。font_closeは借用終了後にだけFDを閉じ、STALEなleaseも登録世代一致なら解放可能とする (誤った世代で再利用後のFDを閉じない)。自動再openは次の通常安全点に1回だけ試し、失敗した設定世代は明示変更までKCGに固定する。

現在 `VfsSqliteLease` だけにあるfd+generationの検証を、SQLite cookieを流用せずfont用read leaseとして追加する設計。kernel側がowner/lifetime/in_use/generation/staleをread_at/closeの各処理内で検証する。seek状態を公開せずread_at内部で位置決めと読込を一体で行う。ユーザー側の事前checkだけにしない。権限は以下の**別入口**で固定し、ownerを呼出引数で選ばせない。

| 入口 | 権限の根拠 / 検証 |
|---|---|
| app用public KAPI `font_source_open/read_at/close` | int80で保存されたcallerのCPL=3・slot/generationを根拠に、`ring3_call_from_user()!=0` かつWM外を要求。lease表はcaller owner専用で、ownerは `res_owner_get()` とsaved callerの一致も照合する。shell leaseはこの名前空間に存在せず、shell token/生FD/他app tokenはINVALまたはSTALEで拒否。KAPI直呼びのshell/WMをこの口で特別許可しない |
| shell内部 `shell_font_source_open/close` | loaderが検証した現行gshell画像世代・shell textの直呼びで、`ring3_in_syscall==0`・WM外・`res_owner_get()==GUI_SHELL_OWNER` を全て要求。登録済みshell sourceのみ生成/破棄する。app引数からleaseを登録しない |
| shell内部 `shell_font_source_read_at/validate` | 同じgshell世代のshell text直呼びかつ `ring3_call_from_user()==0`。shell専用registryで照合し、期待ownerは固定の `GUI_SHELL_OWNER`。WM中は `res_owner_get()` がappでもよいが、app registryへはアクセスしない。通常shell文脈か、登録済みgshellのhandler/pump/owner_exit呼出区間であることも確認する |

shell内部入口はKAPI/trampoline/ユーザーimport表に載せず、resident shell loaderが検証済みgshellだけへ渡すsupervisor専用サービス表で供給する。入口の薄いcdecl wrapperは直接のreturn PCがloader由来の現行shell executable text範囲内かを検査してから共通VFS実装へ進む (callerが申告する番地は使わない、尾呼出しで検査を省略しない)。サービス表とregistryはUSERなし。shell登録世代が終わればサービス参照とleaseを失効させ、旧gshellからの利用を拒否する。これによりappがshell番号を知ってもKAPI経由では到達しない。resident限定入口/サービス表の実装と混在拒否をT7b-bの変更対象に含める。

`ring3_call_from_user()` は `ring3_guard_active(in_syscall, wm_depth)` であり、WM中に0になることを実コードで確認した (`exec/exec.c:311-338,386-389`)。この値やdepth単独をshell権限の証拠にせず、上記の非公開入口・shell text/世代・専用registryと組み合わせる。WM呼出区間の印はkernelがhandler/pump/owner_exitの前後で設定/解除し、通常returnに加えてpark/STOP/faultのlongjmpとdispatch入口でも消す。font借用中は§2-4のguardでpark/STOP配送を止める。試験はappからshell lease/生FD指定を拒否、WM中の正当なshell read成功、WM終了直後のappから拒否、close/reload後の旧世代拒否、例外/park経路で権限の印が残らないことを含める。

ttf-parserのprivate `glyf`内部へ当然にアクセスできるとはしない。T7b-aで現在のcrate公開APIを調査し、字形単位の安全な薄層が実現できなければ、必要なテーブル/単純輪郭readerを明示的に実装するか、ライセンスを保った限定vendor化をレビューする。`Face::parse`へ全ファイルを保持する試作を製品経路へ戻さない。採用方法・依存版・変更範囲はこのゲートで記録する。

### 2-2. 常駐表とメモリ上限

上位票の「表≈100KiB」は廃止したサブセットの見込みで、IPA原本のcmap/loca/hmtx合計だけで343,649B (約336KiB)になる。再照合時の `assets/fonts/ipaexg.ttf` / `ipaexm.ttf` のSFNT directory (offset12から20entry) を読取計測し、両方で245,775+48,960+48,914B。原本hash/取得経路は `assets/fonts/README.OS32:4-16` と一致した。**全表常駐を前提にせず、表ディレクトリと選択subtableの索引だけ常駐させる設計案**とする。

| データ | 読み方 / 実測から決める上限 |
|---|---|
| head/hhea/maxpとtable directory | 範囲検査後に必要scalarだけ保持。table数・索引/scalar/face管理量はa1③のcorpus実測最大以上で決める。全font常駐なし |
| cmap | 対応format4/12のheader・検索用indexを保持、segment/group本体はread_at。検索回数は要素数の対数に制限 |
| loca/hmtx | glyph IDから必要entryだけ読む。長短loca、末尾advance共有を区別 |
| table read cache | 私有LRU、file/generation/offsetでkey化。slot数・各slot量はa1③の読込単位・同時保持量の実測最大以上で決め、a6でhit率とcold性能を記録 |
| glyph scratch | 入力byte数・点・contour・component・再帰深さ・曲線分割後edge・専用領域はa1③のcorpus実測最大以上で上限を決め、超過は代替字形。製品fontで欠ける字を一覧にする |
| raster scratch | 16/32pxでbbox・余白・pitch×height・f32累積面/出力/行scratchをa1③で測り、実測最大以上の上限を決める。超過は代替字形、座標とサイズのoverflowを検査し不足で途中描画しない |
| bitmap cache | 8MB構成は上位票の64〜80KiB/owner内。glyph+font_generation+px+render_modeをkey、LRUで返却 |

a1③で配布fontの字形corpusとhashを固定し、上表の最大値を測って上限を決める。a3〜a5はその実測最大以上の上限を使い、超過時は代替字形とする。作業域総量 (bitmap別) はa6で実peakを記録し、cold性能も計測する。複数faceでも作業域は直列に共有し再入を拒否する。shell画像/BSS+heap+40KiB stack+guard+font peakが1MiB内、アプリのfont領域は私有総量2MiB内であることをT7b-aのmap/peakで確認する。16MB以上の256KiB bitmap cacheも私有勘定。gshellはT3の起動時固定分割 (exec_heap256KiB、sbrk最低128KiB) とresident CRTを使う。共有glyph cache/GUI補充opはこの票に追加しない (U21)。T3は現在のmapで初期分割を確定し、未測定T7bに依存させない。T7b-aでcrate導入前後のgshell画像とshlib text/data/reloc/paddingを比較し、shell/shlib帯と2MiB私有枠への課金を確認する。不足時は固定分割定数/リンクASSERT変更を独立差分として再レビュー・全再ビルド・再受入してから製品接続する。実行中に境界を動かしたり帯そのものを無断拡大したりしない。

FD表は全16本、通常open対象は3〜15の13本。active faceのFD上限はT7b-a6でgshellとapp、FEP/SQLite (DB/journal含む)、標準redirect、pipe、loaderを含むFD peakと残数を実測して決める。FD不足はKCG fallback、再openを毎字繰り返さない。FD増設を予算不足の暗黙の解決にしない。

### 2-3. 解析・描画・エラー

font/fileの状態は `CLOSED → VALIDATING → READY → CLOSED`。検証中のfaceを公開しない。table offset/length、glyph ID、glyph span、contour/point命令のrun長を検査する。locaは全走査せず要求glyphの隣接2entryを読み `0 <= start <= end <= glyf_length` を検査する。cmapは対応する (platform,encoding)=(3,10) format12を優先、次に(3,1) format4とし、未対応ならfallback。検索に使うsegment/groupのソート・非重複はopen時にストリーム走査で検証し保持しない。そのcold I/Oも計測する。format4のidRangeOffsetは当該wordを基点としてglyphIdArrayの範囲内か検査し、delta加算前後のglyph IDを検査する。複合glyphは循環・再帰深さ・総component/点数・総読込byte数を上限付きで処理し、最初に選定fontで非対応にするなら検出して代替へ落とす。fontの実測最大字形をOpenType一般の保証にしない。

1字の処理はlook up→全入力取得/検証→scratchへ完全raster→cache commit→描画。短読/破損/確保失敗で部分bitmapをcacheへ入れない。cacheから描画する場合も先にsource lease/FD generationを確認する。設定変更時は借用中の描画終了を待つ通常安全点で旧face/token/cacheを失効 → 旧FD close → 新face検証 → 公開とする。旧faceを残す二重openはしない。設定切替でFD peakを増やさず、再open失敗時はKCGで継続する。trimはT2の通常文脈・非再入安全点でbitmap/read cacheを返し、描画中に借用中のentryを解放しない。

### 2-4. 内容の更新とx87境界

FD generationは番号の再利用を検出する値で、ファイル内容の世代ではない。fontはFEPの辞書と同じく、使用中はhsyncがBUSYで置き換えを断る。新しいフォントは使用していないときに配備し、次の起動で読む。T7b-b1でfont sourceの使用中保護を接続し、製品結線でBUSY時の元ファイル不変と次回起動の新font読込を確認する。全更新口への変更前失効通知と内容epochは作らない。read leaseのowner/FD generation・既存stale検証とclose後の旧token拒否は維持する。HostDrv/SerialFS/LAN版 `/host` など管理外で内容が変わるsourceは最初の製品font sourceにはせず、管理下ext2へ配備する。

x87 guard の実装開始前に [CONSTRAINTS [HW3]](../../CONSTRAINTS.md#hw3-新しいハードウェアの実装はライセンス上参照しやすい既存の実装資料を参照してから設計する) に従い、Intel 資料の版・節を固定する。CPU 命令だけの扱いでは BSD/MIT の既存実装参照は任意で、参照した場合に版・ファイル・行を記す。採用ラスタライザがf32を使う場合、P6全体を待たず **T7b-aにx87隔離guardを先行実装する**。gshellのfont描画/寸法の最外周でcallerのx87全状態 (register stack、status/tag、CW) を専用の低位外領域へ保存、例外をmaskした既知CWで初期化し、成功/失敗とも復元する。CWだけの保存では不足。guard中のpark/アプリ切替/STOP処理/再入を禁止し、要求は終了後の安全点で処理する。IRQでFPを使わない。アプリ私有側も同じguardで呼出元のFP計算を壊さない。x87非搭載CPUは検出してこの経路を実行せずKCGへ落とす。386対応は最終命令列検査を行う。x87保存領域は実測するface管理量に、追加codeは画像増分へ算入する。

**T7b-a2 の [HW3] 根拠 (2026-10-19 固定)**: Intel SDM Vol.2A **253666-088US (June 2025)** [原本](https://cdrdv2-public.intel.com/858446/253666-088-sdm-vol-2a.pdf)、FINIT/FNINIT・FLDCW・FRSTOR・FSAVE/FNSAVE (特に 3-414 の 387/486 向け保存後 FWAIT)。Intel **80386 Programmer's Reference Manual (1986)** [§11.1](https://pdos.csail.mit.edu/6.828/2018/readings/i386/s11_01.htm) の ET/EM/MP/TS と 387 の #MF、Intel **i486 Programmer's Reference Manual (1992)** [原本](https://www.bitsavers.org/components/intel/80486/1992_Intel_486_Programmers_Reference_Manual.pdf) の Fig.3-23 CPU_ID/MCP_ID 検出例 (AC 判定、FNINIT/FNSTSW/FNSTCW、非搭載) を用いる。Pentium の no-wait 窓は SDM Vol.1 **253665-021 (September 2006)** [§D.2.1.3](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366521.pdf) に従い NE=1 で避ける。BSD/MIT 実装は参照していない。共通入口は `font_guard_run` (整数結果、NOSYS は KCG、再入は BUSY)、専用108Bは低位外の core BSS。製品 caller は b3、STOP は復元後の既存 exec 安全点へ戻す。

parser/rasterは境界検査済み入力と上限付き領域だけを使い、allocation failureはResult、unchecked index/unwrap/panicを製品経路に残さない。host fuzz/破損font・全上限±1を通し、panic不在はテストだけで証明せず全呼出経路のレビュー条件とする。gshell側の予期しないfault/panicはfont active印でapp-killより先に検出しfail-stopする (WM faultを無関係なアプリだけの失敗へ畳まない)。STOPは正常leave後に配送する。T7b-bでアプリがCW/rounding/例外maskを変更しx87 stackに値を残した状態からWM描画し、状態の完全復帰とfault帰属を検査する。

## 3. T7 の実装分割と受入

各行は半日〜1日の独立受入単位。番号付き条件が10項を超えたら分け、同じ判定の機種/入口違いは試験行列で展開する。各段で対象像の増減・BSS・pool peakを記録し、10項超または1日を超える依頼は契約の境界で再分割する。原則は表の順、b-aの試作は実装sourceを差し替えられるfixtureとし、b-bのlease/使用中保護完成前にgshellへ製品接続しない。T7b-a1の着手条件はG2維持とcaller一覧の承認 (§2-1)、a5のレビュー条件は全呼出経路のpanic/unchecked不在 (§2-4) とし、観測による受入とは分ける。

| 段 (旧4段の細分) / 主な対象 | 観測で閉じる条件 | 故障注入・変異 (正常対照も必須) |
|---|---|---|
| T7a-1a BB / `gfx/gfx_core.c`・planar backend | ①boot連続pool確保 ②全planeが新backing ③boot→gshell所有移譲1回 ④lease/present/再attachで同じ絵 ⑤失敗時ページ漏れ0 | 最後のplane旧番地残し、pool確保失敗 |
| T7a-1b KCG / `drivers/video/kcg/kcg.c`・`kernel/boot_font.c`・`drivers/video/kcg/kcg_module.c`・deploy | ①配備予行で旧font無し ②KCGのみのGUI日本語/CUI英数編集 ③cache hit率記録 (64KiBは計上値) ④module欠落/init失敗はCUI維持 (`kcg_module_ready()`・`kcg_module_load()` の結果でcoreが判断、T5c-1のstubは字形0/診断・TVRAM経路保持、`drivers/video/kcg/kcg_module.c:17-29`。現状は旧font成功時にmoduleを読まない、`init/kernel.c:543`) ⑤旧slot NOSYSと低位font参照0 ⑥cold/warm1000字・I/O記録 | cache満杯/確保失敗、旧font fallback再導入。D36の `os32_lz4` は呼出し0を確認して撤去 |
| T7a-1c boot原本 / T5c-3のcore保存口・BIOS consumer | ①書換え前の4096B保存済み原本を回帰確認 ②原本不変 ③残る3経路の直読0 ④Unicode橋撤去済み ⑤bootinfo/boot stack退去 ⑥2回目を含む各セッション前後の物理page0全4096Bが原本と一致、importの例外文脈の印を確認 ⑦boot2/DOS起動成功 | snapshot末尾欠落、終了時原本フラグ消去、consumer1本直読へ戻す (NP化は次段) |
| T7a-2a 入場 / exec・uaccess・session | ①単独CPL3呼出し成功 ②GUI/親/他AS/parked/再入を変更前拒否 ③不正path/outputでPTE不変 ④caller pin/CR3退避 ⑤master中caller VA参照0 ⑥成功/失敗ともpin解除 | 終端欠落、2本目pathのcopy失敗、mailbox起動時確保失敗、pin解除漏れ |
| T7a-2b 低位切替 / paging・v86_mem/bios | ①fixtureの直接低位640KiB実行 ②通常page0 NP ③page0/mailbox原内容一致 ④全出口CR3/PTE/PDE/TLB一致 ⑤前回データ/0x8C000ゼロ | 各準備境界失敗、末尾PTE復元/TLB反映/mailbox復元を1つずつ除去 |
| T7a-2c 表示復元 / v86_io・KCG facade/body・固定診断 | ①[HW3]根拠対を固定 ②6Ah標準/拡張復元 ③既存kcg部品のfacade (`KcgModuleApi`) に失効口を足し `KCG_MODULE_ABI_GEN` を上げ、coreのCODE復帰口・搭載時cache失効を実装し68h=0Bh注入後に確認 (未搭載時はCODEのみ) ④CG窓WB/VRAM UCの前後一致 ⑤9801 gui_gate・9821実機再描画 ⑥R7FIX2のFFF窓とCUI復帰維持 | mode復帰/失効省略、古いF00000h窓へ戻す。A9h値は実測記録 |
| T7a-2d 採取中断 / `kdebug/v86_gcap.c`・`kernel/v86/v86_gcap.c`・STOP・DBGREF境界 | ①active印を見てSTOP注入 ②CTRL+STOP は HOTKEY で戻り kills 0 (kbd.c→v86.c→isr_stub の実経路)、#PF/#GP は共通 kill で kills 増分 1 (PM 2026-10-19、STOP が kill を通る想定を実経路に合わせた) ③g/tv alloc−free差0 ④gcap_ops/active解除 ⑤TVRAM30行/CR3/出力規則復元 ⑥次のv86実行成功 | detach/release片方除去、準備失敗/#PF/#GP/STOPを各入口で注入 |
| T7a-2e 予約統合・backing撤去 / memory_boot・memmap・gen_memmap・02_memory・v86_mem | ①低位全160PFNが単一FIXED/kernel/WB ②session前後で型/owner/PFN数不変 ③一般pool供給0 ④BBは別SURFACE_BACKING ⑤docs-genと台帳/定数/説明一致 ⑥通常経路を直接低位へ ⑦159ページ確保0 (旧経路との差636KiB) ⑧低位予約とpool不変 ⑨GUI→CUI→V86→GUI20回漏れ0 ⑩Ra266 -g/boot2/DOS回帰 (T7a-2c レビュー R1: `v86 -g -t` の後の 6Ah の読み (09A0h) と E0000h の状態を 1 行観測 — 採取中の表示の戻しは ROM AH=30h 任せ) | 旧低位予約1行残し、owner/端PFN違い、連続pool枯渇でもV86可 (他stagingは確保済み)、旧低位backing別名残し |
| T7b-a1 採用ゲート / font crate・assets | ①公開API/必要reader範囲固定 ②IPA/欧文原本のhash/license/fetch確認 ③corpus全字形 (16/32px) の点/輪郭/component/深さ/span/edge・table/索引・read cache/作業域・bboxの実測最大表と上限決定 ④上限超過字一覧 ⑤W/M/m/アクセント/descenderの1x可読性 | CFF/可変font/未対応cmapはKCG。読めなければ欧文再選定 |
| T7b-a2 x87 / 共通guard・exec安全点 | ①全状態一致 (save領域の予約byteは除外) ②既知CW内で実行 ③再入拒否 ④STOP/parkはleave後 ⑤非搭載はKCG ⑥faultの帰属が一致 | 値8本/CW丸め/例外mask変更、失敗出口のrestore除去。f32前に完成 |
| T7b-a3 table reader / directory・cmap・loca/hmtx | ①全file常駐0 ②table数/索引/read cache上限はa1③の実測最大以上、超過は代替 ③format4/12対照一致 ④隣接locaとadvance一致 ⑤短読で公開0 ⑥read cache は LRU、枠の数は定数 1 か所 (a6 の実測で cmap 常駐か枠増を決める) | offset/length overflow、順序逆転、idRangeOffset境界±1、LRU を FIFO に戻す |
| T7b-a4 glyph / 単純・複合輪郭 | ①製品corpusの輪郭対照一致 ②入力/点/contour上限はa1③の実測最大以上 ③component/深さも同基準 ④edge/専用領域も同基準、各上限超過は代替 ⑤不対応字は一覧と一致 | 循環、run長、各上限±1、途中読込失敗で部分commit拒否 |
| T7b-a5 raster / 16・32px・font_test | ①26.6寸法とbaseline対照一致 ②bbox/作業域上限はa1③の実測最大以上、超過は代替 ③完全bitmapだけcommit ④最終386命令列検査 ⑤x87 (CW=037F、精度64bit) の結果はhostのf32とビット一致しない (二重丸め) — 比較は許容差か被覆の段で取る | bbox/pitch/座標overflow、allocation failure、途中raster失敗 |
| T7b-a6 試作計測 / map・FD・ext2 | ①shell1MiB/8MB私有2MiBの内訳表 ②作業域実peak/bitmap別枠 ③FD peakと残数からactive faceのFD上限を決定 ④cold/warm1000字 ⑤起動/入力→描画計測 ⑥上限・採用法を実測で確定 | corpus cache thrash、font FD不足、memory上限ちょうど/超過。試作全file readを製品へ戻さない |
| T7b-b1 app read lease / VFS・public KAPI | ①open/read_at/closeのowner/世代検証 ②短読失敗 ③a6で決めたactive faceのFD上限 ④STALE closeで別FD保持 ⑤通常終了でFD/owner漏れ0 ⑥[ABI1]〜[ABI3]と一式再ビルド ⑦使用中のhsync置換はBUSYで元ファイル不変 | 異owner/旧token/生FD混用、FD再利用、使用中保護欠落 |
| T7b-b2 shell入口 / loader・WM・service表 | ①shell世代/text/文脈検査 ②appから到達不可 ③WM中shell read成功 ④app ownerのFD不使用 ⑤park/fault/dispatch後の印0 ⑥reloadで旧lease無効 | wrapper検査/印消去を除去、WM後app再利用。新しい権限モデルは足さない |
| T7b-b3 製品結線 / gshell・shlib・trim | ①配備予行で新一式一致 ②旧face失効→close→新検証公開 ③設定変更/パネル終了/別app open後再描画 ④trim後再描画 ⑤借用中解放0 ⑥fault/終了でFD/owner漏れ0 ⑦使用中hsyncはBUSY、新fontは次の起動で読込 | 借用中trim/STOP、close後open失敗、partial bitmap投入 |
| T7b-b4 G2表示 / text・measure・edit・FEP | ①1x日本語/欧文画像可読 ②W/M/m等の欠け0 ③text/measure/caret位置一致 ④clip/baseline一致 ⑤FEP候補窓一致 ⑥旧tokenで寸法/描画STALE | cell幅とadvance混同、縮小せずclip、fallback baselineずれ |
| T7b-b5 統合 / 製品計測・回帰 | ①cold/warm1000字とpeak記録 ②D15の計測判定 (容量別は最終段) ③FD正常起動 (SQLITE_ONLY、FEP stub・予約0) と実失敗時MINIMALでKCG ④破損/短読/上限超過で操作継続 ⑤x87完全復帰 ⑥20回trim/設定変更/終了で増加0 | 使用中保護/guard復元を抜き実経路で検出、枯渇後の次起動 |

T7a-2bはCR3/PTEと直接低位のfixture内でU7を実証する。-2c/-2dの確認後、-2eで予約統合・docs-gen・旧backingの製品経路撤去を一緒に受け入れる。page0通常NPは残る3consumer退去後だけ有効にし、復元失敗注入はfail-stopを期待値とする。-1cの比較時点はPREPARINGの低位変更前とRESTORINGの復元後・NP化前で、0x600〜0xFFFも含む。guest実行中のpage0書換えは禁止せず、DOS起動成功はbyte一致とは別に観測する。fontの使用中保護はb1、hsyncのBUSYと次の起動での新font読込はb3の製品結線で受ける。準備段だけの合格で製品の成功を宣言しない。

**T7b-b1・b2 の詳細設計 (2026-10-20、Fable 起草・Opus 2 往復で条件付き承認・PM が N1〜N6 を反映)**: `~/os32-tmp/run/t7bb12/DESIGN.final.md`。票の前提と実物の食い違い 9 件 (独立の shell loader は無く `exec_run` の is_shell、使用中の更新拒否の手本は loop の pinned、保存 caller は AS 世代、gshell へ KAPI 外で渡す経路は無い等) を §1 に記録。渡し方は entry frame の 4 語目 (`frame[5]`) + CRT0 `__os32_aux`、`main` 署名は不変。段は S1 (VFS、a6 の確定後) ∥ S3a → S2 → S3b (exec.c の結線を 1 段に集約)。FD の予約・上限の値は a6 の実測で入れる。

**T7b-b3・b4 の詳細設計 (2026-10-20、Fable 起草・Opus 3 往復・PM の最終修正)**: `~/os32-tmp/run/t7bb34/DESIGN.final.md`。PM 決定 (推奨どおり): U1 欧文 baseline は face ごと (東雲 14・Inconsolata 13、超過は cell で clip — 下の b4 変異欄「縮小せず clip」はこの意味)、U2 IPAex はゲストに配備しない (kcgfont は触らない)、U3 公開 token API は作らない — 字形 cache 設計 final §2 の「公開 font_lookup/GlyphToken の世代検証」は「公開 font API を足す時に同時に入れる」と読み替え、b4⑥ は「設定世代の変更後、旧世代の cache entry を描かない (host 検出)」。

### 3-1. 像と実行時占有の記録

T7a/T7bの入口と出口で同じ構成のmapを記録し、core・module・私有領域・圧縮像の増減を分ける。推測のcore追加8〜16KiBを関門にせず、像・容量の停止条件はMAX_IMAGE_SIZE・MEM_KERNEL_IMAGE_MAXのASSERT・FD実測だけとする。

| 対象 / 担当段 | 記録する勘定 |
|---|---|
| T7a core (1a〜2e) | code/管理のmap差分 (mailbox pointer等を含む)。不変page0はT5c-3で転用したreal_lowmemを共有し追加0B、mailbox配列はKHEAPへ置き追加BSS 0B。旧font読込/codeの撤去量も実測で記録 |
| T7a pool / KHEAP | planar BB128KiB (32頁)、KCG moduleのページ切上げmem_sizeは0x11000=68KiB (cache 64KiBは `drivers/video/kcg/kcg_body.c:15` の静的BSS内訳で、部品全体を一度だけ計上)。mailbox868Bは起動時確保で保持し、allocator管理/整列込みの実占有をKHEAPへ別計上。path2本とV86Gcap stagingは現在のAPI上限/sizeofで事前確保しpeakを別記、stagingはmaster切替後に確保しない。backing撤去の636KiBはpool peak差で像の減少にはしない |
| T7b core (b1〜b2、guard結線) | code/registryのmap差分、a6のFD上限に対応するsource/世代表のtarget sizeofと整列。VFS/execをfont crateと一緒にcoreへリンクしない |
| T7b shell/shlib (a2〜a6、b3〜b5) | parser/rasterのgshell静的リンクとshlib共有textを別計上し、reloc/data/整列も記録。a1③で定めた作業域の実peak + bitmap64〜80KiB (8MB) / 256KiB (16MB以上) はowner私有。shell40KiB stack+guard/heapも1MiB内 |
| DEBUG部品と製品 | 新診断/fixtureは対のkdebugまたは試験専用像に置き、core必須復元コードは部品OFFでも残す。部品file長とページ切上げmem_sizeを区別し、KDEBUG=0/1のpool peakを別測定 |

T5最終一式 (`846dbeb`) の入口はVK32圧縮像220,607B、core raw 362,288B、BSS込み常駐末尾0x18c000。現 `MEM_KERNEL_IMAGE_MAX` は0x275000 (定義は `include/memmap.h:228`、`build/out/kernel.map:1638` は値の確認に使用)、圧縮上限508KiB=520,192Bとの差は299,585B。raw・BSS込み常駐・圧縮像を別勘定とし、module・heap/stack/NP guardとともにT7出口との差を残す。FD空きは2HD 84,992B/1.44MB 304,128B、64KiB下限まで19,456B/238,592B。旧fontは既にFD非搭載 (`kernel/boot_font.c:7`) なので撤去益をFD余裕へ足さない。KHEAPのmailbox868Bとallocator費用を含むpeakは別計上する。旧20,288Bや撤去予定量の差引きを予算にせず、圧縮像はMAX_IMAGE_SIZEへ照合する。

### 3-2. 試験の実行と判定

変異は実ソースの復元・検証を1箇所ずつ欠かし、正常対照GREEN→意図した観測のREDを対応づける。ビルド失敗は検出成功に数えない。font readerはhost破損corpus/上限±1とguest実I/Oの両方、V86は `check-v86-gcap-host`・T2のrecovery/lifecycle/SHM/surface試験を改訂して使う (`TASK_DBGREF.md:271-277`)。最後の変更後に選択した変異・静的検査を実行し、受入像のSHA/CRC/実行rc/観測前後値/画像の所在をRESULTへ残す。新しい採取手順は `tools/accept/`、恒久的な診断の本文は増やさない。

DBGREF既定はKDEBUG=0、観測は対の `/boot/kdebug.bin` がRUNNINGかつpair CRC一致の像で行う (`TASK_DBGREF.md:6,370`)。基点ではgcap本体は `kdebug/v86_gcap.c` に移設済みで、coreの `kernel/v86/v86_gcap.c:6` はops呼出し/NOSYSの受け口。OFF `v86 -g` はNOSYS・低位副作用0、ONは採取成功/中断を受け入れる。通常 `-t/-b/-d` と必須復元はOFFでも検証し、デバッグ部品不在を受入のSKIP成功にしない。

V86の4入口 `v86_smoke_test` / `v86_disk_test` / `v86_boot2` / `v86_gdc_capture` は各々、成功・準備失敗・#PF/#GP・STOPの到達可能な全出口と未到達段を表にし、共通RESTORING経由・caller出力・CR3/PTE/TLB復元を検証する。物理Ra266の `v86 -g` はNP21/Wの代替結果で閉じない。

性能記録は固定1000字corpusとfont hash、read_at回数/bytes、cache hit/miss、fallback字数/理由、計測分解能を添える。coldはopen時のcmap走査を含む初回表示まで、warmは同画面2回目。起動要求→初回presentと入力受理→対応presentを別に測り、最大/中央値を出す。P100/32MBの判定は容量別最終段、途中の64MB測定は退行比較に使う。

FD正常起動 (SQLITE_ONLY、FEP stub・予約0) と部品等の実失敗によるMINIMALはフォントfileを載せない構成としてKCGのcold/warm/fallbackだけを計測する。OpenType cold I/Oは管理下ext2上の原本fontをsourceとして測り、起動媒体・source媒体・FSを別々に記録する。FD起動後にHDD ext2をsourceにする追加ケースも「FD上のfont性能」とは呼ばない。FD FAT/HostDrvからの直接OpenType読込はこの受入に含めない。

> 2026-10-14 ユーザー決定: 搭載メモリ量ごとの確認は v3 の最終段にまとめる (DEFERRED_TESTS の関門「v3 の最終段」)。それまでのゲスト受入は 64MB の 1 構成。以下の構成一覧は最終段用として残す。

8MB planar/PEGC、17MB、Ra266、FDを区別する。coldはboot直後のfile/cache状態、warmは同じ画面2回目と定義し、機種/CPU/表示mode/字数/cache hit/miss/peak bytesを記録する。D15の速度目標を未測定推測から合格にしない。U7/U12/U14はT7a、U4/U5/U6/U15はT7bで証拠を対応づけ、容量別の残りは最終段で閉じる。P100/32MBで起動2秒以内・入力→描画100ms以内を判定し、64MB/Ra266/NP21/Wの参考値を代用しない。未達なら上限/方式をレビューするが目標は弱めない。

## 4. P3〜P10 へ渡す契約と着手ゲート

この表は後続との接続先で、全項目をT7後へ遅らせる順序表ではない。P7はV3_PLANどおりP1と同時。4世代の基盤はT2cで吸収済みだが、スロット整理は未実施でP7に残る (`TASK_T2D_T2H.md:49`、V3_PLAN §3 の P7 の行)。T7後へ順序を移さない。以下は実装完了や独立設計票の承認を意味しない。既存票の決定はそちらを正典とし、未起票項目は列の成果物が揃うまで実装しない。

| 柱/項目 | 渡す契約 | 着手時に閉じる設計・受入 |
|---|---|---|
| P3 HAL W7/NIC L-C/音 | T5cのowner/IRQ/DMA/import境界。portable側へPC-98 portを漏らさない | `TASK_HAL_WIRING` の残と各driver票を突合。1kHz tickは専用票を新設しclock別divisor、wrap、期限換算、ISR負荷、既存PCM/FM/V86/keyboardの影響を測る。測定前に周波数だけ変更しない |
| P4 resource broker | T1のverified resource、T2 lease、T5cの永久予約とmodule owner分離 | `DEVICE_RESERVATION` の旧GUI初回予約条件をD33へ改訂。GUI入口でBDF/ID/revision/BAR decode/boot世代を再照合、不一致はenable拒否・予約差替えなし。第二span衝突、GUI=0、probe失敗、再enterを試験 |
| P5 82557/PCM/Trident/FD/KBD | DMA物理記述子、module lifetime、gfxのsurface/lease | 各既存票の実機段。Cirrus/Tridentはソフトウェアの確認で足り、追加の実機WAB・64MB組合せ確認は求めない (ユーザー決定2026-10-19、`TASK_T4_T6_MODULES.md:367`)。ドライバ設計はOpus/Codex突合。PCM合成器はまずIRQ整数演算 (D36) |
| P5 IDE DMA/cache/先読み | 永久device資源、DMA停止証明、VFS I/Oエラーとowner | 新票でPIO実測baseline、DMA mask/境界/descriptor所有、timeout→停止→fallback、dirty/writeback/flush順序を設計。cache所有はkernel、アプリ終了でdirtyを消さない。使い捨てext2で失敗/再起動後e2fsckと内容比較 |
| P6 実行モデル | T2のSTOP要求/通常回収、trim安全点、T4のengine active | 新票で自動番犬と明示STOPの条件を分離。KAPI中はAS切替なし、長いKAPIの期限/取消点、VFS/SQLite副作用の中断規則、x87をAS毎に保存復元。純loop/長いKAPI/WM再入/SQLite active/PCM/park-resumeを列挙して停止応答を測る。IRQからfreeしない |
| P7 ABI整理 | T2の形式/KAPI/layout/shlibの4識別とcompile-unit stamp (T2cで基盤吸収済み、現行値は `sdk/kapi.json` の `version` / `generations` を参照) | 未実施の一度限りのスロット整理について、世代正典と整理一覧を1回の変更セットに固定する。未知型のRust生成失敗、旧shell含む混在拒否、旧.oのlink拒否、NHD/HostDrv一組を検査。[ABI1]〜[ABI3] の通常append-onlyとD35の一度限り整理を混同しない |
| P8 GUI音・入力 | DMA ring/lease、T2通常文脈、gshell owner | 新票でproducer/consumer cursor、underrun/overrun、close/fault時停止、event所有/容量/drop、入力focusとWM再入を定める。microUIマウスキーも対象に含め、既存WMマウスキーと二重配送せずfocus/移動/クリック/リピートを試験。8bppを維持 |
| P9 Video HAL/SDL/移植 | SURFACE記述子とlease VA分離、能力bit、全画面専有 | 新票でformat/pitch/plane/clip/present/flip/mode-change revoke、SW fallback、私有面(c)の費用を定義。(b)を勝手に(c)へ変えない。PEGC直描きは全画面だけ。SDL shimはcapabilityで断り、固定VRAM番地を公開しない。`PORT_CANDIDATES` の低難度移植から着手しZSNESもv3候補として依存API/CPU/メモリ/音性能を個別票で測る。移植完了をこのHAL票だけで認定しない |
| U21 共有グリフ最適化 | T7b私有版のcold/warm/実peak、gshell所有・RO leaseのT2契約 | 私有80KiB×owner数との実節減を測ってから別票。補充GUI opの再入、cache evictionと借用寿命、世代/revoke、gshell終了・app faultを設計。測定で価値が出る前に共有化しない |
| P10 辞書meta/F3b | T4の単一heap予約と暫定重複禁止、T5a RESIDENT group | `TASK_DICT_META`でdict_id、S/M/L、学習別file、再実行冪等、途中失敗復旧。F3b導入後に暫定禁止を置換し、parked/親子/別名の競合試験を再利用 |
| control panel/i18n/GUI apps | 設定保存と実行中state適用を分離、FEP再起動要求、font API | `TASK_CONTROL_PANEL`→`TASK_I18N`。設定保存失敗で表示を成功にしない。画像/音楽アプリ等は個別票と終了owner試験を起こす |
| network CD/HostDrv | 82557 L-B〜Eに加えL-F、既存Host Service、通常I/O期限 | LANFS/L-FのNIC/session結線・実機hsyncは受入済み (`docs/tasks/realhw/TASK_LAN_82557.md:3`)。`TASK_LAN_82557.md:8` の決定どおりL-CはL-Fに必要な分、lpr/wgetは後回し。実機で済んだのはL-Fのhsyncだけで、残りの段 (L-C/L-E・cdinstのLAN経由) では接続断・短読・再試行冪等・cdinst契約・実機/NP21分離を測る。LAN /hostも製品font sourceにはしない。host virtual memory/EMSは混入しない |

**T7 の後のリファクタリング (ユーザー決定 2026-10-20: T7 を予定どおり実装した後に切り出す)**: 字形ラスタの整数化。輪郭の受取りを ttf-parser の f32 から自前の i16 点集め (outline.rs) へ、合成字の拡大は F2Dot14 の固定小数点、曲線の分割数は整数平方根、面積・被覆は 26.6 の整数累積 (FreeType ftgrays 型)、`userland/rust/os32_math` (fix16・sqrt・recip) を使う (font crate はユーザーランドの部品なので同じ層の依存。カーネル層からは使わない — POLICY_DEV §2)。終われば x87 guard (a2 の FNSAVE/FRSTOR、service 表 v2 の `font_guard_run`、FPU 無しの NOSYS・`KcgOnly(NoFpu)`) を撤去し、FPU 無しでも新フォントで描ける (台帳 NOFPU-1 の font 部分)。受入は a5 のやり直し (FONTCHK、東雲の EBDT 全字一致、変異)。T7 の実装中は x87 guard のまま進める。

## 5. 受入一式・データ移行・残る判断

システム更新は既存データを保持する媒体へ行う。settings.dbの新master上書きやfep.dbのdict_user消去を移行と呼ばない。DICT_META前後を二段に分け、ユーザー学習は件数・内容・二重加算なし・途中失敗後の再実行で照合する。v2へ戻す写しはバイナリと旧データの両方を残す。HDD再区画/再formatは不要。NHD変更は [D1]〜[D3] の範囲で別実行する。

外部apps/gameは現行引継ぎのビルド対象外という決定を守る。新SDK/layout/Unicode/lease変更に対するcaller追随の一覧を渡し、将来外部ビルド再開時に全再ビルドと混在拒否をゲートとする。外部未検証をv3同梱アプリの合格へ読み替えない。

後続の未起票項目は §4 の成果物を個別票で半日〜1日・受入10項以内へ分け、正常/故障注入/像増分を揃えてから実装する。HAL/NIC/DMA/音源の新しい装置操作は [HW3] のメーカー資料とBSD/MIT実装の版・ファイル・行を先に揃える。LAN票は既にIntelのSDMとBSD実装を参照しており (`TASK_LAN_82557.md:40`)、同票:51-52の資料とFreeBSD参照を使う。

独立レビューで確定する実装案は、T3 shell heap初期分割、T4 allocator hook/暫定DB identity、T7b parser/欧文font/作業域上限。これらは既決D契約の範囲で具体化するが、要件変更 (heap増量、必須FEP省略、高位kernel、後半scope追加など) が必要なら未決として別に判断する。設計書が存在するだけで測定や承認が済んだことにしない。


## 6. Opus 5.5 第1回所見への対応 (2026-10-01)

判定Request changesを受けた設計修正であり、以下は再レビューや実装試験の合格記録ではない。

| 所見 | 採否と理由 |
|---|---|
| B1 | callerを拒否する旧文は修正済み。共有低位PT/master CR3/pin/全出口を補強。親CUI集合まで許す提案は不採用: T2の例外は停止中callerであり、親対応は必須と確認できない。入れ子を副作用なく拒否する案を明記 |
| B2・N6・N7・N13 | 採用。WMはcaller ownerのまま、generic readはFD世代を検証しない実コードを確認。shell文脈open/protect、世代付きread lease、内容変更前失効、1 face/ownerとclose先行を具体化 |
| B3・N5・N12 | 採用。68h pass-through/起動時のみCODE設定を確認。未確認HW値を断定せずCODE復帰/cache失効、KCG失敗時CUI、性能参考測定、4入口とRa266回帰を追加 |
| B4・N8〜N11 | 採用。G2固定cellを維持しface/token/metricsを追加。x87全状態guardとpanic帰属、作業域/索引の上限、loca局所検査/cmap検証、T3後の画像増分再評価を明記 |
| N1〜N4 | 採用。4つのpage0 consumerを保存済み写しへ変更した後NP化。台帳は新型でなく単一FIXED/kernel用途へ統合。mailbox保存/復元+host排他、guestゼロ化と試験語を明記 |
| N14 | 採用。U21・microUIマウスキー・移植アプリを補いP7同時着手を明記 |


### 第2回所見への対応 (2026-10-01)

第2回はB1〜B4/N1〜N14をclosedとし、新規R1のみblocker。以下は第2回時点の記録。第3回確認は下記で完了、実装・実測の受入とは別。

| 所見 | 採否と理由 |
|---|---|
| R1 (P2) | 採用。app KAPIと非公開shell専用入口/registryを分離。WMのownerがappのままでもshell readは固定shell ownerを検証し、shell text/登録世代を要求。`ring3_call_from_user()==0`やdepthだけで許可する案は採らない。return/longjmp後の権限消去と偽token拒否を受入へ追加 |
| R2 (P3) | 採用。現行0x600のsnapshotをpage0全体へ拡大する設計を選択。追加BSSを計上しboot2/DOS回帰へ0x600〜0xFFFを明記 |
| R3 (P3) | 採用。欧文幅超過は横縮小・中央配置、筆位置はG2維持。1xの可読性/字形の欠けを判定し不合格ならfont再選定 |
| R4 (P3) | 採用。FD/MINIMALはKCGのみ、OpenTypeのcold I/Oは管理下ext2 sourceに限定。起動媒体とfont source媒体を別記 |


### 第3回・最終設計レビュー (2026-10-01)

Opus 5.5 (`claude-opus-5-5`) は **Approve**。第2回R1〜R4は全てclosed、残るblockerなし。文書設計の承認であり、実装・実機の受入完了ではない。WM呼出区間の印は既存 `ring3_wm_enter/leave` とlongjmp/dispatchでの解除機構の再利用を優先し、T7b-bの実装レビューで確認する。

現在の未測定ゲートはD15の速度、ext2 sourceのcold性能、corpusに基づく作業域実測、shell 1MiB/私有2MiB予算、FD peak本数、Ra266の `v86 -g`、68h=0Bh時のA9hの値、page0全体の保存・復元へ変更した後のboot2/DOS回帰。これらは本レビューのApproveで合格に置き換えない。

## 7. 未決の争点

独立レビュー r1 の2件は下表のとおり決定済みで、本文へ反映した。parser/欧文選定、性能/像/FD/実機の未測定は引き続き各段のゲートであり、合格済みにはしない。

| 所見 / 論点 | 削減・緩和する側の言い分 | 維持する側の言い分 / 決着に必要な判断 |
|---|---|---|
| **ユーザー承認 2026-10-18: 根拠のあいまいな基準の見直し** | corpus実測で上限、a6実測でFD本数、KCG hit率と入口/出口mapを記録。各段は10項超で分割 | §2-2・§2-4・§3へ反映。予約統合/backing撤去はT7a-2eへ統合、内容epochの2段は削除し使用中hsync BUSY・新fontは次回起動へ変更 |
| **決定 (ユーザー 2026-10-16)**: F5: shell wrapperのreturn PC検査 | **残す**。目的はなりすまし対策ではなく、偶発的な入口の誤用 (バグ) の検出。WM 経由で caller 判定が 0 になる経路 (`exec/exec.c:311-338,386-389`) でも shell 由来を確かめられる | 既承認 R1 と本文・b2 受入はそのまま。T7 の実装で検査の費用が目立てば、そのとき比較して削る |
| **決定 (ユーザー 2026-10-16)**: F7: x87 guardへの[HW3]適用 | CPU の命令だけの扱いは、**メーカーの資料 (Intel のマニュアル) を必須、BSD/MIT の実装は任意** (`docs/CONSTRAINTS.md` の [HW3] に 1 行足した) | §2-4 の根拠は Intel の資料の版・節を必須とし、BSD の実装は参照したときだけ書く |
