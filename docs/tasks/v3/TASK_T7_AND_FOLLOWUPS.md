# TASK_T7_AND_FOLLOWUPS — 低位解放・OpenType と v3 後半への接続

> 状態: **設計中 (2026-10-16、再照合 r3、F5/F7 決定済み)** — 本文を全部持つ実装用設計案。本版は **r3 (2026-10-16、独立レビュー r1 反映)**。共通依頼の状態行は維持し、旧版の Opus 5.5 第3回 Approve は本版の承認・実装・受入とは区別する。
> **レビューの経過 (PM、2026-10-16)**: astra が起草 (r2・r3) → Opus 5.5 が独立レビュー 2 往復 (r1「直してから」、r2「確定してよい (未決の争点はユーザーへ)」)。往復の記録は `~/os32-tmp/run/t3design/T7_AND_FOLLOWUPS/` (`opus_r1.md`・`astra_r2_response.md`・`opus_r2.md`)。F5 は return PC 検査を維持、F7 は CPU 命令だけならメーカー資料必須・既存実装任意に決定済み (§7)。本文へ反映済み、実装と未測定ゲートは残る。
> 設計: Codex。再照合基点 `c81b27bed7c1b0d05e5e7d9de9763c4eded844e5` (2026-10-15、main、作業木差分なし)。旧調査基点 `3c4171a`、元の設計レビュー基点 `59c4285`。D1〜D36・契約・順序は変更しない。file:line はこの再照合基点、相対リンクは正典の `docs/tasks/v3/` 配置を基準とする。
> 決定本文は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md)、柱/範囲/順序は [V3_PLAN](V3_PLAN.md)。前提は [T3](TASK_T3_LAYOUT.md) と [T4〜T6b](TASK_T4_T6_MODULES.md)。後半の既存票を置換せず、接続契約と未起票項目の着手ゲートを定める。

## 0. 再照合

単一利用者・単一タスク・協調切替、攻撃者を想定しない。検査はバグ・装置の誤動作からの復元と像の予算のためで、認証・暗号・権限分離強化は追加しない。性能は P100/32MB で D15 を満たす範囲に限る。上位決定は `TASK_MEMMAP_V3.md:17-52`、再照合規則は `DOCS_REORG_T3.md:60-63,86-93`、装置の根拠は `docs/CONSTRAINTS.md:92-98` [HW3] に従う。

| 元票の前提・着手条件 | 分類 | 今の根拠 / r3 での扱い |
|---|---|---|
| T2 の e5〜e12 の再init/revoke/再attach・200行・Unicode橋は引渡し待ち | 吸収済み | T2e 受入済み (`docs/tasks/agents/HANDOVER_2026-10-09.md:11`、`TASK_T2D_T2H.md:190-227`)。公開leaseを基盤として引継ぐ |
| f2〜f13/g の map・allocator・trim は引渡し待ち | 吸収済み | 同 HANDOVER:12-13。通常文脈の安全点を再利用し、font独自の借用guardは追加する |
| h の V86/STOP 復元が未確定 | 要改訂 | 既存session/正確なPTE復元・通常文脈回収は `kernel/v86_mem.c:50-207`、`kernel/paging.c:657-689`。未観測は台帳に残し §1 で受ける |
| T3→T4→T5a→T5b→T5c→T6b→T7a→T7b | 維持 | `V3_PLAN.md:70-82`。前段の配置・module・KCG facadeの受入後にT7へ。今回の文書更新は着手許可ではない |
| planar BB の旧低位 backing をpoolへ移す | 維持 | `include/memmap.h:148-150`、`kernel/memory_boot.c:219-224`。CLIENT/SURFACE契約は維持してbackingを替える |
| Unicode の暫定RO leaseをT7まで残す可能性 | 要改訂 | 撤去はT3の組表/KAPI化と同時 (`DOCS_REORG_T3.md:60`)。T7aは撤去済みを照合する |
| `.kcgfont` 廃止とKCGのpool cache 64KiB | 維持 | 旧低位cache/boot読込は `drivers/kcg.c:48-57,245-267` に存続。T5c module化とT7a caller撤去の境界を照合 |
| 旧font KAPIをNOSYSにする | 吸収済み | boot終了後のNOSYSは `drivers/kcg.c:247-263`。T7aはboot読込も撤去し旧slotのNOSYSを保つ |
| CUI表示復帰と両ページ消去 | 吸収済み | `kernel/v86_io.c:240-247,249-283`、`gfx/gfx_core.c:894-906` とT2受入。KCG CODE/cache、6Ah標準/拡張、CG窓WBの残件とは別 |
| page0 snapshot は0x600、全ページ化に+2560B | 吸収済み | `kernel/v86_bios.c:19,253-283`、`kernel/v86_mem.h:108-121` は既に4096B。+2560Bは再計上しない |
| 起動時不変snapshot・page0通常NP・BIOS直読撤去 | 維持 | 今のsnapshotはセッションごとの採取。`gfx/backend_pegc.c:156-163,477-514,571-574`、`gfx/gfx_core.c:362-367` に直読が残る |
| 636KiB backing撤去とmailboxの全session排他 | 未測定 | `kernel/v86_mem.c:63-101`、`include/memmap.h:154-155` (868B)。U7/U14は §3 の実証後だけ閉じる |
| KCG cold 1字34 I/O | 要改訂 | `drivers/kcg.c:152-169` はOUT34+IN32=66回、さらに `kcg_wait` 34回 (:39-42)。時間は未測定 |
| 診断・gcapの所在は旧kernel一体 | 要改訂 | DBGREF S0〜S2/KAPI75着地 (`TASK_DBGREF.md:3-9`、`sdk/kapi.json:3`)。gcap移設S3は未完、現物は `kernel/v86_gcap.c`。S3後の結線契約も §3 で引継ぐ |
| PEGCの低位直読撤去で既存port probeを残す | 維持 | R7FIX2後の窓はFFF00000h (`include/memmap.h:381`、台帳R7-1)。F00000hへ戻さず、保存BIOS値との比較だけ差し替える |
| font read lease/内容epochを新設する | 維持 | `fs/vfs_fd.c:292-301,420-457,584-607` はSQLite世代・既存staleのみ。font専用契約は未実装 |
| parser公開API・欧文選定・54KiB・1MiB/2MiB・FD peak・x87 | 未測定 | `userland/rust/font_test/Cargo.toml:10-12` と `userland/rust/font_test/src/lib.rs:274-282` は全ファイルread/Face::parseの試作。製品streamingの証拠ではない |
| P3/P5 NIC とnetwork /hostは後続のみ | 要改訂 | LAN LB-2c送信実機確認、LB-3以降は未完 (`docs/tasks/realhw/TASK_LAN_82557.md:3,8`)。LANFS段1は模型のみ (`tools/tests/lanfs_tdd.md:5-37`)、§4に接続 |
| 8MB/17MBを各段で受入 | 要改訂 | `V3_PLAN.md:39-46`、台帳:19。途中はNP21/W 64MB、容量別はv3最終段。Ra266固有の観測とD15目標は免除しない |

