# TASK_T7_AND_FOLLOWUPS — 低位解放・OpenType と v3 後半への接続

> 状態: **設計中 (2026-10-03)** — 実装用の設計案。Opus 5.5 第3回の独立レビューでApprove。実装・実測・受入は未実施。
> 設計: Codex gpt-6-astra。調査基点 `3c4171a784cc47720392ab55b3c4a24aee17e6f3` (2026-10-03、main)。元の設計レビュー基点は `59c4285`。今回の更新は実装事実・参照の照合で、D番号・契約・順序とレビュー履歴は変更しない。
> 決定本文は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md)、柱/範囲/順序は [V3_PLAN](V3_PLAN.md)。前提は [T3](TASK_T3_LAYOUT.md) と [T4〜T6b](TASK_T4_T6_MODULES.md)。後半の既存票を置換せず、接続契約と未起票項目の着手ゲートを定める。

## 0. 基点で照合した実装と残件

| 実物 (file:line / シンボル) | 確認した境界 |
|---|---|
| `gfx/gfx_core.c:158` / `:178` (`gfx_kernel_framebuffer` / `gfx_bind_client`)、`:398` (`gfx_surface_source`) | e4 で kernel の CLIENT 参照は着地、USER 側は未結線。T7a は T2 完了後の lease caller を引き継ぐ |
| `kernel/v86_io.c:239` (`v86_cui_display_restore`)、`kernel/v86_gcap.c:571` (同呼出し)、`gfx/gfx_core.c:675` (`gfx_clear_planar_pages`) | CUI 定常への表示復帰と両ページ消去の修正は着地。§1-1 の KCG CODE 復帰/cache失効・低位専有化とは別 |
| `kernel/v86_mem.c:40` (`v86_mem_setup`)、`kernel/v86_bios.c:19` (`REAL_SNAPSHOT_SIZE`)、`:253` (`v86_bios_save_real`) | 636KiB backing と 0x600 snapshot は存続。page0 全体保存は未実装 |
| `gfx/backend_pegc.c:543` (`pegc_identify`)、`:450` (`pegc_boot_sync_record`)、`gfx/gfx_core.c:312` (`gfx_identify_candidates`) | §1-1 の BIOS 直読撤去の照合先 |
| `fs/vfs_fd.c:292` (`vfs_validate_sqlite`)、`exec/exec.c:299` / `:316` (`ring3_wm_enter/leave`) | 世代検査と WM 境界の既存入口。font read lease・内容 epoch は未実装 |

**T2 完了後に確定**: e5〜e12 の再init/revoke/再attach・200行の面配置・Unicode橋、f2〜f13/g の map・allocator・trim、h の V86 復元/STOP/構成別受入。現状と記録は [T2d〜h §2・§5・§7・§12](TASK_T2D_T2H.md)、未解決の文言差は [再整備草案 §4](DOCS_REORG_T3.md)。T2h の残件を T7 の未来の受入で消さない。

## 1. T7a 低位の所有権切替

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

coreの復帰状態、低位PTE、mailbox/boot dataを低位外へ保存 → **caller CR3を保存してmaster CR3へ一時切替** → V86用低位PTE/TLBを更新 → 実行、の順。master切替後はcaller VAを直接参照せず、path・出力・captureを全てkernel stagingで扱う。ファイルopen/loop deviceにもstaged pathを渡す。V86セッション中はcaller ASが存在しても実行せず、通常ASの地図検査を停止する。低位PTはmasterと各PDが共有しているため、そのUSER変更は全PDへ及ぶ。callerを停止するのはこのためで、通常map APIからはこの専用変更口を呼べない。ページ0の実物写像はPREPARINGからRESTORING完了までに限定する。

