# LIBC_SHLIB_DESIGN — newlib (C ライブラリ) の共有ライブラリ化

> 出所: ~/os32-tmp/run/libc-shlib/DESIGN.final.md (astra 起草 r1→r2、Opus レビュー 2 回、N1〜N4 は PM が反映)。
> 状態: **現行 (2026-11-05)** — 実装は v3 の最後の P7 と組 (V3_PLAN の P7 の行)。

## r1→r2 の変更
- P1-RO: マクロ直読の公開RO区画を固定し、関数末尾追加後の公開番地不変試験・変異を追加。
- P1-data: state.cによるimpure.o置換、object単位の固定配置、未定義参照からの公開集合生成、__sfの扱いを具体化。
- P2-link: 共有アプリから-lc/NANO_LIBを外し、link_guardで静的メンバー混入を検査。
- P2-main: PM指定main (/home/hight/os32-v3) で差6と§8を再確認し、GUI・一般アプリの実測を記録。
- P2-time: _gettimeofdayはsys_time・tv_usec=0を維持し、時計ページはVA/aux予約に限定。
- P3-index: shlib.idxはD7の生成物のみを正とし、手書きの二重管理を廃止。
- P3-init: init/fini slot欄を予約欄へ置換し、entry[0]/[1]固定を明記。
- P3-a.out: 固定番地・ジャンプ表・成長余白の先例を1行追加。
- P3-FD／PM決定: MINIMAL全静的、libc/indexはNORMAL、fd.excludeとinstall case 9は変更不要。
状態: 設計案。2026-11-05。実装は v3 最後の P7 と同時、今は設計のみ。
前提: PC-9821、シングルタスク・協調切替、全アプリ再ビルド可。カーネルは libc の関数・内部状態を知らない。

## 1. 実物の確認
|実物・根拠|設計への意味／食い違い番号|
|---|---|
|`mm/shlib.c:13,25,37` は単一の大域状態と GUI の固定パス。`include/memmap.h:405–410` は1MiB、app=0x80100000|差1: 汎用複数枠は未実装。名前の変更だけでは libc を載せられない|
|`mm/shlib.c:93–110` は全 AS に GUI を付け、data を複製。`exec/exec_image_map.inc:5` はその全枚数を予算化|差2: 非依存 CUI にも付く。依存集合ごとの attach と予算へ変更|
|`mm/shlib.c:57` と `sdk/link/shlib.ld:42` は text 直後を data とする|差3: text 増加でデータ番地が変わる。公開データ固定番地 ABI には新配置が必要|
|`exec/exec_image_map.inc:76` は常駐 shell の依存を拒否。`docs/09_exec.md:17` は低位 shell を supervisor 共有|差4: shell.bin はそのまま共有 libc を呼べない。P7 は静的を継続|
|`build/config.mk:212–213`、`build/programs.mk:37–50,437` は CRT＋私有 nano allocator＋`-lc -lgcc`|差5: 「libc 全体が nano 版」とは確認できない。実物は private nano allocator と toolchain libc.a の組合せ|
|`sdk/crt/syscalls.c:130–135`、mainの `docs/decisions/time/OS32_TIME_DESIGN.md:63,154,193`|差6訂正: 時刻設計は存在。sys_timeはローカル暦epoch、tv_usec=0を維持、UTC/µsは新KAPI time_get。時計ページは本票では口だけ予約|
|`build/programs.mk:665` は「0x400000 常駐」、実定義は `include/memmap.h:405` の0x80000000|差7: 古いコメント。実装根拠は現定義とする|
|`sdk/include/os32/os32_kapi_shared.h:230–285`、`exec/os32x_hdr.c:8–18`|OS32X v4=60B、4世代照合、SHLIB表32B＋絶対関数番地。GUI版と汎用protocolを区別する|
|`userland/rust/libos32gui/src/shlib.rs:209,217`、`sdk/rust/os32api/src/gui/stub.rs:15`|GUI表は122関数、Rust stub と番号を照合。生成元を共通化して libc でも使う|
|`build/packages.yaml:39,113`、`userland/deploy.yaml:43–69,392–403`|FD=BOOT＋MINIMAL−除外。外部救援7本は install/cdinst/hsync/less/grep/hexdump/cfg、shell は core|
|`build/programs.mk:675–687`、`docs/decisions/network/NET_HOSTSVC_DESIGN.md:185–193`|GUI は libos32host を内包、現規則は末尾に `-lc -lgcc`。host_call は SDK の待機関数で libc と別の責務|
- r1の差6を訂正し、残る食い違いは6件。差6と§8はPM指定mainを再確認、他の実物確認はr1を継承。mainの既存ELFで§8を実測（再ビルド・git操作なし）。