**現存成果物の実測再計算**: `build/out/kernel.map:1576-1578,4452,8011` は load=0x100000、max=0x99000=626,688B、`__bss_end=0x1940C0`。像=text/data/BSS/整列込み **606,400B**、残り **20,288B** (=0x199000−0x1940C0)、ページ切上げ後の余白は16,384B。DBGREF後の30,176B (`TASK_T2D_T2H.md:547`) から正味 **9,888B増**。LANだけの増分とは断定しない。map更新日時2026-10-15 23:47:40 JST、SHA256=`b83f79dff899041aba6070037e620f14bdae5baf4e5cec06deb1e6e03d9c4a23`。
同時に存在する `build/out/vmkernel.lz4` は500,430B、`boot/boot_defs.h:93` の508KiB上限まで19,762B、`build/out/kdebug.bin` は44,840B (file長、常駐量ではない)。再ビルドはせず読取計測のみで、配備像・同一設定の再現性は未確認。T7着手時はT3〜T6b後の同一一式で再計測し、core・module・私有領域・圧縮像を別勘定にする。予定の撤去益を先取りしない。

## 1. T7a 低位の所有権切替

**台帳から受けるID** (`DOCS_REORG_T3.md:25,51,74`、正典 `docs/tasks/DEFERRED_TESTS.md:23,57,60,62,81`)。台帳の本文・進捗は複製せず、閉鎖証拠の受け口だけを定める。

| ID | 本票の関門 |
|---|---|
| E10-4 | T7a-2d: 採取active中を注入で捕まえ、kill後のg/tv解放・gcap_ops解除・TVRAM30行復元を観測 |
| H-6b | T7a-2c: 6Ah標準/拡張の復元と9801でのgui_gate。実機未確認をNP21/Wだけで閉じない |
| H-5 (CG窓WB・68hのみ) | T7a-2c: 固定診断でCG窓属性とCODE復帰/cache失効を実機観測。kstackは本票へ移管しない |
| X-1 (page0 NPのみ) | T7a-1c / -2b: boot原本、直読consumer退去、通常NPと全出口復元。文字列/kprintf部分はT4/T5a |

T7aの到達点は「低位を一般poolへ追加」ではなく、bootinfoの保存済み写しとmailboxを扱ったうえで **V86が低位を直接使える状態**。`kernel/v86_mem.c` の636KiB backingを消すかは上位票U7の実証後に決める。高位lease窓をV86へ渡さない。

| 現利用 | 切替先 / 条件 |
|---|---|
| planar BB | boot時pool連続確保→gshell所有、T2のSURFACE経由。全planeポインタを更新し旧0x6A000参照をゼロにする |
| Unicode展開表 | T3のkernel組表+KAPI。T2暫定RO leaseが残っていれば先に撤去 |
| `.kcgfont` 展開/読込scratch | 廃止。T5cのKCG ROM moduleとpool上の上限64KiBキャッシュへ |
| bootinfo/BDA | IVT/BDAの起動時原本とbootinfoをcoreへコピー。以後の低位直読を§1-1の順で撤去 |
| `V86_TEST_MAGIC_ADDR` (現0x8C000) | 低位V86領域内の試験語。開始ごとにゼロ化し当該試験だけが設定。通常kernel stateの置場にしない |
| mailbox | 0x90000の内容をセッション前後で退避/復元。host側排他はツールの契約。kernelがhost接続を検出して拒否できるとはしない |

### 1-1. V86 状態遷移

`CUI_IDLE → PREPARING → V86_ACTIVE → RESTORING → CUI_IDLE`。呼出元はCPL=3の `userland/cmds/v86.c` (`v86 -t/-b/-g` のKAPI呼出し) であり、**そのASの存在自体は拒否理由にしない**。入場はCUI・非再入・呼出元以外の通常ASなし (parked/親ASも含む) に限定する。resident shellは通常ASとは別。T2の「CUI呼出元ASは停止中」の例外をここで具体化する。親ASを伴うCUI入れ子はBUSYで副作用なく拒否し、単独起動へ案内する。親を含む集合のpin/復帰は本案に含めない。GUIと他ASありの拒否は低位PTEを触る前に行う。

PREPARINGではT2のuaccessでpath/secondをkernelの上限付きstagingへコピーし、`V86Gcap`等の出力範囲をcaller PDで検証する。NULL可否は既存API契約どおり。未終端/範囲外は副作用前に失敗。callerのslot/generation・PD・KAPI復帰状態を固定して、slotにV86呼出し中の印を付ける。caller所有のPD/backingをpinし、park/resume、trim配送、別アプリ起動とcaller teardownを禁止する。この停止は他アプリへのスケジューリングではない。

coreの復帰状態、低位PTE、mailbox/boot dataを低位外へ保存 → **caller CR3を保存し、masterでも有効なcore stack/TSS.ESP0を確認してmaster CR3へ一時切替** → V86用低位PTE/TLBを更新 → 実行、の順。master切替後はcaller VAを直接参照せず、path・出力・captureを全てkernel stagingで扱う。ファイルopen/loop deviceにもstaged pathを渡す。V86セッション中はcaller ASが存在しても実行せず、通常ASの地図検査を停止する。低位PTはmasterと各PDが共有しているため、そのUSER変更は全PDへ及ぶ。callerを停止するのはこのためで、通常map APIからはこの専用変更口を呼べない。ページ0の実物写像はPREPARINGからRESTORING完了までに限定する。

正常終了・#PF/#GP・STOP・準備途中失敗の全出口は、到達した段の印を持つ同じRESTORINGを通す。**実物IVT/BDA等をRW写像中に復元 → 通常低位PTE/SHM・trampolineのPDE権限を復元 → KCG復帰 → master側TLB反映・地図検査 → 保存したcaller CR3へ復帰 (TLB更新) → caller generation確認 → kernel stagingからchecked copyout → pin/呼出し中印を解除 → KAPI復帰**。V86が変えた低位データをkernel stateとして信用しない。STOPによるcaller終了要求は復元を飛ばさず、復元後のT2安全点で通常回収する。復元に必要なcode/stack/保存領域を低位へ置かず、master切替をまたぐreturn先もcaller VAに置かない。既存kernel stackで足りるかhigh-waterを測り、不足なら固定帯を無断拡大せず設計を再確認する。復元検査失敗時はcallerへ戻らず停止する。

masterの検査は共有低位PTの復元検査でもあり、caller復帰時のCR3ロードでそのTLBも更新する。KCG復帰では既存定数 `MODE_FF1_PORT/MFF1_KCG_CODE` でコードアクセスモードを再設定し、ROM/CG RAM由来のキャッシュを全失効する (module未ロード時はcoreの復帰口でmodeだけ設定し、次のmodule initは空cache)。68h=0Bhを残した時のA9hの値は未確認であり、本設計の根拠を実測済みとはしない。

ページ0をNPにする前に、`pegc_identify`、`pegc_boot_sync_record` のBIOSフラグ、`gfx_identify_candidates` (`gfx_core.c`)、`v86_bios_save_real` の4経路を起動時の保存済みcore写しへ変更する。PEGCの実port probeは存続しBIOS値との比較だけ写しを使う。V86用原本はページ0全体 `[0, PAGE_SIZE)` (4096B、0x600〜0xFFFを含む) の上書きされないboot snapshotとする。既存の `real_lowmem[PAGE_SIZE]` (`kernel/v86_mem.h:121`) をT5b保存口で起動時に一度だけ採る原本へ転用し、session resetの対象から外す。`real_saved` は原本有効フラグへ役替えし、セッション終了で消さない (`kernel/v86_bios.c:253-272` の再採取/クリアを変更)。T5b側にも同じ原本を重複保持せず、この唯一のcore写しを共有する。page0用の編集stagingは置かず、追加配列は0B (既存の2560B増は吸収済み)。退避は低位consumerの書換え/NP化より前、復元も全ページが対象。開始はmailboxを退避 → 実物低位640KiBをゼロ化 → 原本から物理page0へ直接4096Bをコピー → `v86_bios_setup` が物理page0上だけでguest BIOS情報を編集 (`kernel/v86_bios.c:275-283`) → guest image配置 → 実行の順。終了時も同じ原本から物理page0全体を戻し、原本自身は変更しない。これにより前回guest/旧font/boot stackを再公開しない。終了時にmailbox原内容を復元する。ゼロ化の前に全boot stack/bootinfo consumerの退去を確認する。