正常終了・#PF/#GP・STOP・準備途中失敗の全出口は、到達した段の印を持つ同じRESTORINGを通す。**実物IVT/BDA等をRW写像中に復元 → 通常低位PTE/SHM・trampolineのPDE権限を復元 → KCG復帰 → master側TLB反映・地図検査 → 保存したcaller CR3へ復帰 (TLB更新) → caller generation確認 → kernel stagingからchecked copyout → pin/呼出し中印を解除 → KAPI復帰**。V86が変えた低位データをkernel stateとして信用しない。STOPによるcaller終了要求は復元を飛ばさず、復元後のT2安全点で通常回収する。復元に必要なcode/stack/保存領域を低位へ置かず、復元検査失敗時はcallerへ戻らず停止する。

masterの検査は共有低位PTの復元検査でもあり、caller復帰時のCR3ロードでそのTLBも更新する。KCG復帰では既存定数 `MODE_FF1_PORT/MFF1_KCG_CODE` でコードアクセスモードを再設定し、ROM/CG RAM由来のキャッシュを全失効する (module未ロード時はcoreの復帰口でmodeだけ設定し、次のmodule initは空cache)。68h=0Bhを残した時のA9hの値は未確認であり、本設計の根拠を実測済みとはしない。

ページ0をNPにする前に、`pegc_identify`、`pegc_boot_sync_record` のBIOSフラグ、`gfx_identify_candidates` (`gfx_core.c`)、`v86_bios_save_real` の4経路を起動時の保存済みcore写しへ変更する。PEGCの実port probeは存続しBIOS値との比較だけ写しを使う。V86用原本はページ0全体 `[0, PAGE_SIZE)` (4096B) の上書きされないboot snapshot、セッション用編集は別stagingとする。現行 `REAL_SNAPSHOT_SIZE=0x600` をページ全体へ拡大し、0x600〜0xFFFも起動時の値を保持する。退避は低位consumerの書換え/NP化より前、復元も全ページが対象。追加の2560Bをcore BSS予算へ算入する。開始はmailboxを退避 → 実物低位640KiBをゼロ化 → 保存済みIVT/BDAを種にguest BIOS情報を構築 → guest image配置 → 実行の順。これにより前回guest/旧font/boot stackを再公開しない。終了時にmailbox原内容を復元する。ゼロ化の前に全boot stack/bootinfo consumerの退去を確認する。

`memory_boot_fixed` の旧FONT/UNICODE/BB/残余の低位行は、`[0, MEM_CONV_END)` の単一予約 **LEDGER_R_FIXED / LEDGER_OWNER_KERNEL / WB** (用途名V86_LOW、一般poolへ供給しない) に統合する。新しい台帳型やセッションownerへの移譲は不要で、V86状態が排他使用権を表す。BBのpool移動は別のSURFACE_BACKING記録にする。memmap定数・`tools/gen_memmap.py`・生成される `02_memory.md` とCLAUDEの帯要約を同じ差分で更新し、開始/終了でこの予約の型/owner/PFN数不変を検査する。

mailboxはDOSへ申告する640KiBの内側なのでV86中はguestが上書きしてよい。保存・実行・復元の**全セッション中**にhostが読書きしないことをツール側のセッション所有で保証する。`game/tools/autoplay/driver.py` の変更が必要なら外部引継ぎ事項として記録し、外部リポジトリの実装を完了したと扱わない。

### 1-2. OpenType前の中間段を動かす

T7a→T7bの間もGUIの日本語を失わない。**KCG ROMの字形取得を唯一の文字供給にして受け入れる**。CUIは引き続き内蔵フォント、GUIはKCG KAPI/facade経由で描画。KCG moduleのコード/キャッシュは低位の外、未収録字は固定の代替字形。`.kcgfont` の旧KAPIスロットはNOSYSで残し、旧ファイルを読み直すfallbackを作らない。フォントファイルのないFD/MINIMALでも英数入力・編集と文字表示を確認する。KCG moduleのロード/init失敗時はGUIへの切替を失敗させCUIを維持する (日本語を欠いたGUIを成功扱いしない)。T7a-1でcold/warm各1000字、ROM I/O数、cache hitと入力→描画時間をD15への参考値として記録する。coldで全角1字34 I/Oという現行実装の費用を隠さず、実測前に速度合格とはしない。

## 2. T7b OpenType ストリーミング