## 2. Linux (glibc) との対応
|Linux の仕組み|OS32 への採用／不採用|
|---|---|
|動的リンカが依存 DSO を読み込む|汎用 kernel loader が枠の像を読む、SDK stub が呼ぶ。ELF動的再配置、名前検索、dlopen、遅延解決は導入しない。[glibc manual](https://sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker.html)|
|SONAME による依存、関数単位の symbol versioning|`(library_id, major)` を要求し、関数表は末尾追加、最低表長を起動前照合。挙動変更は別名・別slot、データ互換を壊すなら major を分ける。[glibc](https://www.sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker-Hardening.html)、[GNU ld](https://sourceware.org/binutils/docs/ld/VERSION.html)|
|明示依存と依存順初期化、複数 DSO 内の同じ静的ライブラリによる状態重複の回避|GUI→libc を宣言し、main／GUI init 前に libc init。GUI とアプリの malloc/FILE/errno を同一 libc に揃える。[glibc](https://www.sourceware.org/glibc/manual/latest/html_node/Dynamic-Linker-Hardening.html)|
|vDSO を libc が使い、kernel が位置を通知|kernel の RO 時計ページを CRT の初期化引数で libc に渡す。ELF vDSO や Linux のコードは採らない。[vdso(7)](https://man7.org/linux/man-pages/man7/vdso.7.html)|
|静的 sln は動的リンカ故障時にも使える|静的 shell と救援7本を維持。静的 BusyBox と同じ「直す道具が壊れた libc に依存しない」役割のみ採る。[sln(8)](https://www.man7.org/linux/man-pages/man8/sln.8.html)|
- 最も近い先例はLinux a.outの固定番地・ジャンプ表共有ライブラリ。番地登録簿とデータ余白を設け、公開配置を変えるならmajor更新という原則を採る。512KiB枠・固定区画の余白はこの制約と§8の予算から定める。[Linux GCC HOWTO](https://www.linuxdoc.org/HOWTO/GCC-HOWTO/x575.html)
- 時機: ABI切替は P7 の1回、依存照合は入口前、状態初期化は初回利用前、媒体切替は実行停止後。Linux の実装・構造を移植しない。

## 3. 帯と枠
- 推奨: shlib 帯を2MiBへ拡張、app は0x80200000へ移動。1MiBでも小さい像3本なら入り得るが、2版共存と固定dataの成長余白を保証しづらい。物理RAMを2MiB先取りしない。
|枠|予約VA（終端含まず）|用途|
|---|---|---|
|0|0x80000000–0x80080000|libc.1|
|1|0x80080000–0x80100000|libc.2（未提供時は未写像）|
|2|0x80100000–0x80180000|libos32gui.1|
|3|0x80180000–0x80200000|将来枠。末尾0x801FF000–0x80200000を時計ページとして別予約|
- 各通常枠512KiB、先頭16KiBが表、+0x4000–0x8000は公開RO固定区画、+0x8000からtext/private rodata、上限+0x70000。data固定先頭は+0x70000、上限64KiB。RO内部の予約余白はpayloadに含め、RO末尾とdata間の穴・未使用枠は写像・複製しない。
- APP_BAND は可変PDEの既存方式（`include/memmap.h:316–323`）を維持。image最大長、PT予算、最初のPDEの残量を新app先頭から再計算。heap=0x88000000、stack帯は維持。
- 定義元は memmap.h（帯）、公開shlib ABI（枠ID/offset）、config.h（配備パス）の3層。リンカ・Rust・Pythonへ生成し数値の二重管理をなくす。
```c
/* 新protocol。全フィールド u32、LE、自然4B配置、64B。C89対応。 */
typedef struct {
    u32 magic, protocol, library_id, major;
    u32 base, slot_bytes, nfunc, text_pages;
    u32 data_va, data_pages, data_init_bytes, data_abi;
    u32 flags, reserved[3]; /* init/finiはentry[0]/[1]固定 */
} ShlibHeader;
/* +0x40: u32 entry[nfunc]; 2..4080。+0x4000公開RO、+0x8000コード。 */
/* OS32X外包み → text_pages*4096 bytes → data_init_bytes bytes。
 * dataはdata_vaへ、残りdata_pagesはゼロ。RO終端からdata_vaまでの隙間はファイルに保存しない。 */
```
- `text_pages` は表を含む実ページ数。data_init_bytes≤data_pages*4096、text終端≤data_va、data終端≤枠終端、entryは+0x8000以降のコード領域内を検算。加算は差分検査でoverflowを防ぐ。
- `/sys/lib/shlib.idx` は版付きレコード `{id,major,base,slot_bytes,path}` の一覧（最大4通常枠）。D7がABI票＋配備選択から生成するだけで手編集しない。kernel はlibc名を解釈せず、枠重複と像ヘッダ一致を検査する。
- bootで登録像を一度読み、RO原本を保持。更新中の原本差替え・再ロードはしない。失敗した枠を非公開にし、救援起動を続ける。

## 4. アプリ側と公開データ
```c
typedef struct { u32 id, major, min_nfunc, data_abi; } ShlibNeed; /* 16B */
/* OS32X v5: v4の60B + u32 need_count + ShlibNeed need[4] = 128B。
 * 未使用needは0。共通protocolはライブラリmajorと独立。 */
typedef struct {
    u32 bytes; KernelAPI *api;
    u32 image_end, initial_brk_end, heap_limit;
    u32 time_page, time_abi;
} LibcStart; /* i386 28B。CRTがアプリの値を渡す。 */
int __cdecl os32_libc_init(const LibcStart *s); /* entry[0] */
void __cdecl os32_libc_fini(void);              /* entry[1] */
/* 生成stub例（i386、可変引数を含め呼出しframeを保つ）:
 * printf: jmp *LIBC_ENTRY_PRINTF_ADDR
 * LIBC_ENTRY_PRINTF_ADDR = LIBC_BASE + 0x40 + 4*SLOT_PRINTF */
```
- stub archive に関数ごとの小さいobjectと非ロード依存刻印を持たせる。包装器が実際に選択された刻印を合併し need[] を焼く。依存なしはcount=0、protocol=0。
- ライブラリ像も同じneed[]を持つ。execは推移閉包を解決し、major/data_abi完全一致・nfunc以上・循環なしを入口前に照合。各idは1 AS内1 major、異なるASは libc.1/2 を同時使用可。
- GUI.1 は libc.1 に依存。libc.2 のアプリが GUI.1 を必要とする矛盾は入口前に診断する。FILEやmalloc領域を版間に渡す暗黙変換はしない。
- attachは閉包だけ、textはRO共有、data/BSSはAS ownerの私有ページ。複製枚数は閉包の和、途中失敗時はそのASの写像とdataだけ巻き戻す。依存0の静的CUIにlibc/GUIは付けない。
```c
/* libc.1 の公開RO: R=0x80004000、libc.2はR=0x80084000。
 * R+0x000: _ctype_（現libc_a-ctype_.oのoffset=0、サイズもABI票に固定）。
 * 残りのmacro/inline直読ROも同区画へ追記、16KiB超過はmajor/枠再設計。 */
/* libc.1 の公開data。D=0x80070000、libc.2はD=0x800F0000。
 * linkerの絶対data symbolで解決。アプリ側copy relocationはない。 */
/* D+0x000: struct _reent *_impure_ptr; D+0x004: char **environ;
 * D+0x008: const struct _reent * const _global_impure_ptr;
 * D+0x100: struct _reent _impure_data（固定2KiB区画）、先頭がerrno。
 * D+0x104/108/10C: stdin/stdout/stderrのFILE *格納場所。
 * D+0x900..0x1000: その他公開dataの追記区画、D+0x1000以降: private data。 */
/* errno = *__errno() = _impure_ptr->_errno。
 * _impure_ptr は D+0x100 を指す。errnoのoffsetとFILE内offsetも版別ABI票に固定。
 * stdin/out/err はnewlibのmacroどおり上記ポインタを読む。
 * FILE実体はlibc所有。__sfの番地は固定不要（_reent->_stdin等経由）。FILE構造体の配置・配列strideはABI固定。 */
```
- 固定番地は「変数の格納場所」であり全アプリ共有の物理メモリではない。newlibの `FILE/_reent/mbstate_t/time_t`、compiler option・sizeof/offsetof を版別SDKに同梱し、変更時はmajor更新。
- 公開集合は全対象アプリ/GUIの.oの未定義記号とlibc.aのdata/BSS/RO定義の積から生成し、macro/inline参照も収集する。固定済み集合は削除せずABI票へ追記。未再開apps/gameは対象外と記録し、再開時に同検査を行う。
- このlibc.aは-fdata-sectionsなし。state.cが_impure_ptr・_impure_data・_global_impure_ptr・environを個別sectionで定義し、impure.oと元のenviron.oを引き込まない。__sinitは既存findfp.oの__sfを使い、stdio実体を二重化しない。--allow-multiple-definitionには頼らず、mapで元定義の不在を検査。
- その他はlibc_a-ctype_.oの.rodata、libc_a-getopt.oの.bss（optind）等をobject単位でKEEPし、ABI票指定の順序・固定先頭・予約幅で並べ、通常の*(.data*)等より先に回収する。object内の公開記号offsetもmapで検算し、同じmajorで動かさない。予約幅超過はリンクエラー。直接公開されたBSSも固定RW区画へ配置する。
- _reent≤2KiBと先頭4欄のoffsetをbuild時検査（toolchain `sys/reent.h:568–575`）。公開RWはD+0x1000までに収め、private dataとの重なりをASSERT。__sfが直接未定義参照集合に現れた場合は公開集合として再分類する。
- libc initはKAPIを私有dataへ保存→reent/stdio→heapを結線。CRTはその後アプリ `_init`・help処理・mainを実行。GUIのKAPI設定は従来のGUI initで行い、libcより後にする。
- 正常終了はatexit→stdio flush→KAPI exit。`_exit` とfaultはflushせずkernelがFD/ASを回収。mainのreturnもlibc exitへ。失敗した初期化はmainを呼ばない。

## 5. libc の中身と状態所有
|部品|範囲と所有者|
|---|---|
|newlib|現toolchainの `-lc` 相当（文字列、変換、stdio、stdlib、時刻等）＋現在のprivate nano allocator。libm、C++ runtime、dlopen/TLS/thread機構は今回追加しない|
|export|公開関数・newlibヘッダのmacro/inlineが参照する内部関数・dataを版別ABI票へ登録。ソース許可リストではない。未提供関数は通常の未定義参照でリンク時に分かる|
|syscall adapter|`_read/_write/_open/_close/_lseek/_stat/...` とOS32 errno変換をlibc側へ。KAPIだけを呼ぶ。静的救援も同じソースから別objectを作る|
|malloc|nano state、primary/secondary arena、trim/retry、large allocation台帳はlibcのAS私有dataに1組。heap実体は既存アプリのmem_map領域で、shlibのdata枠からは取らない|
|初期break|`sdk/allocator/nano_adapter.c:139` のリンク時 `_end` は共有libc自身の末尾になるため使えない。CRTからLibcStartのアプリ末尾と初期commit境界を渡し、以後libcがbreakを唯一管理|
|stdio/errno|FILE、buffer、atexit、locale、乱数、環境はASごと。公開reentと `_r` 関数を揃える。`_impure_ptr`をkernelが切り替える方式にはしない|
|再入|CR3切替で状態が切り替わる。協調yield前後の状態保持、既存nano busy/in_trim規則を維持。同じASのcallbackからallocator再入を許さない。thread用lockやTLSは追加しない|
|host_call|SDK/libos32hostに残す。共有アプリ・GUIからのlibc参照は同一libcへ、静的救援は静的libcへ。host待機がlibc initやmallocから逆向きに呼ばれる依存を作らない|
- `libc→KAPI`、`GUI→libc＋KAPI`。共有アプリとGUIのリンク行から `-lc` と `$(NANO_LIB)` を外しstubへ置換、CRTの静的syscalls/nanoも共有経路へ混ぜない。link_guardがmapのlibc.a/libos32nano.aメンバー不在を検査し、未提供関数は未定義参照で失敗させる。libc像と静的救援だけが実体をリンクする。新libc像に `-lgcc` を漫然と追加せず、必要helperは[L1]適合の既存実装へ解決する。
- 時計の結び目: `{time_page,time_abi}` と上記VAを予約し、P7では0/0。将来のkernel所有RO+USER頁の実体・更新は別の時刻実装へ委譲する。libc `_gettimeofday` はページ提供有無によらず `sys_time` とtv_usec=0を維持し、UTC/µsは新KAPI time_getを使う。
- `exec/exec_launch.inc:370` のapp用aux=0を、app stack上に複製した `{u32 bytes,time_page,time_abi;}` へのUSERポインタにする。shell用auxは既存ShellServiceのまま。shared_startがapp auxを読みLibcStartへ写す。時計未提供時は0/0で、この情報のためにkernelポインタを公開しない。
- 時刻の意味は `docs/decisions/time/OS32_TIME_DESIGN.md:63,154,193` に従う。ページpayload・整合読取は将来の別契約とし、未知版は利用しない。P7は置き場と引渡しだけで、newlibの時刻意味・精度を変更しない。

## 6. 起動と配備
- 順序: VFS→shlib.idxの登録/像検査→静的shell→アプリ要求の依存検査→AS作成/必要枠attach→CRT libc init→アプリ初期化。kernelはユーザーlibc initを呼ばない。
- 通常boot配布組はkernel・shell・libc.1・shlib.idx・世代manifest。GUI組は対応GUI像を加える。libc.2は要求アプリが存在する配布組だけへ加える。
- 欠損／major違い／表不足／data ABI違い／KAPI世代違いを区別し、要求id/majorと観測版を表示、アプリ入口前に `EXEC_ERR_INVALID`。静的shellは残り、hsync/install/cdinstで同一組を修復できる。
- 静的維持の確定対象: MINIMALの全コマンド（FDを含む）、shell.bin、gshell.bin（現常駐方式）、install.bin、cdinst.bin、hsync.bin、less.bin、grep.bin、hexdump.bin、cfg.bin。MINIMAL外の一般コマンドとCPL=3のsh.binから共有へ切り替える。
- shell.binで同じ共有像を使うには専用AS／master中の別data写像／resident morecoreの切替が要る。今の低位常駐と子アプリ実行中のcallbackを保つためP7では採らず、同じnewlibソースの静的版を使う。
- 更新は静的道具で非実行中の配布組を揃え、manifestを検証して再起動。旧原本は再起動まで存続。新majorを先に置き、対応アプリを次に置く。異世代混在を入口前に拒否し、旧組とFDを復旧路にする。
- PM決定: FD/MINIMALは全部静的、libc/indexはNORMALタグで通常配布組に含める。fd.exclude追加とinstall case 9の変更は不要。Minimal復旧環境から共有アプリを導入する際はCD/hsyncで通常boot組を補完する。

## 7. KAPI・ABI と P7
- `sdk/kapi.json` 正典からC/Rust/Python/asm/リンカ刻印を再生成。[ABI]の通常追記規則を維持し、P7の承認されたスロット整理だけを新ABI世代で一括切替。旧世代は互換実行しない。
- P7の同一公開点でKAPI版＋kapi_abi_generation＋memory_layout_generation＋OS32X format(v5)＋shlib_protocolを更新。具体的なKAPI番号は着手時の末尾を採用し先取りしない。
- libcの関数自体をKAPIへ足さない。起動時のLibcStartはCRT→libcの契約、時刻参照は共通entry情報の拡張。将来時計APIが要る場合だけKAPIに追記する。
- static/shared CRT、stub、GUI、一般コマンド、救援、SDKを同一世代で再生成・再リンク。newlibの内部配置票とABI世代刻印をlink_guardへ結び、混在を検出。
- P7統合時にPMが `make clean`→`make all`。apps/gameはH-7再開ゲートと承認後に作り直し、未再開なら除外を明記し「全アプリ済み」としない。

## 8. 大きさの見積もり（KiB、実測と計画を区別）
- 2026-11-05、main `/home/hight/os32-v3` の既存ELFを `i386-elf-size -A` とGUIの `nm` で測定。debug/世代刻印等の非ロードsectionは除外。成果物の再build・ソースとの一致検証は行っていない。
|対象|実測／計画値・算式|
|---|---|
|現GUI実測|表4096＋text151192＋rodata3924 B、整列後RO=0x27000=156 KiB（39頁）。data23088＋BSS17104=40192 B=0x9d00、複製は40 KiB（10頁）。build/programs.mk:678の8頁コメントは古い|
|現一般アプリ実測|userland/cmdsのwc.elf: text17573/data1760/BSS131656 B、sort.elf: 17557/1792/82536 B、hdate.elf: 21157/1856/16968 B。各RW量は133416/84328/18824 B（純量、ページ整列前）|
|libc.1像（計画）|text/private rodata160–320＋表16＋公開RO予約16＋初期data4–12=196–364。全exportのlink mapで確定。合計RO上限448KiBを超えればP7配置確定前に枠を見直す|
|libc per-AS（計画）|data+BSS 8–24（公開固定区画4を含む）。64KiB予約全体は複製しない。stdio buffer/heap追加は別計上|
|GUI per-AS（計画）|現GUI40＋libc8–24=48–64を暫定予算。GUI内libc重複除去による減少は新map実測まで差し引かない|
|原本RAM（計画）|libc192–352 RO＋8–24 template。libc.2未提供なら0。AS数×私有dataは別加算、RO末尾以降のVA穴は0物理頁|
|一般アプリの削減|共有版は未実装で実測不可。上記text全量をlibc分とみなさず、同一アプリの静的/共有版のbin・map差で算出。アプリ固有BSSは残る|
|HDD|純減=`Σ(旧bin−新bin)−libc像−index`。共有前後の比較未実測のため純減値は未確定。GUI重複除去も別測定|
|FD|全静的を維持し削減0。libc/indexはNORMALなのでFDに追加なし。196–364＋indexの同梱は最低空き64KiBを超えるため採らない|
- 生bin/配置済サイズ・2HDの実空き・圧縮媒体を別々に測る。静的archive全体サイズを削減量とみなさない。8MiB構成でpool残量・PT・AS data・初期heapの合計を受入する。

## 9. ファイルごとの変更一覧
各行=1ファイル=1実装担当の段。新規は「新」。同一ファイルの担当を分けない。生成物はその入力段の成果物。共通構造体だけの既存ヘッダ追記はPOLICY_DEV §2の例外。
|段|ファイル|機能・所有状態|
|---|---|---|
|A1|`sdk/shlib_abi.json`（新）|枠・関数slot・data配置・newlib build設定の正典|
|A2|`sdk/gen_shlib.py`（新）|stub/絶対RO・RW symbol/表/C・Rust定義、.o未定義参照とarchive定義から公開集合を生成|
|A3|`sdk/include/os32/os32_kapi_shared.h`|OS32X v5、共通構造体、KAPI版同期|
|A4|`sdk/kapi.json`|P7整理と4世代切替、既存生成器で再生成|
|A5|`include/memmap.h`|2MiB帯、各枠、clock頁、app先頭|
|A6|`include/config.h`|indexとlibc配備パス（実装時に既存config定義元へ合わせる）|
|B1|`mm/shlib.c`|単一GUI状態を汎用枠レジストリ・原本管理へ|
|B2|`mm/shlib_attach.c`（新）|依存集合の写像・複製・解除・巻戻し|
|B3|`mm/shlib.h`|枠ID/依存集合引数、枚数合計、read_page契約|
|B4|`exec/shlib_needs.c`（新）|依存閉包・版照合・診断、状態は起動計画に所有|
|B5|`exec/os32x_hdr.c`|v5の128B・依存件数・世代を検査|
|B6|`exec/exec_launch.inc`|入口前の依存計画、attachとCRT起動情報|
|B7|`exec/exec_image_map.inc`|必要data/PT予算・app帯境界・欠損診断|
|B8|`exec/exec_finish.inc`|ASの依存集合を使ってdetach|
|B9|`init/kernel_gui_shlib.inc`|GUI専用の失敗通知を枠別診断へ。kernel.cの既存shlib_init呼出し位置は維持|
|C1|`sdk/libc/state.c`（新）|impure.o代替の固定公開data、reent/stdio初期化と終了|
|C2|`sdk/crt/shared_start.c`（新）|LibcStartを組み、libc init前後の順を所有|
|C3|`sdk/crt/crt0_c.c`|static/shared入口の結線、return/exit経路|
|C4|`sdk/crt/syscalls.c`|静的/共有の同一syscall実装、KAPI依存のみ|
|C5|`sdk/allocator/nano_adapter.c`|共有版の明示heap handoff、nano状態唯一化|
|C6|`sdk/link/shlib.ld`|枠別リンク、固定公開RO/RW・object順序/予約幅ASSERT、穴を像から除くsection記号|
|C7|`sdk/os32x_hdr.py`|新外包みと刻印の共通包装|
|C8|`sdk/mkos32x.py`|実stub依存抽出、count/needの生成|
|C9|`tools/mkshlib.py`|新像のtext/data別payload、ヘッダ範囲検算|
|C10|`sdk/link_guard.py`|共有/静的link方式とlibc ABI票の照合、共有アプリ/GUIへのlibc.a/libos32nano.aメンバー混入拒否|
|C11|`userland/rust/libos32gui/src/shlib.rs`|新表・libc依存・init順への適合|
|C12|`sdk/rust/os32api/src/gui/stub.rs`|共通生成定義と枠別stubへの適合|
|D1|`build/libc-shlib.mk`（新）|libc像・stub・配置票のbuild規則|
|D2|`build/config.mk`|静的救援/共有一般のCRT選択、共有経路のNANO_LIB除去|
|D3|`build/programs.mk`|D1 include、共有アプリ/GUIの-lc・NANO_LIB除去、各リンク結線|
|D4|`build/kernel.mk`|新kernel機能ファイルの登録|
|D5|`userland/deploy.yaml`|通常boot組へlibc/indexをNORMALタグで追加|
|D6|`build/packages.yaml`（変更不要）|既存のBOOT＋MINIMAL選択でFDを維持、fd.exclude追加なし|
|D7|`sdk/libc/boot_index.py`（新）|ABI票＋配備選択からshlib.idx生成、一覧の重複手書きを避ける|
- app auxの構造体定義はA3、stackへの配置はB6、読出しはC2。clock頁の実体と更新処理は時刻設計の実装段へ渡し、未提供のP7は0/0を渡す。
- 検査は次表の各項目に `tools/tests/test_libc_shlib_<項目>.py` を新設し各1段、CHECK.inputsを宣言。guest fixtureも各1ファイル段。正典更新は `docs/09_exec.md`・`docs/KAPI_SPEC.md`・`sdk/README.md` を各1段。

## 10. 受入 → 試験と変異
H=host、N=NP21/W、R=PC-9821。変異は各試験の一意な1行anchor、通常成功／変異実行時REDを必須にする。
|項目・受入|試験／観測|1行変異|
|---|---|---|
|bundle: 欠損libcから復旧|N/R: FD→静的install/hsync→通常組→再起動→CUI成功、故意の欠損時はshell存続|救援1本へlibc依存を付ける|
|needs: 版と表不足は入口前拒否|H/N: libc.1/2の別AS、欠損/major/data_abi/nfunc違い、main到達マーカーなし|major一致判定を無効化|
|layout: 枠・表・payloadの境界|H: 切詰め/overflow/重複/表内entryを注入し原本未公開|data終端検査を外す|
|attach: 私有dataと共有text|H/N: 2ASでerrno/FILE/heap変更を交互確認、text frame同一、不要枠なし|data複製を原本写像へ変更|
|rollback: OOMで資源を戻す|H/N: 各割当点の失敗後owner頁数と原本frameを照合|失敗時data解放を飛ばす|
|crt: 初期化順と唯一heap|N/R: help/printf/scanf/malloc/realloc/trim、GUI callback、host_call yield、親子復帰|初期breakをlibc自身の_endへ変更|
|stdio: 終了意味を維持|N/R: stdout redirect、atexit順、main returnでflush、_exit/faultで未flush|正常returnのfiniを飛ばす|
|dataabi: macro/直接data参照|H/N: C89で標準ヘッダを使用、errnoと_errno_r、3stream、_impure_ptr、サイズ/offset検証|公開_impure_ptr番地を4Bずらす|
|stable: 関数追加で公開番地不変|H/N: ダミー関数を表末尾に追加して再link、全公開RO/RW記号と既存slot格納番地が不変。旧アプリのisalpha等を新像で実行。関数実装の行先は移動可|_ctype_を固定区画から可動rodataへ移す|
|link: 静的実体の混入なし|H: 共有アプリ/GUIのmapでlibc.a・libos32nano.aメンバー0、未提供関数fixtureはリンク失敗|共有リンク行に-lcを戻す（未提供関数が静的解決してRED）|
|clock: 従来時刻の維持|H/N: auxの0/0・未知版でsys_timeのローカルepochとtv_usec=0、UTC+540分・未設定時も時刻設計の互換試験へ接続|_gettimeofdayのtv_usecを1にする|
|capacity: 8MiB/媒体予算|H/N/R: link map、2HD最低空き64KiB、8MiB起動と一般CUI/GUIの頁数を記録|依存data予算を0にする|
- P7内の順: A契約/世代計画→B loader＋host試験→C libc/CRT＋guest退行→D配備の予行→P7統合世代切替/全再build→N→R。途中成果物は旧配布組へ混ぜない。
- 各実装段は指定のpreflight、触った像のbuild、check-changed MUTATE=0、追加/変更試験のmutate、lint。PMが最終clean/allと全体検査、NP21/W停止→NHD配備→再起動、実機操作は担当へ。
- 本r2は外部設計ファイルだけ。行数・r1差分を確認、既存ELFのサイズを測定。build・preflight（基点SHAなし、木は読取専用）・check・NP21/W・実機・r2独立レビューは未実施。P7着手前に別モデルの独立レビューを受ける。

## 11. 未決（ユーザー判断0件）
- FD/MINIMAL全静的・libc/indexのNORMAL配布はPM決定済み。
- 共有版サイズの実測、exportの完全性、具体的版番号・結線先確定は実装前の技術作業。

反論: 0件。

## 確定 (2026-11-05、Opus 第 2 回「確定してよい」、PM が反映)
- N1 (P2) §4: environ の定義は state.c だけに置き、sdk/crt/syscalls.c:140 の定義 (`char **environ = (char **)__env;`) は C4 で削除する。
- N2 (P3) §4: D+0x008 は予約 (0)。このツールチェーンの libc.a に `_global_impure_ptr` の定義・参照は無い (nm で 0 件)。
- N3 (P3) §4: state.c は `_REENT_INIT(_impure_data)` (sys/reent.h:653) で初期化し、stdin/stdout/stderr を `__sf[0..2]` に結ぶ (元の impure.o の再配置 4 件と同じ)。
- N4 (P3) §3: 表と公開 RO 区画の未使用の末尾ページは payload に含めず、写像しない (ヘッダの検算もこれに合わせる)。
- 実装は v3 の最後の P7 と組 (ユーザー 2026-11-05「aで」)。