`memory_boot_fixed` の旧FONT/UNICODE/BB/残余の低位行は、`[0, MEM_CONV_END)` の単一予約 **LEDGER_R_FIXED / LEDGER_OWNER_KERNEL / WB** (用途名V86_LOW、一般poolへ供給しない) に統合する。新しい台帳型やセッションownerへの移譲は不要で、V86状態が排他使用権を表す。BBのpool移動は別のSURFACE_BACKING記録にする。memmap定数・`tools/gen_memmap.py`・`02_memory.md` の説明を同じ差分で更新し、`make docs-gen` で `build/out/MEMMAP.md` を確認し、開始/終了でこの予約の型/owner/PFN数不変を検査する。

mailboxの868B (`include/memmap.h:154-155`) は起動時に低位外のKHEAPへ1回確保し、core所有で保持する。固定BSSへ配列は足さず、pointer等の管理分はcore管理枠、868Bとallocator管理/整列はKHEAP実占有に計上する。未確保ならV86入場を低位変更前に失敗させる。H-7 (`docs/tasks/DEFERRED_TESTS.md:73`) でgameはv3ビルド外だが、hostツールとの既存の保存/復元契約は維持する。path/出力stagingの確保失敗も低位変更前に返す。mailboxはDOSへ申告する640KiBの内側なのでV86中はguestが上書きしてよい。保存・実行・復元の**全セッション中**にhostが読書きしないことをツール側のセッション所有で保証する。`game/tools/autoplay/driver.py` の変更が必要なら外部引継ぎ事項として記録し、外部リポジトリの実装を完了したと扱わない。

H-6b/H-5の復元は6Ah標準/拡張、68h CODE、GRCG/EGC残状態、CG窓WBを一組で扱う。CGを根拠なくUCへ変更しない。9801で存在しない9821用I/Oを発行しないことも観測する。[HW3]に必要なメーカー資料とBSD/MIT実装の版・ファイル・行をT7a-2cの開始時に固定する (現状は未収集で、新しいポート列はこの票で確定しない)。既存根拠の `kernel/v86_io.c:238-283` と `drivers/kcg.c:61-72` に対し、資料との不一致と実機結果を記録する。読む口は対象固定の診断とし、公開の任意物理read KAPIを追加しない。

### 1-2. OpenType前の中間段を動かす

T7a→T7bの間もGUIの日本語を失わない。**KCG ROMの字形取得を唯一の文字供給にして受け入れる**。CUIは引き続き内蔵フォント、GUIはKCG KAPI/facade経由で描画。KCG moduleのコード/キャッシュは低位の外、未収録字は固定の代替字形。`.kcgfont` の旧KAPIスロットはNOSYSで残し、旧ファイルを読み直すfallbackを作らない。フォントファイルのないFD/MINIMALでも英数入力・編集と文字表示を確認する。KCG moduleのロード/init失敗時はGUIへの切替を失敗させCUIを維持する (日本語を欠いたGUIを成功扱いしない)。T7a-1でcold/warm各1000字、ROM I/O数、cache hitと入力→描画時間をD15への参考値として記録する。cold全角1字はOUT34+IN32=66 I/Oとwait34回 (`drivers/kcg.c:152-169`)。I/Oとwaitを別計数し、実測前に速度合格とはしない。

## 2. T7b OpenType ストリーミング

### 2-1. 実装範囲と読込 API

最初の対応は配布するIPA原本と選定した欧文フォントが使うTrueType `glyf` 輪郭。CFF、可変font、複雑なscript shapingの一般対応を「OpenType」という名で暗黙に約束しない。対応外formatはKCG/代替字形へ。欧文フォントの選定とライセンス確認は実装前ゲートであり、本票で新しい配布物を承認済みとしない。日本語fontは既存の同意付きfetch経路から配布物へ入れ、リポジトリへ追加しない。

D36どおり依存はMITソースをvendor化し、版・license・差分・最終386命令列を記録する。共有Rust crate案 `userland/lib/font/` をgshell静的リンクとlibos32gui shlibの両方で利用する。API案は以下。ハンドルは各owner内のslotと再利用しないu32世代を持ち、満了時はslotをretireする。

- `font_open(source, limits) -> FaceHandle` / `font_close(face)` / `font_trim(face)`。
- `font_lookup(face, codepoint) -> GlyphToken {face_slot, face_generation, glyph_id}`。close/reopen後のtokenは描画・寸法取得ともSTALEで拒否する。
- `font_metrics(face, px) -> {ascent, descent, line_gap}`、`font_advance(token, px) -> advance`、`font_rasterize(token, px, target) -> bitmap_bounds`。寸法は符号付き26.6固定小数点、pxは16/32、座標/丸め/overflow規則を共通化する。bboxとadvanceを混同しない。

既存libos32guiのG2 (`text`/`measure_text` の半角8・全角16・高さ16) は維持する。GUI adapterはUnicode幅判定を共通にして固定cellへ配置し、筆位置はcell幅だけ進める。glyphはraster前のbboxで幅を測り、cellを超える欧文 (W/M/m等) は横方向をcell幅へ縮小して中央配置する。横縮小はadvanceを変えず、縦は共通baselineを維持する。cell clipは最終安全境界に限定し、縮小後の字形が欠けた結果を受け入れない。欧文の比例advanceはfont APIから取得できるが既存widgetへ直結しない。baselineはface metricsから同じ丸めで求め、KCG fallbackも同じcellへ置く。T7b-aで欧文fontの横縮小後の1x可読性・上下の欠けなし (W/M/m、アクセント、descenderを含む) を確認し、読めなければ等幅候補の再選定をゲートとする。T7b-aでG2維持の承認とcaller一覧 (text/measure_text、edit/caret、clip、FEP候補窓) を照合し、T7b-bで画像と測定を一致させる。比例幅GUIへ変更したい場合はG2改訂の別判断が必要。

sourceは `read_at(offset, dst, len)`、file_size、**owner・FD generation・内容epoch**を持つ。seek/readの短読は成功扱いせず、他ownerのFDを借りない。gshellのopen/reopenはshell自身の通常文脈だけで行い、`vfs_fd_set_protect` を設定する。GUI op中は設定変更要求だけ記録し、そのopからopenしない。描画時の遅延openもしない。shell shutdownでは明示closeする。アプリのfont FDはそのownerでopenして通常終了回収に任せる。

read_atは `offset <= file_size && len <= file_size-offset` を先に判定し、要求長を返した場合だけ完全成功とする。len=0は有効な範囲で0、短読/媒体エラーでは当該glyphを捨てる。font_closeは借用終了後にだけFDを閉じ、STALEなleaseも登録世代一致なら解放可能とする (誤った世代で再利用後のFDを閉じない)。自動再openは次の通常安全点に1回だけ試し、失敗した設定世代は明示変更までKCGに固定する。