### 2-1. 実装範囲と読込 API

最初の対応は配布するIPA原本と選定した欧文フォントが使うTrueType `glyf` 輪郭。CFF、可変font、複雑なscript shapingの一般対応を「OpenType」という名で暗黙に約束しない。対応外formatはKCG/代替字形へ。欧文フォントの選定とライセンス確認は実装前ゲートであり、本票で新しい配布物を承認済みとしない。日本語fontは既存の同意付きfetch経路から配布物へ入れ、リポジトリへ追加しない。

共有Rust crate案 `userland/lib/font/` をgshell静的リンクとlibos32gui shlibの両方で利用する。API案は以下。ハンドルは各owner内のslotと再利用しない世代を持ち、満了時はslotをretireする。

- `font_open(source, limits) -> FaceHandle` / `font_close(face)` / `font_trim(face)`。
- `font_lookup(face, codepoint) -> GlyphToken {face_slot, face_generation, glyph_id}`。close/reopen後のtokenは描画・寸法取得ともSTALEで拒否する。
- `font_metrics(face, px) -> {ascent, descent, line_gap}`、`font_advance(token, px) -> advance`、`font_rasterize(token, px, target) -> bitmap_bounds`。寸法は符号付き26.6固定小数点、pxは16/32、座標/丸め/overflow規則を共通化する。bboxとadvanceを混同しない。

既存libos32guiのG2 (`text`/`measure_text` の半角8・全角16・高さ16) は維持する。GUI adapterはUnicode幅判定を共通にして固定cellへ配置し、筆位置はcell幅だけ進める。glyphはraster前のbboxで幅を測り、cellを超える欧文 (W/M/m等) は横方向をcell幅へ縮小して中央配置する。横縮小はadvanceを変えず、縦は共通baselineを維持する。cell clipは最終安全境界に限定し、縮小後の字形が欠けた結果を受け入れない。欧文の比例advanceはfont APIから取得できるが既存widgetへ直結しない。baselineはface metricsから同じ丸めで求め、KCG fallbackも同じcellへ置く。T7b-aで欧文fontの横縮小後の1x可読性・上下の欠けなし (W/M/m、アクセント、descenderを含む) を確認し、読めなければ等幅候補の再選定をゲートとする。T7b-aでG2維持の承認とcaller一覧 (text/measure_text、edit/caret、clip、FEP候補窓) を照合し、T7b-bで画像と測定を一致させる。比例幅GUIへ変更したい場合はG2改訂の別判断が必要。

sourceは `read_at(offset, dst, len)`、file_size、**owner・FD generation・内容epoch**を持つ。seek/readの短読は成功扱いせず、他ownerのFDを借りない。gshellのopen/reopenはshell自身の通常文脈だけで行い、`vfs_fd_set_protect` を設定する。GUI op中は設定変更要求だけ記録し、そのopからopenしない。描画時の遅延openもしない。shell shutdownでは明示closeする。アプリのfont FDはそのownerでopenして通常終了回収に任せる。

現在 `VfsSqliteLease` だけにあるfd+generationの検証を、SQLite cookieを流用せずfont用read leaseとして追加する設計。kernel側がowner/lifetime/in_use/generation/staleをread_at/closeの各処理内で検証する。seek状態を公開せずread_at内部で位置決めと読込を一体で行う。ユーザー側の事前checkだけにしない。権限は以下の**別入口**で固定し、ownerを呼出引数で選ばせない。