現在 `VfsSqliteLease` だけにあるfd+generationの検証を、SQLite cookieを流用せずfont用read leaseとして追加する設計。kernel側がowner/lifetime/in_use/generation/staleをread_at/closeの各処理内で検証する。seek状態を公開せずread_at内部で位置決めと読込を一体で行う。ユーザー側の事前checkだけにしない。権限は以下の**別入口**で固定し、ownerを呼出引数で選ばせない。

| 入口 | 権限の根拠 / 検証 |
|---|---|
| app用public KAPI `font_source_open/read_at/close` | int80で保存されたcallerのCPL=3・slot/generationを根拠に、`ring3_call_from_user()!=0` かつWM外を要求。lease表はcaller owner専用で、ownerは `res_owner_get()` とsaved callerの一致も照合する。shell leaseはこの名前空間に存在せず、shell token/生FD/他app tokenはINVALまたはSTALEで拒否。KAPI直呼びのshell/WMをこの口で特別許可しない |
| shell内部 `shell_font_source_open/close` | loaderが検証した現行gshell画像世代・shell textの直呼びで、`ring3_in_syscall==0`・WM外・`res_owner_get()==GUI_SHELL_OWNER` を全て要求。登録済みshell sourceのみ生成/破棄する。app引数からleaseを登録しない |
| shell内部 `shell_font_source_read_at/validate` | 同じgshell世代のshell text直呼びかつ `ring3_call_from_user()==0`。shell専用registryで照合し、期待ownerは固定の `GUI_SHELL_OWNER`。WM中は `res_owner_get()` がappでもよいが、app registryへはアクセスしない。通常shell文脈か、登録済みgshellのhandler/pump/owner_exit呼出区間であることも確認する |

shell内部入口はKAPI/trampoline/ユーザーimport表に載せず、resident shell loaderが検証済みgshellだけへ渡すsupervisor専用サービス表で供給する。入口の薄いcdecl wrapperは直接のreturn PCがloader由来の現行shell executable text範囲内かを検査してから共通VFS実装へ進む (callerが申告する番地は使わない、尾呼出しで検査を省略しない)。サービス表とregistryはUSERなし。shell登録世代が終わればサービス参照とleaseを失効させ、旧gshellからの利用を拒否する。これによりappがshell番号を知ってもKAPI経由では到達しない。resident限定入口/サービス表の実装と混在拒否をT7b-bの変更対象に含める。

`ring3_call_from_user()` は `ring3_guard_active(in_syscall, wm_depth)` であり、WM中に0になることを実コードで確認した (`exec/exec.c:309-336,377-379`)。この値やdepth単独をshell権限の証拠にせず、上記の非公開入口・shell text/世代・専用registryと組み合わせる。WM呼出区間の印はkernelがhandler/pump/owner_exitの前後で設定/解除し、通常returnに加えてpark/STOP/faultのlongjmpとdispatch入口でも消す。font借用中は§2-4のguardでpark/STOP配送を止める。試験はappからshell lease/生FD指定を拒否、WM中の正当なshell read成功、WM終了直後のappから拒否、close/reload後の旧世代拒否、例外/park経路で権限の印が残らないことを含める。

ttf-parserのprivate `glyf`内部へ当然にアクセスできるとはしない。T7b-aで現在のcrate公開APIを調査し、字形単位の安全な薄層が実現できなければ、必要なテーブル/単純輪郭readerを明示的に実装するか、ライセンスを保った限定vendor化をレビューする。`Face::parse`へ全ファイルを保持する試作を製品経路へ戻さない。採用方法・依存版・変更範囲はこのゲートで記録する。

### 2-2. 常駐表とメモリ上限

上位票の「表≈100KiB」は廃止したサブセットの見込みで、IPA原本のcmap/loca/hmtx合計だけで343,649B (約336KiB)になる。再照合時の `assets/fonts/ipaexg.ttf` / `ipaexm.ttf` のSFNT directory (offset12から20entry) を読取計測し、両方で245,775+48,960+48,914B。原本hash/取得経路は `assets/fonts/README.OS32:4-16` と一致した。**全表常駐を前提にせず、表ディレクトリと選択subtableの索引だけ常駐させる設計案**とする。

| データ | 読み方 / 上限の案 |
|---|---|
| head/hhea/maxpとtable directory | 範囲検査後に必要scalarだけ保持。索引/scalar/face管理合計4KiBまで、table数64まで。全font常駐なし |
| cmap | 対応format4/12のheader・検索用indexを保持、segment/group本体はread_at。検索回数は要素数の対数に制限 |
| loca/hmtx | glyph IDから必要entryだけ読む。長短loca、末尾advance共有を区別 |
| table read cache | 4KiB×4slotの私有LRU。上限16KiB。file/generation/offsetでkey化 |
| glyph scratch | 入力合計2KiB、展開点512・contour64・component32・深さ8まで。点/edge/再帰フレームの専用領域を別に16KiB以内、曲線分割後edge1024までとし、超過は代替字形。製品fontで欠ける字を一覧にする |
| raster scratch | 初期上限16/32px。bbox各辺を最大32pxへ制限し外側余白各4pxを含む最大40×40、f32累積面6400B + 出力/行scratch等を含め合計16KiBまで。巨大bearing/幅/高さはfallback。pitch×heightと座標をoverflow検査し不足で途中描画しない |
| bitmap cache | 8MB構成は上位票の64〜80KiB/owner内。glyph+font_generation+px+render_modeをkey、LRUで返却 |

これは予算案で、16KiBのread cacheでOpenType sourceのcold性能が目標を満たすかは未測定。font作業域は4+16+2+16+16=54KiB/owner (bitmap別) の設計上限。複数faceでも作業域は直列に共有し再入を拒否する。shell画像/BSS+heap+40KiB stack+guard+font peakが1MiB内、アプリのfont領域は私有総量2MiB内であることをT7b-aのmap/peakで確認する。16MB以上の256KiB bitmap cacheも私有勘定。gshellはT3の起動時固定分割 (exec_heap256KiB、sbrk最低128KiB) とresident CRTを使う。共有glyph cache/GUI補充opはこの票に追加しない (U21)。T3は現在のmapで初期分割を確定し、未測定T7bに依存させない。T7b-aでcrate導入前後のgshell画像とshlib text/data/reloc/paddingを比較し、shell/shlib帯と2MiB私有枠への課金を確認する。不足時は固定分割定数/リンクASSERT変更を独立差分として再レビュー・全再ビルド・再受入してから製品接続する。実行中に境界を動かしたり帯そのものを無断拡大したりしない。

FD表は全16本、通常open対象は3〜15の13本。初期案は各ownerのactive face用FDを最大1本、gshell+4 appで最大5本とし、欧文/日本語切替時はcloseして開き直す。T7b-aではFEP/SQLite (DB/journal含む)、標準redirect、pipe、loaderを含む実peakを計測し残数を記録する。FD不足はKCG fallback、再openを毎字繰り返さない。FD増設を予算不足の暗黙の解決にしない。

### 2-3. 解析・描画・エラー

font/fileの状態は `CLOSED → VALIDATING → READY → CLOSED`。検証中のfaceを公開しない。table offset/length、glyph ID、glyph span、contour/point命令のrun長を検査する。locaは全走査せず要求glyphの隣接2entryを読み `0 <= start <= end <= glyf_length` を検査する。cmapは対応する (platform,encoding)=(3,10) format12を優先、次に(3,1) format4とし、未対応ならfallback。検索に使うsegment/groupのソート・非重複はopen時にストリーム走査で検証し保持しない。そのcold I/Oも計測する。format4のidRangeOffsetは当該wordを基点としてglyphIdArrayの範囲内か検査し、delta加算前後のglyph IDを検査する。複合glyphは循環・再帰深さ・総component/点数・総読込byte数を上限付きで処理し、最初に選定fontで非対応にするなら検出して代替へ落とす。fontの実測最大字形をOpenType一般の保証にしない。

1字の処理はlook up→全入力取得/検証→scratchへ完全raster→cache commit→描画。短読/破損/確保失敗で部分bitmapをcacheへ入れない。cacheから描画する場合も先にsource lease/content epochを確認する。設定変更時は借用中の描画終了を待つ通常安全点で旧face/token/cacheを失効 → 旧FD close → 新face検証 → 公開とする。旧faceを残す二重openはしない。意図的にpeak FDを1本へ抑える選択で、再open失敗時はKCGで継続する。trimはT2の通常文脈・非再入安全点でbitmap/read cacheを返し、描画中に借用中のentryを解放しない。

### 2-4. 内容の更新とx87境界

FD generationは番号の再利用を検出する値で、ファイル内容の世代ではない。font read leaseはmount世代+inode identity+u32内容epochをkernel内に保持し、font用lease/face世代もu32とする。ext2の置換rename/unlink/umountによる既存staleに加え、同inodeへのwrite/truncateの**変更前**に関連font leaseを失効し内容epochを進める。対象はFD write、O_TRUNC、path write/stream、inode write/truncateを通るext2更新で、`fs/vfs_fd.c:207,420-457`、`fs/vfs.c:527`、`fs/ext2_vfs.c:143,288,407-429` を共通の変更通知へ結ぶ。失敗したwriteでも失効は戻さず再open時に再検証し、epoch満了はwrapせず当該leaseを閉じる。この非wrap契約と世代満了時のretireは維持し、u32満了の強制注入はT7の試験・変異から外す (型/境界処理はレビュー対象)。各read_at内部と全入力取得後/cache commit前、cache-hit描画前にも検証する。更新中のfaceを継続せずsafe pointで再openして全索引を検証し直す。hsync置換・同サイズ上書き・truncateを別々に試験する。外部変更を観測できないHostDrv/SerialFS/LAN版 `/host` などinode/変更通知のないsourceは最初の製品font sourceとして拒否しKCGへ落とす (事前に管理下ext2へ配備する)。mtime/sizeやヘッダだけの比較を内容一致の保証にしない。このVFS追加はT7b-bの前提であり、現行generic FDで実装済みとはしない。