| 入口 | 権限の根拠 / 検証 |
|---|---|
| app用public KAPI `font_source_open/read_at/close` | int80で保存されたcallerのCPL=3・slot/generationを根拠に、`ring3_call_from_user()!=0` かつWM外を要求。lease表はcaller owner専用で、ownerは `res_owner_get()` とsaved callerの一致も照合する。shell leaseはこの名前空間に存在せず、shell token/生FD/他app tokenはINVALまたはSTALEで拒否。KAPI直呼びのshell/WMをこの口で特別許可しない |
| shell内部 `shell_font_source_open/close` | loaderが検証した現行gshell画像世代・shell textの直呼びで、`ring3_in_syscall==0`・WM外・`res_owner_get()==GUI_SHELL_OWNER` を全て要求。登録済みshell sourceのみ生成/破棄する。app引数からleaseを登録しない |
| shell内部 `shell_font_source_read_at/validate` | 同じgshell世代のshell text直呼びかつ `ring3_call_from_user()==0`。shell専用registryで照合し、期待ownerは固定の `GUI_SHELL_OWNER`。WM中は `res_owner_get()` がappでもよいが、app registryへはアクセスしない。通常shell文脈か、登録済みgshellのhandler/pump/owner_exit呼出区間であることも確認する |

shell内部入口はKAPI/trampoline/ユーザーimport表に載せず、resident shell loaderが検証済みgshellだけへ渡すsupervisor専用サービス表で供給する。入口の薄いcdecl wrapperは直接のreturn PCがloader由来の現行shell executable text範囲内かを検査してから共通VFS実装へ進む (callerが申告する番地は使わない、尾呼出しで検査を省略しない)。サービス表とregistryはUSERなし。shell登録世代が終わればサービス参照とleaseを失効させ、旧gshellからの利用を拒否する。これによりappがshell番号を知ってもKAPI経由では到達しない。resident限定入口/サービス表の実装と混在拒否をT7b-bの変更対象に含める。

`ring3_call_from_user()` は `ring3_guard_active(in_syscall, wm_depth)` であり、WM中に0になることを実コードで確認した。この値やdepth単独をshell権限の証拠にせず、上記の非公開入口・shell text/世代・専用registryと組み合わせる。WM呼出区間の印はkernelがhandler/pump/owner_exitの前後で設定/解除し、通常returnに加えてpark/STOP/faultのlongjmpとdispatch入口でも消す。font借用中は§2-4のguardでpark/STOP配送を止める。試験はappからshell lease/生FD指定を拒否、WM中の正当なshell read成功、WM終了直後のappから拒否、close/reload後の旧世代拒否、例外/park経路で権限の印が残らないことを含める。

ttf-parserのprivate `glyf`内部へ当然にアクセスできるとはしない。T7b-aで現在のcrate公開APIを調査し、字形単位の安全な薄層が実現できなければ、必要なテーブル/単純輪郭readerを明示的に実装するか、ライセンスを保った限定vendor化をレビューする。`Face::parse`へ全ファイルを保持する試作を製品経路へ戻さない。採用方法・依存版・変更範囲はこのゲートで記録する。

### 2-2. 常駐表とメモリ上限

上位票の「表≈100KiB」は廃止したサブセットの見込みで、IPA原本のcmap/loca/hmtx合計だけで約336KiBになる。**全表常駐を前提にせず、表ディレクトリと選択subtableの索引だけ常駐させる設計案**とする。

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

FD generationは番号の再利用を検出する値で、ファイル内容の世代ではない。font read leaseはmount世代+inode identity+内容epochをkernel内に保持する。ext2の置換rename/unlink/umountによる既存staleに加え、同inodeへのwrite/truncateの**変更前**に関連font leaseを失効し内容epochを進める。各read_at内部と全入力取得後/cache commit前、cache-hit描画前にも検証する。更新中のfaceを継続せずsafe pointで再openして全索引を検証し直す。hsync置換・同サイズ上書き・truncateを別々に試験する。外部変更を観測できないHostDrvなどinode/変更通知のないsourceは最初の製品font sourceとして拒否しKCGへ落とす (事前に管理下ext2へ配備する)。mtime/sizeやヘッダだけの比較を内容一致の保証にしない。このVFS追加はT7b-bの前提であり、現行generic FDで実装済みとはしない。