x87 guard の実装開始前に [CONSTRAINTS [HW3]](../../CONSTRAINTS.md#hw3-新しいハードウェアの実装はライセンス上参照しやすい既存の実装資料を参照してから設計する) に従い、Intel 資料の版・節を固定する。CPU 命令だけの扱いでは BSD/MIT の既存実装参照は任意で、参照した場合に版・ファイル・行を記す。採用ラスタライザがf32を使う場合、P6全体を待たず **T7b-aにx87隔離guardを先行実装する**。gshellのfont描画/寸法の最外周でcallerのx87全状態 (register stack、status/tag、CW) を専用の低位外領域へ保存、例外をmaskした既知CWで初期化し、成功/失敗とも復元する。CWだけの保存では不足。guard中のpark/アプリ切替/STOP処理/再入を禁止し、要求は終了後の安全点で処理する。IRQでFPを使わない。アプリ私有側も同じguardで呼出元のFP計算を壊さない。x87非搭載CPUは検出してこの経路を実行せずKCGへ落とす。386対応は最終命令列検査を行う。x87保存領域は4KiBの管理予算に、追加codeは画像増分へ算入する。

parser/rasterは境界検査済み入力と上限付き領域だけを使い、allocation failureはResult、unchecked index/unwrap/panicを製品経路に残さない。host fuzz/破損font・全上限±1を通し、panic不在はテストだけで証明せず全呼出経路のレビュー条件とする。gshell側の予期しないfault/panicはfont active印でapp-killより先に検出しfail-stopする (WM faultを無関係なアプリだけの失敗へ畳まない)。STOPは正常leave後に配送する。T7b-bでアプリがCW/rounding/例外maskを変更しx87 stackに値を残した状態からWM描画し、状態の完全復帰とfault帰属を検査する。

## 3. T7 の実装分割と受入

各行は半日〜1日の独立受入単位。番号付き条件は各段7項以下、同じ判定の機種/入口違いは試験行列で展開する。各段で対象像の増減・BSS・pool peakを記録し、10項超または1日を超える依頼は契約の境界で再分割する。原則は表の順、b-aの試作は実装sourceを差し替えられるfixtureとし、b-bのlease/epoch完成前にgshellへ製品接続しない。T7b-a1の着手条件はG2維持とcaller一覧の承認 (§2-1)、a5のレビュー条件は全呼出経路のpanic/unchecked不在 (§2-4) とし、観測による受入とは分ける。

| 段 (旧4段の細分) / 主な対象 | 観測で閉じる条件 | 故障注入・変異 (正常対照も必須) |
|---|---|---|
| T7a-1a BB / `gfx/gfx_core.c`・planar backend | ①boot連続pool確保 ②全planeが新backing ③boot→gshell所有移譲1回 ④lease/present/再attachで同じ絵 ⑤失敗時ページ漏れ0 | 最後のplane旧番地残し、pool確保失敗 |
| T7a-1b KCG / `drivers/kcg.c`・`kernel/boot_font.c`・deploy | ①配備予行で旧font無し ②KCGのみのGUI日本語/CUI英数編集 ③cache≤64KiB ④module欠落/init失敗はCUI維持 ⑤旧slot NOSYSと低位font参照0 ⑥cold/warm1000字・I/O記録 | cache満杯/確保失敗、旧font fallback再導入。D36の `os32_lz4` は呼出し0を確認して撤去 |
| T7a-1c boot原本 / T5b保存口・BIOS consumer | ①書換え前に4096B保存 ②原本不変 ③4経路の直読0 ④Unicode橋撤去済み ⑤bootinfo/boot stack退去 ⑥各セッション前後の物理page0全4096Bが原本と一致 ⑦boot2/DOS起動成功 | snapshot末尾欠落、終了時原本フラグ消去、consumer1本直読へ戻す (NP化は次段) |
| T7a-2a 入場 / exec・uaccess・session | ①単独CPL3呼出し成功 ②GUI/親/他AS/parked/再入を変更前拒否 ③不正path/outputでPTE不変 ④caller pin/CR3退避 ⑤master中caller VA参照0 ⑥成功/失敗ともpin解除 | 終端欠落、2本目pathのcopy失敗、mailbox起動時確保失敗、pin解除漏れ |
| T7a-2b 低位切替 / paging・v86_mem/bios | ①fixtureの直接低位640KiB実行 ②通常page0 NP ③page0/mailbox原内容一致 ④全出口CR3/PTE/PDE/TLB一致 ⑤前回データ/0x8C000ゼロ | 各準備境界失敗、末尾PTE復元/TLB反映/mailbox復元を1つずつ除去 |
| T7a-2c 表示復元 / v86_io・KCG facade・固定診断 | ①[HW3]根拠対を固定 ②6Ah標準/拡張復元 ③68h=0Bh注入後CODE+cache失効 ④CG窓WB/VRAM UCの前後一致 ⑤9801 gui_gate・9821実機再描画 ⑥R7FIX2のFFF窓とCUI復帰維持 | mode復帰/失効省略、古いF00000h窓へ戻す。A9h値は実測記録 |
| T7a-2d 採取中断 / gcap・STOP・DBGREF境界 | ①active印を見てSTOP注入 ②kills増分1 ③g/tv alloc−free差0 ④gcap_ops/active解除 ⑤TVRAM30行/CR3/出力規則復元 ⑥次のv86実行成功 | detach/release片方除去、準備失敗/#PF/#GP/STOPを各入口で注入 |
| T7a-2e1 予約統合 / memory_boot・memmap・gen_memmap・02_memory | ①低位全160PFNが単一FIXED/kernel/WB ②session前後で型/owner/PFN数不変 ③一般pool供給0 ④BBは別SURFACE_BACKING ⑤docs-genと台帳/定数/説明一致 | 旧低位予約1行残し、owner/端PFN違い。旧backing製品経路は維持 |
| T7a-2e2 backing撤去 / v86_mem・製品結線 | ①通常経路を直接低位へ ②159ページ確保0 (旧経路との差636KiB) ③低位予約とpool不変 ④GUI→CUI→V86→GUI20回漏れ0 ⑤Ra266 -g/boot2/DOS回帰 | 連続pool枯渇でもV86可 (他stagingは確保済み)、旧低位backing別名残し |
| T7b-a1 採用ゲート / font crate・assets | ①公開API/必要reader範囲固定 ②IPA/欧文原本のhash/license/fetch確認 ③最大点/輪郭/component/span表 ④上限超過字一覧 ⑤W/M/m/アクセント/descenderの1x可読性 | CFF/可変font/未対応cmapはKCG。読めなければ欧文再選定 |
| T7b-a2 x87 / 共通guard・exec安全点 | ①全状態一致 (save領域の予約byteは除外) ②既知CW内で実行 ③再入拒否 ④STOP/parkはleave後 ⑤非搭載はKCG ⑥faultの帰属が一致 | 値8本/CW丸め/例外mask変更、失敗出口のrestore除去。f32前に完成 |
| T7b-a3 table reader / directory・cmap・loca/hmtx | ①全file常駐0 ②索引≤4KiB/read cache≤16KiB ③format4/12対照一致 ④隣接locaとadvance一致 ⑤短読で公開0 | offset/length overflow、順序逆転、idRangeOffset境界±1 |
| T7b-a4 glyph / 単純・複合輪郭 | ①製品corpusの輪郭対照一致 ②入力≤2KiB/点512/contour64 ③component32/深さ8 ④edge≤1024/専用領域≤16KiB ⑤不対応字は一覧と一致 | 循環、run長、各上限±1、途中読込失敗で部分commit拒否 |
| T7b-a5 raster / 16・32px・font_test | ①26.6寸法とbaseline対照一致 ②bbox最大40×40/作業域≤16KiB ③完全bitmapだけcommit ④最終386命令列検査 | bbox/pitch/座標overflow、allocation failure、途中raster失敗 |
| T7b-a6 試作計測 / map・FD・ext2 | ①shell1MiB/8MB私有2MiBの内訳表 ②作業域54KiB/bitmap別枠 ③FD peakと残数 ④cold/warm1000字 ⑤起動/入力→描画計測 ⑥上限・採用法を実測で確定 | corpus cache thrash、font FD不足、memory上限ちょうど/超過。試作全file readを製品へ戻さない |
| T7b-b1 app read lease / VFS・public KAPI | ①open/read_at/closeのowner/世代検証 ②短読失敗 ③1face/owner ④STALE closeで別FD保持 ⑤通常終了でFD/owner漏れ0 ⑥[ABI1]〜[ABI3]と一式再ビルド | 異owner/旧token/生FD混用、FD再利用 |
| T7b-b2 shell入口 / loader・WM・service表 | ①shell世代/text/文脈検査 ②appから到達不可 ③WM中shell read成功 ④app ownerのFD不使用 ⑤park/fault/dispatch後の印0 ⑥reloadで旧lease無効 | wrapper検査/印消去を除去、WM後app再利用。新しい権限モデルは足さない |
| T7b-b3a 内容epoch基盤 / VFS FD write・O_TRUNC | ①FD同サイズwriteの変更前失効 ②O_TRUNC変更前失効 ③失敗writeでも失効維持 ④各read_atとcommit/cache-hit前検証 ⑤再openは安全点だけ ⑥host source拒否 | FD write/O_TRUNCの通知を各1つ除去、読込途中に更新。fixture限定で製品へは未接続 |
| T7b-b3b 全更新口 / ext2 path・stream・inode・rename等 | ①path write/streamの変更前失効 ②inode write/truncateの変更前失効 ③rename/unlink/umount失効 ④hsync置換/同inode再利用で旧face拒否 ⑤更新口一覧と実呼出traceの対応漏れ0 ⑥b3aのFD経路回帰 | 通知を経路ごとに1つ抜く、同サイズ上書き/短縮/失敗後再open。全経路完了後だけb4へ |
| T7b-b4 製品結線 / gshell・shlib・trim | ①配備予行で新一式一致 ②旧face失効→close→新検証公開 ③設定変更/パネル終了/別app open後再描画 ④trim後再描画 ⑤借用中解放0 ⑥fault/終了でFD/owner漏れ0 | 借用中trim/STOP、close後open失敗、partial bitmap投入 |
| T7b-b5 G2表示 / text・measure・edit・FEP | ①1x日本語/欧文画像可読 ②W/M/m等の欠け0 ③text/measure/caret位置一致 ④clip/baseline一致 ⑤FEP候補窓一致 ⑥旧tokenで寸法/描画STALE | cell幅とadvance混同、縮小せずclip、fallback baselineずれ |
| T7b-b6 統合 / 製品計測・回帰 | ①cold/warm1000字とpeak記録 ②D15の計測判定 (容量別は最終段) ③FD/MINIMALでKCG ④破損/短読/上限超過で操作継続 ⑤x87完全復帰 ⑥20回trim/設定変更/終了で増加0 | 内容epoch確認/guard復元を抜き実経路で検出、枯渇後の次起動 |

T7a-2bはCR3/PTEと直接低位のfixture内でU7を実証する。-2c/-2dの確認後、-2e1で予約統合とdocs-genを独立受入し、-2e2で旧backingの製品経路を撤去する。page0通常NPは4consumer退去後だけ有効にし、復元失敗注入はfail-stopを期待値とする。-1cの比較時点はPREPARINGの低位変更前とRESTORINGの復元後・NP化前で、0x600〜0xFFFも含む。guest実行中のpage0書換えは禁止せず、DOS起動成功はbyte一致とは別に観測する。b3aはFD write/O_TRUNC、b3bはpath/stream・inode直接更新と名前/寿命の変更を受け持ち、共通通知の結線を照合する。準備段だけの合格で製品の成功を宣言しない。

### 3-1. 像と実行時占有の見積もり

以下は着手パック用の増分枠 **[推]** (計測値ではない)。T7a入口/T7b入口のmapを基準に各段で内訳を埋める。上限超過時は依存/作業域を再検討し、帯・D契約・目標を無断で変えない。

| 対象 / 担当段 | 見積もりと勘定 |
|---|---|
| T7a core (1a〜2e2) | code/管理 +4〜8KiB [推] (mailbox pointer等を含む)、不変page0追加0B (既存real_lowmemを転用、T5bも同じ原本)、mailbox配列追加0B (KHEAPへ)。既存4096Bは再計上しない。減る旧font読込/codeは実測まで控除0 |
| T7a pool / KHEAP | planar BB128KiB (32頁)、KCG cache≤64KiB + moduleのmem_size。mailbox868Bは起動時確保で保持し、allocator管理/整列込みの実占有をKHEAPへ別計上。path2本とV86Gcap stagingは現在のAPI上限/sizeofで事前確保しpeakを別記、stagingはmaster切替後に確保しない。backing撤去の636KiBはpool peak差で像の減少にはしない |
| T7b core (b1〜b3b、guard結線) | code/registry +4〜8KiB [推]、最大5 active source、epoch/世代表はtarget sizeofで算定。VFS/execをfont crateと一緒にcoreへリンクしない |
| T7b shell/shlib (a2〜a6、b4〜b6) | parser/raster text等は各32〜64KiB増の仮枠 [推]。gshell静的リンクとshlib共有textを別計上、reloc/data/整列込み。作業域54KiB + bitmap64〜80KiB (8MB) / 256KiB (16MB以上) はowner私有。shell40KiB stack+guard/heapも1MiB内 |
| DEBUG部品と製品 | 新診断/fixtureは対のkdebugまたは試験専用像に置き、core必須復元コードは部品OFFでも残す。部品file長とページ切上げmem_sizeを区別し、KDEBUG=0/1のpool peakを別測定 |

core追加の合計は **8,192〜16,384B [推]** (=T7a 4〜8KiB + T7b 4〜8KiB、両端ともpage0追加0B・mailbox配列追加0B)。F1の原本転用だけなら9,060〜17,252B、F4のmailbox868BをKHEAPへ移してこの値となる。KHEAPは既存固定帯内の実占有が増えるため、868Bとallocator費用を含むpeakを確認し、core像減と空きheap増を取り違えない。現存20,288Bとの単純差は3,904〜12,096Bだが、T3〜T6b後の入口実測・管理表sizeof・整列と各段実増分が確定するまで予算成立を宣言しない。撤去益は先取りせず、圧縮像は別にMAX_IMAGE_SIZEへ照合する。

### 3-2. 試験の実行と判定

変異は実ソースの復元・検証を1箇所ずつ欠かし、正常対照GREEN→意図した観測のREDを対応づける。ビルド失敗は検出成功に数えない。font readerはhost破損corpus/上限±1とguest実I/Oの両方、V86は `check-v86-gcap-host`・T2のrecovery/lifecycle/SHM/surface試験を改訂して使う (`TASK_DBGREF.md:271-277`)。最後の変更後に選択した変異・静的検査を実行し、受入像のSHA/CRC/実行rc/観測前後値/画像の所在をRESULTへ残す。新しい採取手順は `tools/accept/`、恒久的な診断の本文は増やさない。

DBGREF既定はKDEBUG=0、観測は対の `/boot/kdebug.bin` がRUNNINGかつpair CRC一致の像で行う (`TASK_DBGREF.md:6,260`)。基点ではgcapはcore、S3後はops経由になるため、着手時に所在を引き直す。S3後のOFF `v86 -g` はNOSYS・低位副作用0、ONは採取成功/中断を受け入れる。通常 `-t/-b/-d` と必須復元はOFFでも検証し、デバッグ部品不在を受入のSKIP成功にしない。

V86の4入口 `v86_smoke_test` / `v86_disk_test` / `v86_boot2` / `v86_gdc_capture` は各々、成功・準備失敗・#PF/#GP・STOPの到達可能な全出口と未到達段を表にし、共通RESTORING経由・caller出力・CR3/PTE/TLB復元を検証する。物理Ra266の `v86 -g` はNP21/Wの代替結果で閉じない。

性能記録は固定1000字corpusとfont hash、read_at回数/bytes、cache hit/miss、fallback字数/理由、計測分解能を添える。coldはopen時のcmap走査を含む初回表示まで、warmは同画面2回目。起動要求→初回presentと入力受理→対応presentを別に測り、最大/中央値を出す。P100/32MBの判定は容量別最終段、途中の64MB測定は退行比較に使う。

FD/MINIMAL起動はフォントfileを載せない構成としてKCGのcold/warm/fallbackだけを計測する。OpenType cold I/Oは管理下ext2上の原本fontをsourceとして測り、起動媒体・source媒体・FSを別々に記録する。FD起動後にHDD ext2をsourceにする追加ケースも「FD上のfont性能」とは呼ばない。FD FAT/HostDrvからの直接OpenType読込はこの受入に含めない。

> 2026-10-14 ユーザー決定: 搭載メモリ量ごとの確認は v3 の最終段にまとめる (DEFERRED_TESTS の関門「v3 の最終段」)。それまでのゲスト受入は 64MB の 1 構成。以下の構成一覧は最終段用として残す。

8MB planar/PEGC、17MB、Ra266、FDを区別する。coldはboot直後のfile/cache状態、warmは同じ画面2回目と定義し、機種/CPU/表示mode/字数/cache hit/miss/peak bytesを記録する。D15の速度目標を未測定推測から合格にしない。U7/U12/U14はT7a、U4/U5/U6/U15はT7bで証拠を対応づけ、容量別の残りは最終段で閉じる。P100/32MBで起動2秒以内・入力→描画100ms以内を判定し、64MB/Ra266/NP21/Wの参考値を代用しない。未達なら上限/方式をレビューするが目標は弱めない。

## 4. P3〜P10 へ渡す契約と着手ゲート

この表は後続との接続先で、全項目をT7後へ遅らせる順序表ではない。P7はV3_PLANどおりP1と同時。4世代の基盤はT2cで吸収済みだが、スロット整理は未実施でP7に残る (`TASK_T2D_T2H.md:49`、`V3_PLAN.md:63`)。T7後へ順序を移さない。以下は実装完了や独立設計票の承認を意味しない。既存票の決定はそちらを正典とし、未起票項目は列の成果物が揃うまで実装しない。

| 柱/項目 | 渡す契約 | 着手時に閉じる設計・受入 |
|---|---|---|
| P3 HAL W7/NIC L-C/音 | T5cのowner/IRQ/DMA/import境界。portable側へPC-98 portを漏らさない | [HAL_WIRING](TASK_HAL_WIRING.md) の残と各driver票を突合。1kHz tickは専用票を新設しclock別divisor、wrap、期限換算、ISR負荷、既存PCM/FM/V86/keyboardの影響を測る。測定前に周波数だけ変更しない |
| P4 resource broker | T1のverified resource、T2 lease、T5cの永久予約とmodule owner分離 | [DEVICE_RESERVATION](../settings/DEVICE_RESERVATION.md) の旧GUI初回予約条件をD33へ改訂。GUI入口でBDF/ID/revision/BAR decode/boot世代を再照合、不一致はenable拒否・予約差替えなし。第二span衝突、GUI=0、probe失敗、再enterを試験 |
| P5 82557/PCM/Trident/FD/KBD | DMA物理記述子、module lifetime、gfxのsurface/lease | 各既存票の実機段。Tridentのdecode幅実測を済ませるまで未検証BARを承認済みにしない。ドライバ設計はOpus/Codex突合。PCM合成器はまずIRQ整数演算 (D36) |
| P5 IDE DMA/cache/先読み | 永久device資源、DMA停止証明、VFS I/Oエラーとowner | 新票でPIO実測baseline、DMA mask/境界/descriptor所有、timeout→停止→fallback、dirty/writeback/flush順序を設計。cache所有はkernel、アプリ終了でdirtyを消さない。使い捨てext2で失敗/再起動後e2fsckと内容比較 |
| P6 実行モデル | T2のSTOP要求/通常回収、trim安全点、T4のengine active | 新票で自動番犬と明示STOPの条件を分離。KAPI中はAS切替なし、長いKAPIの期限/取消点、VFS/SQLite副作用の中断規則、x87をAS毎に保存復元。純loop/長いKAPI/WM再入/SQLite active/PCM/park-resumeを列挙して停止応答を測る。IRQからfreeしない |
| P7 ABI整理 | T2の形式/KAPI/layout/shlibの4識別とcompile-unit stamp (T2cで基盤吸収済み、現行値は `sdk/kapi.json` の `version` / `generations` を参照) | 未実施の一度限りのスロット整理について、世代正典と整理一覧を1回の変更セットに固定する。未知型のRust生成失敗、旧shell含む混在拒否、旧.oのlink拒否、NHD/HostDrv一組を検査。[ABI1]〜[ABI3] の通常append-onlyとD35の一度限り整理を混同しない |
| P8 GUI音・入力 | DMA ring/lease、T2通常文脈、gshell owner | 新票でproducer/consumer cursor、underrun/overrun、close/fault時停止、event所有/容量/drop、入力focusとWM再入を定める。microUIマウスキーも対象に含め、既存WMマウスキーと二重配送せずfocus/移動/クリック/リピートを試験。8bppを維持 |
| P9 Video HAL/SDL/移植 | SURFACE記述子とlease VA分離、能力bit、全画面専有 | 新票でformat/pitch/plane/clip/present/flip/mode-change revoke、SW fallback、私有面(c)の費用を定義。(b)を勝手に(c)へ変えない。PEGC直描きは全画面だけ。SDL shimはcapabilityで断り、固定VRAM番地を公開しない。[PORT_CANDIDATES](PORT_CANDIDATES.md) の低難度移植から着手しZSNESもv3候補として依存API/CPU/メモリ/音性能を個別票で測る。移植完了をこのHAL票だけで認定しない |
| U21 共有グリフ最適化 | T7b私有版のcold/warm/実peak、gshell所有・RO leaseのT2契約 | 私有80KiB×owner数との実節減を測ってから別票。補充GUI opの再入、cache evictionと借用寿命、世代/revoke、gshell終了・app faultを設計。測定で価値が出る前に共有化しない |
| P10 辞書meta/F3b | T4の単一heap予約と暫定重複禁止、T5a RESIDENT group | [TASK_DICT_META](../fep/TASK_DICT_META.md)でdict_id、S/M/L、学習別file、再実行冪等、途中失敗復旧。F3b導入後に暫定禁止を置換し、parked/親子/別名の競合試験を再利用 |
| control panel/i18n/GUI apps | 設定保存と実行中state適用を分離、FEP再起動要求、font API | [CONTROL_PANEL](../gui/TASK_CONTROL_PANEL.md)→[I18N](../gui/TASK_I18N.md)。設定保存失敗で表示を成功にしない。画像/音楽アプリ等は個別票と終了owner試験を起こす |
| network CD/HostDrv | 82557 L-B〜Eに加えL-F、既存Host Service、通常I/O期限 | LANFS段1 (`fs/sfs_lan.c:30-85`、`tools/tests/lanfs_tdd.md:25-37`) は吸収し、L-FのNIC/session結線・実機hsync受入へ渡す。`TASK_LAN_82557.md:8` の決定どおりL-CはL-Fに必要な分、lpr/wgetは後回し。接続断・短読・再試行冪等・cdinst契約・実機/NP21分離を測る。LAN /hostも製品font sourceにはしない。host virtual memory/EMSは混入しない |

## 5. 受入一式・データ移行・残る判断

システム更新は既存データを保持する媒体へ行う。settings.dbの新master上書きやfep.dbのdict_user消去を移行と呼ばない。DICT_META前後を二段に分け、ユーザー学習は件数・内容・二重加算なし・途中失敗後の再実行で照合する。v2へ戻す写しはバイナリと旧データの両方を残す。HDD再区画/再formatは不要。NHD変更は [D1]〜[D3] の範囲で別実行する。

外部apps/gameは現行引継ぎのビルド対象外という決定を守る。新SDK/layout/Unicode/lease変更に対するcaller追随の一覧を渡し、将来外部ビルド再開時に全再ビルドと混在拒否をゲートとする。外部未検証をv3同梱アプリの合格へ読み替えない。

後続の未起票項目は §4 の成果物を個別票で半日〜1日・受入10項以内へ分け、正常/故障注入/像増分を揃えてから実装する。HAL/NIC/DMA/音源の新しい装置操作は [HW3] のメーカー資料とBSD/MIT実装の版・ファイル・行を先に揃える。LAN票の旧Linux参照文 (`TASK_LAN_82557.md:40`) は新設計の根拠にせず、同票:51-52の資料とFreeBSD参照を使う。

独立レビューで確定する実装案は、T3 shell heap初期分割、T4 allocator hook/暫定DB identity、T5b early init依存、T7b parser/欧文font/作業域上限。これらは既決D契約の範囲で具体化するが、要件変更 (heap増量、必須FEP省略、高位kernel、後半scope追加など) が必要なら未決として別に判断する。設計書が存在するだけで測定や承認が済んだことにしない。


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

未測定ゲートはD15の速度、ext2 sourceのcold性能、54KiB作業域、shell 1MiB/私有2MiB予算、FD peak本数、Ra266の `v86 -g`、68h=0Bh時のA9hの値、page0全体の保存・復元へ変更した後のboot2/DOS回帰。これらは本レビューのApproveで合格に置き換えない。

## 7. 未決の争点

独立レビュー r1 の2件は下表のとおり決定済みで、本文へ反映した。parser/欧文選定、性能/像/FD/実機の未測定は引き続き各段のゲートであり、合格済みにはしない。

| 所見 / 論点 | 削減・緩和する側の言い分 | 維持する側の言い分 / 決着に必要な判断 |
|---|---|---|
| **決定 (ユーザー 2026-10-16)**: F5: shell wrapperのreturn PC検査 | **残す**。目的はなりすまし対策ではなく、偶発的な入口の誤用 (バグ) の検出。WM 経由で caller 判定が 0 になる経路 (`exec/exec.c:309-336,377-379`) でも shell 由来を確かめられる | 既承認 R1 と本文・b2 受入はそのまま。T7 の実装で検査の費用が目立てば、そのとき比較して削る |
| **決定 (ユーザー 2026-10-16)**: F7: x87 guardへの[HW3]適用 | CPU の命令だけの扱いは、**メーカーの資料 (Intel のマニュアル) を必須、BSD/MIT の実装は任意** (`docs/CONSTRAINTS.md` の [HW3] に 1 行足した) | §2-4 の根拠は Intel の資料の版・節を必須とし、BSD の実装は参照したときだけ書く |