採用ラスタライザがf32を使う場合、P6全体を待たず **T7b-aにx87隔離guardを先行実装する**。gshellのfont描画/寸法の最外周でcallerのx87全状態 (register stack、status/tag、CW) を専用の低位外領域へ保存、例外をmaskした既知CWで初期化し、成功/失敗とも復元する。CWだけの保存では不足。guard中のpark/アプリ切替/STOP処理/再入を禁止し、要求は終了後の安全点で処理する。IRQでFPを使わない。アプリ私有側も同じguardで呼出元のFP計算を壊さない。x87非搭載CPUは検出してこの経路を実行せずKCGへ落とす。386対応は最終命令列検査を行う。x87保存領域は4KiBの管理予算に、追加codeは画像増分へ算入する。

parser/rasterは境界検査済み入力と上限付き領域だけを使い、allocation failureはResult、unchecked index/unwrap/panicを製品経路に残さない。host fuzz/破損font・全上限±1を通し、panic不在はテストだけで証明せず全呼出経路のレビュー条件とする。gshell側の予期しないfault/panicはfont active印でapp-killより先に検出しfail-stopする (WM faultを無関係なアプリだけの失敗へ畳まない)。STOPは正常leave後に配送する。T7b-bでアプリがCW/rounding/例外maskを変更しx87 stackに値を残した状態からWM描画し、状態の完全復帰とfault帰属を検査する。

## 3. T7 の実装分割と受入

| 段 | 主な対象 | 閉じる条件 |
|---|---|---|
| T7a-1 BB/font撤去 | `gfx/gfx_core.c`, planar backend、`drivers/kcg.c`, `kernel/boot_font.c`, build/deploy | planar池確保、KCGのみでGUI/CUI、module欠落時CUI維持、cold/warm1000字の参考計測、旧低位参照と旧font読込ゼロ |
| T7a-2 V86 | `kernel/v86*.c`, `kernel/paging.c`, bootinfo、autoplay境界 | CPL=3のv86 -t/-b/-g成功とcaller CR3/出力復元、親AS付きCUI入れ子/他AS/parked/GUI入場拒否、不正path/captureはPTE不変、backing削除前後の占有差、正常/異常/STOP復元、GUI→CUI→V86→GUIを20回、mailbox保存、低位単一FIXED/kernel予約とowner不変、0x8C000/前回データのゼロ化、68h=0Bh注入後KCG再描画、Ra266でv86 -g回帰、boot2/DOSで0x600〜0xFFFを含むpage0全体の初期値/復元と起動回帰 |
| T7b-a prototype | 新font crate、`userland/rust/font_test`, font fetch/asset metadata | 公開API可否・G2維持、原本/欧文fontの最大量と上限超過字、shell/shlib/2MB予算とFD peak、ext2 sourceのcold性能、FD起動時KCG性能、x87 guardを記録 |
| T7b-b 製品接続 | gshell、libos32gui、font cache、deploy | 1x日本語/欧文の可読性・W/M/m等の欠けなしスクリーンショット、cold/warm各1000字・入力→描画、trim→再描画、壊れたfont/短読/上限超過fallback、設定変更→パネル終了→別app open→再描画、旧token/FD再利用/hsync/上書き拒否、appからshell lease拒否・WMから成功・WM退出/世代交代後の権限失効、x87状態保存 |

V86の4入口 `v86_smoke_test` / `v86_disk_test` / `v86_boot2` / `v86_gdc_capture` は各々、成功・準備失敗・#PF/#GP・STOPの到達可能な全出口と未到達段を表にし、共通RESTORING経由・caller出力・CR3/PTE/TLB復元を検証する。物理Ra266の `v86 -g` はNP21/Wの代替結果で閉じない。

FD/MINIMAL起動はフォントfileを載せない構成としてKCGのcold/warm/fallbackだけを計測する。OpenType cold I/Oは管理下ext2上の原本fontをsourceとして測り、起動媒体・source媒体・FSを別々に記録する。FD起動後にHDD ext2をsourceにする追加ケースも「FD上のfont性能」とは呼ばない。FD FAT/HostDrvからの直接OpenType読込はこの受入に含めない。

8MB planar/PEGC、17MB、Ra266、FDを区別する。coldはboot直後のfile/cache状態、warmは同じ画面2回目と定義し、機種/CPU/表示mode/字数/cache hit/miss/peak bytesを記録する。D15の速度目標を未測定推測から合格にしない。U4/U5/U6/U7/U12/U14/U15をそれぞれ閉じ、未達ならどの上限/方式を改訂するかをレビューする。

## 4. P3〜P10 へ渡す契約と着手ゲート

この表は後続との接続先で、全項目をT7後へ遅らせる順序表ではない。P7はV3_PLANどおりP1と同時、D35の整理はT2c/T3で先行する。以下は実装完了や独立設計票の承認を意味しない。既存票の決定はそちらを正典とし、未起票項目は列の成果物が揃うまで実装しない。

| 柱/項目 | 渡す契約 | 着手時に閉じる設計・受入 |
|---|---|---|
| P3 HAL W7/NIC L-C/音 | T5cのowner/IRQ/DMA/import境界。portable側へPC-98 portを漏らさない | [HAL_WIRING](TASK_HAL_WIRING.md) の残と各driver票を突合。1kHz tickは専用票を新設しclock別divisor、wrap、期限換算、ISR負荷、既存PCM/FM/V86/keyboardの影響を測る。測定前に周波数だけ変更しない |
| P4 resource broker | T1のverified resource、T2 lease、T5cの永久予約とmodule owner分離 | [DEVICE_RESERVATION](../settings/DEVICE_RESERVATION.md) の旧GUI初回予約条件をD33へ改訂。GUI入口でBDF/ID/revision/BAR decode/boot世代を再照合、不一致はenable拒否・予約差替えなし。第二span衝突、GUI=0、probe失敗、再enterを試験 |
| P5 82557/PCM/Trident/FD/KBD | DMA物理記述子、module lifetime、gfxのsurface/lease | 各既存票の実機段。Tridentのdecode幅実測を済ませるまで未検証BARを承認済みにしない。ドライバ設計はOpus/Codex突合。PCM合成器はまずIRQ整数演算 (D36) |
| P5 IDE DMA/cache/先読み | 永久device資源、DMA停止証明、VFS I/Oエラーとowner | 新票でPIO実測baseline、DMA mask/境界/descriptor所有、timeout→停止→fallback、dirty/writeback/flush順序を設計。cache所有はkernel、アプリ終了でdirtyを消さない。使い捨てext2で失敗/再起動後e2fsckと内容比較 |
| P6 実行モデル | T2のSTOP要求/通常回収、trim安全点、T4のengine active | 新票で自動番犬と明示STOPの条件を分離。KAPI中はAS切替なし、長いKAPIの期限/取消点、VFS/SQLite副作用の中断規則、x87をAS毎に保存復元。純loop/長いKAPI/WM再入/SQLite active/PCM/park-resumeを列挙して停止応答を測る。IRQからfreeしない |
| P7 ABI整理 | T2の形式/KAPI/layout/shlibの4識別とcompile-unit stamp | 世代正典と整理一覧を1回の変更セットに固定。未知型のRust生成失敗、旧shell含む混在拒否、旧.oのlink拒否、NHD/HostDrv一組を検査。[ABI1]〜[ABI3] の通常append-onlyとD35の一度限り整理を混同しない |
| P8 GUI音・入力 | DMA ring/lease、T2通常文脈、gshell owner | 新票でproducer/consumer cursor、underrun/overrun、close/fault時停止、event所有/容量/drop、入力focusとWM再入を定める。microUIマウスキーも対象に含め、既存WMマウスキーと二重配送せずfocus/移動/クリック/リピートを試験。8bppを維持 |
| P9 Video HAL/SDL/移植 | SURFACE記述子とlease VA分離、能力bit、全画面専有 | 新票でformat/pitch/plane/clip/present/flip/mode-change revoke、SW fallback、私有面(c)の費用を定義。(b)を勝手に(c)へ変えない。PEGC直描きは全画面だけ。SDL shimはcapabilityで断り、固定VRAM番地を公開しない。[PORT_CANDIDATES](PORT_CANDIDATES.md) の低難度移植から着手しZSNESもv3候補として依存API/CPU/メモリ/音性能を個別票で測る。移植完了をこのHAL票だけで認定しない |
| U21 共有グリフ最適化 | T7b私有版のcold/warm/実peak、gshell所有・RO leaseのT2契約 | 私有80KiB×owner数との実節減を測ってから別票。補充GUI opの再入、cache evictionと借用寿命、世代/revoke、gshell終了・app faultを設計。測定で価値が出る前に共有化しない |
| P10 辞書meta/F3b | T4の単一heap予約と暫定重複禁止、T5a RESIDENT group | [TASK_DICT_META](../fep/TASK_DICT_META.md)でdict_id、S/M/L、学習別file、再実行冪等、途中失敗復旧。F3b導入後に暫定禁止を置換し、parked/親子/別名の競合試験を再利用 |
| control panel/i18n/GUI apps | 設定保存と実行中state適用を分離、FEP再起動要求、font API | [CONTROL_PANEL](../gui/TASK_CONTROL_PANEL.md)→[I18N](../gui/TASK_I18N.md)。設定保存失敗で表示を成功にしない。画像/音楽アプリ等は個別票と終了owner試験を起こす |
| network CD/HostDrv | 82557 L-B〜E、既存Host Service、通常I/O期限 | 接続断・短読・再試行の冪等性、cdinstの既存契約、実機/NP21専用経路の分離。host virtual memory/EMS等の保留構想を混入しない |

## 5. 受入一式・データ移行・残る判断

システム更新は既存データを保持する媒体へ行う。settings.dbの新master上書きやfep.dbのdict_user消去を移行と呼ばない。DICT_META前後を二段に分け、ユーザー学習は件数・内容・二重加算なし・途中失敗後の再実行で照合する。v2へ戻す写しはバイナリと旧データの両方を残す。HDD再区画/再formatは不要。NHD変更は [D1]〜[D3] の範囲で別実行する。

外部apps/gameは現行引継ぎのビルド対象外という決定を守る。新SDK/layout/Unicode/lease変更に対するcaller追随の一覧を渡し、将来外部ビルド再開時に全再ビルドと混在拒否をゲートとする。外部未検証をv3同梱アプリの合格へ読み替えない。

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

第2回はB1〜B4/N1〜N14をclosedとし、新規R1のみblocker。以下を反映したが第3回確認と実装・実測は未実施。

| 所見 | 採否と理由 |
|---|---|
| R1 (P2) | 採用。app KAPIと非公開shell専用入口/registryを分離。WMのownerがappのままでもshell readは固定shell ownerを検証し、shell text/登録世代を要求。`ring3_call_from_user()==0`やdepthだけで許可する案は採らない。return/longjmp後の権限消去と偽token拒否を受入へ追加 |
| R2 (P3) | 採用。現行0x600のsnapshotをpage0全体へ拡大する設計を選択。追加BSSを計上しboot2/DOS回帰へ0x600〜0xFFFを明記 |
| R3 (P3) | 採用。欧文幅超過は横縮小・中央配置、筆位置はG2維持。1xの可読性/字形の欠けを判定し不合格ならfont再選定 |
| R4 (P3) | 採用。FD/MINIMALはKCGのみ、OpenTypeのcold I/Oは管理下ext2 sourceに限定。起動媒体とfont source媒体を別記 |


### 第3回・最終設計レビュー (2026-10-01)

Opus 5.5 (`claude-opus-5-5`) は **Approve**。第2回R1〜R4は全てclosed、残るblockerなし。文書設計の承認であり、実装・実機の受入完了ではない。WM呼出区間の印は既存 `ring3_wm_enter/leave` とlongjmp/dispatchでの解除機構の再利用を優先し、T7b-bの実装レビューで確認する。

未測定ゲートはD15の速度、ext2 sourceのcold性能、54KiB作業域、shell 1MiB/私有2MiB予算、FD peak本数、Ra266の `v86 -g`、68h=0Bh時のA9hの値、page0全体の保存・復元へ変更した後のboot2/DOS回帰。これらは本レビューのApproveで合格に置き換えない。
