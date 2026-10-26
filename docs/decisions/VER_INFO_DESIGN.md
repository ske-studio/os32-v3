# VER_INFO — ver と About を実際の版と装置の状態から出す — 設計

> 状態: **現行 (2026-10-26)** — 確定 (astra 起草・Opus 2 往復・PM が条件 G1〜G3 を反映、版の表記はユーザー決定)。段の状態は票 TASK_VER_INFO が持つ。
> 出所: ~/os32-tmp/run/ver-info/DESIGN.final.md を 2026-10-26 に写した (往復の版と REVIEW はそこに残る)。本文の r2 の見出しと『r1→r2 の変更』は往復の記録として残す。

# VER_INFO 詳細設計 r2

## r1→r2 の変更
- F1: sqlite stack / services の副作用を明記し、保存値を共通 formatter の debug 行へ移す。
- F2: 新ユーザーランドは新カーネル専用と訂正し、app.conf 変更・到達しない版ガード・対応変異を削除。
- F3: test_mem_stat.py / kdebug_cli_host.c を CUI 段へ、ver_about_host.c を GUI 段へ移し、直列実装・一括取り込み。
- F4: スクロールを削除し、64 バイト格納・表示52桁以内（54桁以下）・窓高調整で全行を表示。
- F5: 公開配列を16行へ拡張し、count で有効行数を返す固定サイズ ABI とする。
- F6: kdebug_state_name に範囲検査を足し、formatter が既存17状態の名前を共用する。
- F7: PCM の取得元を常駐 pcm_module.c に訂正し、none は未検出または未読込とする。
- F8: rshell_peek / rshell_recover の抽出条件と既存試験を全体受入へ明記。
- F9: README.md / docs/INDEX.md の版更新を独立の小段に分離し、KAPI接続段と同時に取り込む。
- F10: 書式最大長を host で保証し、実行時 NOSPC により ver 全体を空にする仕様を削除。
- U1: ユーザー決定 `3.0-dev` を確定し、§8 の未決を閉じる。

## 1. 実物の確認
- 対象: os32-v3。原文「ver の表示が最新にならないので設計から見直し。ver 2.0 にでるので。これは CUI と GUI 両方です。」
- リポジトリは読むだけ。指定箇所の検索に基づく設計であり、実装・ビルド・ゲスト試験は未実施。
- 単一利用者・単一タスク・協調切替。取得のための再検出、待ち、装置操作、権限強化は加えない。
- 根拠の file:line は /home/hight/os32-v3 からの相対位置。9801 互換は対象外。

| 現在の行 | CUI の出所 | GUI の出所／実情 |
|---|---|---|
| 見出し／版 | include/config.h:16、userland/shell/cmd_base.c:100 | userland/rust/about/src/lib.rs:30 が config.h を include_bytes!、info.rs:113 で字句抽出 |
| CPU / PIC / PIT | cmd_base.c:102–104 の固定文字列 | info.rs:159–162 の写し |
| KBD / SER / SND / GFX | cmd_base.c:105–108 の固定文字列 | info.rs:163–166 の写し |
| API | cmd_base.c:109 の g_api->version | lib.rs:77、info.rs:185 |
| Build | cmd_base.c:117、kapi/kapi_sys.c:153 のカーネル日時 | lib.rs:80、info.rs:190 |
| Commit / Image CRC | cmd_base.c:119–126 の boot_image_info 成功時のみ | lib.rs:93、info.rs:211、同じ条件の別実装 |
| kdebug | cmd_base.c:129、同:66 の mem_stat | lib.rs:106 の ver_lines に渡しておらず、表示なし |
| sqlite stack high-water | sys_get_build_info の副作用、kernel/sqlite_engine.c:71–75 | GUI も同 API を呼ぶが、描画する行にはない |
| services | 同副作用、kernel/sqlite_engine.c:340–349 | GUI の取得中にコンソールへ出るだけで、About の行にはない |

| 票の説明に対する食い違い・補足（4 件） | 実物と設計への影響 |
|---|---|
| 1. §1 の「kdebug は正しく出ている」は GUI には成立しない | info.rs:39 は最大 12 行。共通化後は GUI にも追加 |
| 2. 二重保持は版の値そのものではない | lib.rs:30 は既に config.h を読む。二重なのは整形・装置文言・条件分岐 |
| 3. 「2.0」以外にも見出しの対象機が古い | cmd_base.c:100 / info.rs:155 は PC-9801。PC-9821 に変更 |
| 4. 既存情報口が「現在画面」を必ず返すとは限らない | gfx/gfx_core.c:759–763 は終了後に次の GUI モードを返す。CUI の現在画面と混同しない |

- tools/tests/test_about_info.py:296–306 は旧固定行を正解にしている。二者一致だけでは誤表示も通る。
- tools/tests/test_np21w_gui.py:883–890 は保存画像 gui_about.png の OCR 試験。v2.0 / API v72 は画像の事実で、現行版の試験ではない。
- docs/decisions/VERSIONS.md:9–11 の 2.1 は os32 の戻り先であり、ここを 2.1 と表示する修正はしない。

## 2. 版数の正典
| 候補 | 決定と理由 |
|---|---|
| include/config.h の SYS_VERSION | 採用。機械が読む唯一の製品版。確定値は `3.0-dev`（§8） |
| VERSIONS.md | 系列・リリース判断の説明。製品版文字列の抽出元にせず、SYS_VERSION を参照すると明記 |
| ビルド生成 | 製品版については不要。config.h への通常の依存でカーネルを再構築 |
| git タグ／describe | リリース照合には使えるが、浅い clone・タグなしで表示が変わるため値の供給元にしない |

- カーネルだけが SYS_VERSION を取り込み、新 KAPI の見出し行へ埋める。CUI・GUI は両方とも実行中カーネルの値を表示。
- About の include_bytes! と sys_version() を削除。ユーザーランド自身のビルド版を OS の版と取り違えない。
- ヘッダ依存を host 試験で確認し、SYS_VERSION の変更だけで kernel の表示が変わることを受入条件にする。
- Build / Commit / Image CRC は版と別の識別子として保持。ユーザーランド再配備だけでカーネル版は変わらない。
- build/kernel.mk:57–65 は .FORCE で tools/gen_build_id.py を実行し、内容変更時だけ build_id.c を更新。
- tools/gen_build_id.py:45–55 の短縮 commit・追跡ファイルの dirty・失敗時 unknown を維持。R1 規則（build/kernel.mk:283）も変更不要。
- ビルド日時は既存 kapi_sys.c の __DATE__ / __TIME__ を使う。新 formatter のコンパイル日時に置き換えない。

## 3. 装置の行の出所
| 行 | 出す値／取り方 | 取れないとき／意味の限界 |
|---|---|---|
| CPU | `x86 32-bit PM+Paging; calib=measured/default`。既存の cpu_calibrate_enough(cpu_calib_ticks, cpu_calib_rounds) と cpu_loops_per_tick()（kernel/cpu_calibrate.c:104–114） | 型番・メーカー・MHz は unknown 相当として表示しない。較正は周波数測定でも CPUID 判定でもない |
| PIC | `8259A x2; vectors=20h/28h (configured)`。kernel/idt.h:101–102 の設定定数 | 起動済みの設定説明。実チップを検出済みと主張しない |
| PIT | `8254 tick=100Hz input=1996800Hz`。pit_get_setup() の valid/hz/clk_hz（kernel/idt.c:228） | valid=0 なら `PIT: unknown`。PIT_HZ の表示だけで実設定を代用しない |
| KBD | `8251-compatible; IRQ1 (configured)`。既存 KBD IRQ 定義を使用 | ドライバ構成を表示。kbd_diag の IRQ 数ゼロを未搭載判定に使わず、I/O 読出しも不要 |
| SER | `8251 baud=9600 mode=compat FIFO=yes/no`。serial_get_status()（drivers/serial.c:457） | 戻り負なら `SER: not initialized`。FIFO 搭載と VFAST 動作中を分ける |
| SND | `OPN configured; PCM=CS4231-family/none`。FM 構成＋常駐 pcm_present()（kernel/pcm_module.c:61） | FM 実在は unknown。drivers/fm.c:54 の BUSY 成功はチップ同定ではない。PCM の none は未検出またはモジュール未読込 |
| GFX | `98/PEGC/Cirrus WxHxB bpp; active/GUI target`。下記 getter の保存値 | 無効な寸法・不明な backend は unknown。CUI 時の target を実描画モードと呼ばない |

- drivers/pcm_cs4231.c:451–466 の検出結果は、常駐 kernel/pcm_module.c:61–64 経由で取得する。module_exports 不在なら0であり、物理的未搭載とは断定しない。個別 CS4231/A 型番の getter は増やさない。
- kernel/snd_engine.c:640 の snd_init は再生状態の初期化であり、FM 搭載検出ではない。
- NIC は今回は行を増やさない。ne2k_state()（drivers/lgy98.c:169）と e82557_state()（drivers/e82557.h:33）は別の状態。
- NIC を単一の「有／無」にするには選択経路・モジュールの状態の集約が必要で、版表示修正の段を増やす価値が小さい。
- メモリの sys_get_mem_kb / mem_stat は既存の診断へ、FEP の辞書・起動状態も別の診断へ残す。追加表示は本票外。

| 渡し方 | 評価／決定 |
|---|---|
| 既存 KAPI を各 UI で合成 | 情報が不足し、順序と文言の二重実装が残るため不採用 |
| 全装置の数値構造体を新 KAPI で返す | 再利用向きだが C/Rust 共通 formatter の別ライブラリ化が必要。本票では不採用 |
| カーネルで一度整形した行を固定長構造体で返す | 採用。対象は ver/About 専用。文字列・順序・欠落条件の正典を一か所にする |

- 公開 ABI は sdk/include/os32/os32_ver_info.h（新規、shared.h から include）。C89 互換、ポインタや size_t は含めない。
```c
#define OS32_VER_LINES 16
#define OS32_VER_TEXT 64
#define OS32_VER_COLS 52 /* 440px - 左右PAD20px、8px字幅以内 */
/* kind低8bit: 0=head, 1=device, 2=identity, 3=debug; bit8=INDENT */
typedef struct { u32 kind; char text[OS32_VER_TEXT]; } OS32_VerLine;
typedef struct { u32 count; OS32_VerLine lines[OS32_VER_LINES]; } OS32_VerInfo;
/* sizeof(Line)=68, sizeof(Info)=1092; compile-time assert */
int __cdecl kapi_sys_ver_info(void *out, u32 size);
```
- sdk/kapi.json の api 末尾へ以下を追記（現在 v78、次版を仮に V=79、slot 254 とする）。
```json
{"name":"sys_ver_info", "ret":"int",
 "args":["void *out", "u32 size"],
 "out":[{"arg":"out", "len":"size"}],
 "target":"kapi_sys_ver_info"}
```
- externs にプロトタイプを追加。既存 slot と固定 data_fields を動かさず、shared.h の KAPI_VERSION も V にする。
- 並行変更が先に入れば V と末尾 slot を機械的に読み替える。OS 製品版と KAPI 版は別。
- size は正確に 1092 を要求。NULL／不一致は OS32_ERR_INVAL、書込みなし。成功 0、全体ゼロ初期化、NUL 終端。
- 既存生成ラッパの out 検査に従うだけで新しい権限モデルは導入しない。別サイズの将来 ABI は別エントリにする。
- count は実際の件数。装置 unknown は行を残す。boot_image_info 失敗時だけ Commit/CRC を従来どおり省く。
- formatter は上限付き追記 helper と u32 の10進/16進変換だけで作る。符号・NUL・残容量を先に扱い、カーネル文字列関数は kstrlen/kstrncpy 等を使う。
- 全書式の最大長を u32 最大・全17状態・unknown を含む host 試験で保証し、表示52桁以内、最大16行を固定。SYS_VERSION は20文字以内を静的検査。
- 実行時 NOSPC / count=0 の経路は作らない。上限付き helper の超過検出は host では失敗とし、実行時は当該行だけ `<label>: unknown` にする。Commit/CRC は最大長が既知の専用書式で必ず維持。
- 16行は通常15行＋kdebug詳細の継続1行。boot情報失敗時は2行減る。formatter に cold 属性を付ける。
- 部品境界（下の VerSnapshot はカーネル内部、公開 ABI ではない）:
```c
/* kernel/ver_info.h: private snapshot + prototypes */
typedef struct {
    u32 cpu_measured, pit_valid, pit_hz, pit_input;
    u32 ser_ready, ser_mode, ser_baud, ser_fifo, pcm_present;
    GFX_ScreenInfo gfx; u32 gfx_active, gfx_kind;
    u32 api_version; char build[32];
    int boot_ok, debug_ok; BootImageInfo boot; MemStat debug;
    ServicesVerState services; /* kernel/sqlite_engine.h の内部型 */
} VerSnapshot;
void ver_collect(VerSnapshot *s); /* kernel/ver_collect.c, bounded reads */
void ver_format(const VerSnapshot *s, OS32_VerInfo *out); /* cold, bounded */
/* include/gfx_hal.h / gfx/gfx_core.c: no probe, no mode switch */
int gfx_ver_state(GFX_ScreenInfo *out, u32 *kind, u32 *active);
```
- gfx_ver_state は gfx_core.c 内で g_backend / gfx_started にアクセス。query 成功時に backend 種別を返す。
- active=1 は現在寸法（gfx_current_height）、active=0 は選択済み GUI の目標寸法。既存 gfx_screen_info の契約は変えない。
- gfx_kind は内部 enum UNKNOWN=0/PC98=1/PEGC=2/CIRRUS=3。backend の文字列や weak シンボルの非搭載を UI に漏らさない。
- formatter は gfx_active に応じて `active` / `GUI target` を必ず付ける。同設定でも GUI 起動前後で語尾は変わり得る。
- ver_collect はゼロ初期化後、既存 getter の保存値だけを取得。PIC/KBD/FM の構成定数は formatter が既存ヘッダから読む。
- boot_image_info と kapi_mem_stat の既存取得関数を直接呼ぶ。公開 KAPI をカーネルから再入呼出ししない。
- Build 取得は kapi_sys.c 内の日時コピーを内部 helper に分離。旧 sys_get_build_info の report 副作用は互換維持、新経路は印字せず snapshot→formatter に集約。
- kernel/sqlite_engine.h に内部型 `ServicesVerState { u32 stack_used, stack_size, public; char state[24]; }` と `void services_ver_state(ServicesVerState *out);` を宣言する。
- kernel/sqlite_engine.c が high_water、SQLITE_ENGINE_STACK_SIZE、services_public、既存 services_state_name() をコピー。getter は出力・I/O・割当なし。collector が一回呼ぶ。
- formatter は `sqlite stack high-water=%u of %u` と `services: %s public=%u` を debug 行にする。詳細ページ数等は既存 services_report に残し、ver の要約には含めない。
- 実機手順 .claude/skills/os32-realhw-update/SKILL.md:40,57 の `services: SERVICES_READY` 判定を維持する。これは照合対象の読取りであり実機更新スキルの実行指示ではない。
- kapi_sys_ver_info の配置と処理順:
```c
/* kapi/kapi_sys.c; size/null check precedes this fragment */
VerSnapshot s;
ver_collect(&s);              /* does not collect Build */
build_time_copy(s.build, sizeof(s.build));
ver_format(&s, (OS32_VerInfo *)out);
return 0;
```
- IRQ 全体禁止で整形しない。協調切替中に yield せず保存値を読む。IRQ 更新数値は単一 u32 の読取りでよく、全装置同時刻の保証はしない。
- 再較正・FM レジスタ読書き・FIFO probe・ネットワーク通信・動的割当はゼロ。表示失敗でも UI から戻れる。

## 4. CUI と GUI の共通化
- 正規の行順: 見出し → CPU → PIC → PIT → KBD → SER → SND → GFX → API → Build → Commit → Image CRC → kdebug（必要時は継続1行）→ sqlite stack → services。
- kernel/ver_format.c だけが英文と順序を持つ。見出しは `PC-9821 OS32 v3.0-dev (Ring3 Native)`（版は SYS_VERSION）。
- kdebug は `off` / `on (pair %08x)` / `rejected %u %s` を先頭行にし、on は `kdebug pages: body=%u owner=%u`、rejected は `kdebug file pair: %08x` を続ける。取得失敗は `kdebug: unknown`。
- 状態名は formatter から kernel/module.c:28 の kdebug_state_name() を呼ぶ。同関数に state<0 / 配列上限以上→UNKNOWN を追加。13〜16 は RECORD_FULL/BAD_DEPENDENCY/BUSY/QUARANTINED になる。
- cmd_ver は KAPI を一回呼び、kind の低8bitに応じて既存色を選んで `kprintf(attr, "%s", text)` で出力。
- formatter は機器・識別・services の kind に INDENT を付ける。CUI はその bit に従って先頭2空白、各行末に改行を追加。見出し・kdebug・sqlite stack は字下げなし。構造体内に空白接頭辞・改行は含めない。
- shell_print_kdebug の詳細コマンドは維持するが、ver からは呼ばない。ver/About の情報源は新経路だけ。
- GUI は一回取得し、kind の低8bitで区切りを配置、INDENT は表示に使わない。同じ最大16行を折返しなしで表示する。
```rust
#[repr(C)]
#[derive(Clone, Copy)]
struct VerLine { kind: u32, text: [u8; 64] }
#[repr(C)]
struct VerInfo { count: u32, lines: [VerLine; 16] }
// size_of::<VerLine>() == 68; size_of::<VerInfo>() == 1092
// (api.sys_ver_info)(&mut report as *mut _ as *mut _, 1092)
// info.rs: count <= 16 / NUL position / kind -> display group only
```
- Rust 型は info.rs に置き、C サイズ・定数との一致を host 試験で検査。装置 enum の Rust 複製は不要。
- lib.rs の固定 VER_HW、12 行前提、版の字句解析、BootImageInfo の私有複製を除去する。
- スクロールは追加しない。表示幅52桁×8px=416px、左右PAD込み436pxで WIN_W_MAX=440 内。WIN_H_MAX を368へ広げ、最大16行＋既存区切り・ボタンが400/480画面に収まることを host で検証。
- 高さは既存レイアウト計算を使い画面内へ clamp。最長行、services の末尾、OK / Esc が両モードで見えて操作できることを PM が確認。
- sdk/os32x_hdr.py:264–271 の effective_min_api が生成済み OS32X_MIN_API まで要求版を上げる。新ユーザーランドは新カーネル専用で、旧カーネルは exec 時に拒否する。app.conf の変更と到達しない表示ガードは不要。
- 新 kernel 上の旧ユーザーランドは既存 ABI のまま動作する。実機更新手順どおり boot を先に更新・再起動し、次にユーザーランドを更新する。逆順に配備しない。

## 5. 大きさの見積もり
| 対象 | text+rodata 増分の概算 | 作業用 RAM の概算 |
|---|---|---|
| カーネル | +3〜6 KiB（収集・整形・getter・wrapper） | snapshot 約 0.4 KiB の stack。出力は呼出し元バッファへ直接 |
| シェル | -1〜+0.5 KiB（旧 ver 整形削除） | report 1.07 KiB、固定長。動的割当なし |
| About | -1〜+2 KiB（旧 formatter 削除、固定行描画へ縮小） | report 1.07 KiB＋既存描画行。行の全コピー増殖を避ける |
- 圧縮前の推定であり計測値ではない。kernel.map と各 ELF の size を変更前後で比較し、スタック予算も確認する。
- 増分が上限を超えたら書式ライブラリの取り込みや大きな配列複製を調べる。浮動小数点・汎用 printf の新規導入なし。

## 6. ファイルごとの変更一覧
| 所有段 | 新規／変更する本体ファイル（repo相対） | おおよその変更行数 |
|---|---|---|
| V1 | 変更 include/config.h、docs/decisions/VERSIONS.md | 1 / 8 |
| V2 | 変更 docs/KAPI_SPEC.md、README.md、docs/INDEX.md | 40 / 2 / 2 |
| V3 | 新規 sdk/include/os32/os32_ver_info.h、kernel/ver_info.h | 35 / 50 |
| V4 | 変更 kernel/module.c | 3 |
| V5 | 変更 kernel/sqlite_engine.h、kernel/sqlite_engine.c | 7 / 15 |
| V6 | 変更 include/gfx_hal.h、gfx/gfx_core.c | 8 / 35 |
| V7 | 新規 kernel/ver_collect.c | 100 |
| V8 | 新規 kernel/ver_format.c | 200〜240 |
| V9 | 変更 sdk/kapi.json、sdk/include/os32/os32_kapi_shared.h、kapi/kapi_sys.c、build/kernel.mk | 12 / 3 / 40 / 3 |
| V10 | 変更 userland/shell/cmd_base.c | 65 |
| V11 | 変更 userland/rust/about/src/info.rs、userland/rust/about/src/lib.rs | 180削除＋45 / 60 |
| V12 | 変更 tools/tests/test_np21w_gui.py（歴史画像の注記） | 6 |
- 公開行 kind・INDENT・配列上限は os32_ver_info.h、内部 gfx_kind は gfx_hal.h、装置設定値は既存ヘッダを正典とし、数値を実装へ直書きしない。
- V9 の生成物: sdk/include/os32/os32_kapi_generated.h、sdk/include/os32/os32_kapi_slots.h、kapi/kapi_generated.c、exec/exec_kapi_init.inc、sdk/rust/os32api/src/kapi_generated.rs。生成だけ、手編集禁止。
- 本体と試験の所有段を分離せず、次表を合わせて各段の全変更ファイルとする。表にない driver 本体・build/app.conf は変更しない。

## 7. 段の分け方
- 1段=codex 1回、2時間以内。本体4本以下。目安は小さい順、依存が満たされた段から着手し、手編集ファイルは段間で重ねない。
- 全段 sol、V8 の最大長保証が難航した場合だけ astra。今は実装エージェントを起動しない。
- 下表の本体欄は §6 の全パスを再掲。試験欄の省略名は全て tools/tests/ 配下。既存試験以外は新規。
| 段／見込み | 触る本体ファイル | 触る試験・照合先 | 依存／並列 | 機械語 | host受入／PMの変異1行 |
|---|---|---|---|---|---|
| V1 / 10分 | include/config.h、docs/decisions/VERSIONS.md | test_ver_version.py:単一定義・3.0-dev・再構築 | 決裁済、V2と並列可 | 文字列 | kernel依存再構築／版を9.9-testに |
| V2 / 15分 | docs/KAPI_SPEC.md、README.md、docs/INDEX.md | 変更なし、既存 tools/check_kapi_version.py の照合はV9と統合後 | V1、V3〜V8と並列可 | なし | 3文書の版一致／READMEの版をV-1に |
| V3 / 25分 | sdk/include/os32/os32_ver_info.h、kernel/ver_info.h | ver_layout_host.c、test_ver_layout.py:sizeof/offset/定数 | V1、V4〜V6と並列可 | なし | 68/1092とkind bit／textを63に |
| V4 / 30分 | kernel/module.c | test_ver_state_name.py:本物関数の抽出host実行 | 独立、V3/V5/V6と並列可 | あり | -1/0/12/13〜16/17/巨大値／上限を13に |
| V5 / 40分 | kernel/sqlite_engine.h、kernel/sqlite_engine.c | ver_services_host.c、test_ver_services.py:保存値→snapshot | 独立、V3/V4/V6と並列可 | あり | READY/隔離/未起動、無印字／publicを常に1に |
| V6 / 45分 | include/gfx_hal.h、gfx/gfx_core.c | test_ver_gfx.py:fake backendでgetter実行 | 独立、V3〜V5と並列可 | あり | active/target、3backend、失敗／activeを常に1に |
| V7 / 55分 | kernel/ver_collect.c | ver_collect_host.c、test_ver_collect.py:getter stub→snapshot | V3+V5+V6、V8と並列可 | 未リンク | PIT無効/SER未初期化/PCM未読込／ser_readyを1に |
| V8 / 70分 | kernel/ver_format.c | ver_format_host.c、test_ver_format.py:snapshot→全行の正解 | V3+V4+V5、V7と並列可 | 未リンク | 全状態・最大値52桁/16行・CRC／crc_validを反転 |
| V9 / 80分 | sdk/kapi.json、sdk/include/os32/os32_kapi_shared.h、kapi/kapi_sys.c、build/kernel.mk | test_ver_api.py:生成差分・wrapper・旧API副作用互換 | V2〜V8、V2と一括取り込み | あり | 新1slot、旧offset、clean→all／size判定を反転 |
| V10 / 90分 | userland/shell/cmd_base.c | test_ver_shell.py、既存test_mem_stat.py・kdebug_cli_host.c:cmd_verと贋API | V9、V11より先 | あり | CRC直後kdebug、services字下げ／INDENTを無視 |
| V11 / 100分 | userland/rust/about/src/info.rs、userland/rust/about/src/lib.rs | 既存test_about_info.py・ver_about_host.c:実formatter→両UI | V10、直列・V10と一括取り込み | あり | 16行/52桁/400高、全行一致／高さ上限を312へ戻す |
| V12 / 110分 | tools/tests/test_np21w_gui.py | test_ver_accept.py、build/checks.d/check-ver-info-host.mk、tools/check_map.d/check-ver-info-host.yaml（後二者はrepo相対） | V11後、並列不可 | なし | 全host・既存読取側・実効min_api／fixtureのCRC字下げを1空白に |
- V3 の内部ヘッダは V5 の ServicesVerState を参照するので統合コンパイルは V5 後。V3 単独では公開ABIのレイアウトのみ検証する。
- V10 は mem_stat の抽出host用 API に sys_ver_info の贋物を追加し、kdebug単独コマンドの試験も保持。旧ver整形の存在を前提に切り出さない。
- V10 完了時の旧 test_about_info 不一致は V11 への移行待ち。V11 完了後に二段の全対象を通して一回だけ取り込み、片方だけの合格・配備を宣言しない。
- V12 は V1〜V11 の新規試験を登録。途中は MUTATE=0 で直接実行。文書V2はAPI統合まで保留し、生成物と版が食い違う状態を main に取り込まない。
- V9以外はリンク可能な段で make kernel、対象host、check_select.py --lint。V9は make clean → make all。V12後に make all＋候補 MUTATE=0＋必要静的検査。
- 試験は同時2本以内。変異はレビュー後PMが一回、失敗検出の証拠を残す。目安にゲスト待ち時間は含めず、PMが下記を別途受入する。
- test_about_info.py の版パーサ・旧固定文言表は削除。唯一の書式正解は V8 に置き、V11は本物formatterの出力を両UIへ通して整合を検査する。
- 新たな字下げbitを含む kind のC/Rust一致、NUL、count上限、services が最後まで見えることを V11 で検証。
- test_np21w_gui.py:890 の旧画像の正解は変更せず「保存画像の版、現行版ではない」と注記。現行版は別受入。
- PMゲスト受入: NP21/W の98 16色とPEGC 480で CUI ver とGUI About の全文・services末尾・OK/Escを確認。スクロール不要で収まること。
- GFX は98の640x400x4bppとPEGCの640x480x8bppを区別し、CUIのGUI targetとGUIのactiveの意味を確認する。
- 同じsnapshotなら全行一致、実行状態が変われば対応行だけ変わる。CRC/Commit/Buildは同じkernelなら一致。kdebug 13〜16の名前は旧UNKNOWNから変わる。
- rshell実経路で `  Commit: `、`  Image CRC: `、8桁小文字16進、bytes、HDD/FD/? loader、noneの書式を保持する。
- tools/rshell_peek.py:76–78 の ^\s*Commit と8桁CRC、tools/rshell_recover.py:156 の先頭2空白とHDD/FD loaderを満たす出力を V12 のtest_ver_accept.pyから実際の読取関数へ渡す。
- 既存 tools/tests/test_rshell_peek.py・tools/tests/test_rshell_recover.py は変更せずV12の候補として実行。V12の変異は受理したはずの正常fixtureを壊し、同じ受入assertが落ちることを確認する。
- .claude/skills/os32-realhw-update/SKILL.md:40,57 の Commit/CRC/services=SERVICES_READY を同じfixtureから照合。sqlite stack行も欠落・二重印字しない。
- 新ユーザーランドをv78 kernelがexecで断ること、新kernelで旧ユーザーランドが動くことをhostヘッダ試験とPMゲストで確認。版ガード表示の試験は作らない。
- 配備はPMがNP21/W停止→boot先行→起動→旧ユーザーランドで確認→新ユーザーランド更新。失敗時は対になるkernel/userland一式へ戻す。実機操作・ini変更・上書きはここでは実行しない。

## 8. 未決
- **未決0件。** ユーザー決定（2026-10-26）により `SYS_VERSION "3.0-dev"`、表示 `v3.0-dev` で確定。
- PM決定 F1〜F10 を採用。services/sqlite stack は共通formatter、CUI→GUIは直列・一括取り込み、スクロールなし・16行、その他も上記のとおり。

## 反論
- なし（0件）。

## 確定 (2026-10-26、Opus 5.5 第 2 回の条件付き承認、PM が反映)

- ユーザー決定: 版の表記は **`3.0-dev`** (§8 は閉じる)。
- G1 (P2): **`OS32_VER_LINES` は 20** (構造体は約 1364B の固定、表示は count の行だけで窓は広げない)。16 では見出し 1 + 機器 7 + API・Build・Commit・CRC 4 + kdebug 2 + sqlite・services 2 で満杯になり、NIC などを足す余裕が無い。§3 の 16 は 20 と読む。
- G2 (P3): `kernel/ver_info.h` は V5 の ServicesVerState を参照するので、**作るのは V7** (V3 は公開ヘッダ sdk/include/os32/os32_ver_info.h だけ)。§6・§7 の V3 の担当から外し V7 に足す。
- G3 (P3): V12 の見込みは 40〜60 分。並べる順は V1 → V3〜V8 (並列可) → V9 + V2 (一緒に取り込む) → V10 → V11 (V10 と V11 は直列で取り込みは 1 回) → V12。

## V7 の実装で PM が決めたこと (2026-10-26)

- CPU の行の「実測か既定か」は値からは見分けない (正しい実測値が既定の 10000 と同じになりうる)。**`kernel/cpu_calibrate.c`・`.h` が、計測の結果を決めたときに「既定の値に落ちたか」(ticks=0 か、計算値が最小より小さくて CALIBRATE_FALLBACK_LPT を使った) を static に記録し、それを返す口 (例 `int cpu_calibrate_used_fallback(void)`) を足す**。V7 の触ってよいファイルに cpu_calibrate.c・.h を加える (この 2 つでは記録と口の追加だけ、計算の式は変えない)。表示は記録が 1 なら既定の値と分かる書き方、0 なら実測の値 (書式は formatter の V8 が決める)。

## V9 の実装で PM が決めたこと (2026-10-26)

- KernelAPI に `sys_ver_info` を足し公開の型 (OS32_VerInfo・OS32_VerLine) が増えると、HWDEV の契約の型の一覧 `tools/hwdev_layout.json` と合わずにビルドが止まる。**既存の手順 `tools/gen_hwdev_contract.py --seed-inventory` で一覧を作り直してよい** (V9 の触ってよいファイルに hwdev_layout.json を加える、手で直さない)。HWDEV の契約の値は変わるが、hwdev.mod はカーネルと対で配るので扱いは同じ。
- **V9 の実装で PM が決めたこと 2 (2026-10-26)**: `sdk/include/os32/os32_ver_info.h` の大きさの typedef の検査 (68・1364) は **`#if defined(__i386__)` の中に置く**。64 ビットの host の試験の多くが自前で `u32` を 8 バイトに定義して os32_kapi_shared.h を読むため、検査がそこで落ちる (43 本)。大きさと offset は 32 ビットで組む専用の host 試験 (V3 の ver_layout_host・test_ver_layout) と i386 の製品のビルドで確かめる。MemStat の試験の切り出しの範囲が新しい関数を含むようになった分 (4 本) は、V9 の中で試験の範囲を追従させる (検出力は減らさない)。
- **V10 の実装で PM が決めたこと (2026-10-26)**: V10 で shell が版数を持たなくなるので、V1 の試験 `tools/tests/test_ver_version.py` (と対応表) が版数の文字列を探す先を **shell の .o からカーネルの formatter (ver_format.o か kernel.elf)** に追従させる (V10 の触ってよいファイルに加える、検出力は減らさない — config.h だけを変えてカーネルが組み直され、表示が変わることを確かめる形を保つ)。check-tools-host の旧 About の試験は V11 で直る (V10 と V11 は 1 回で取り込む)。
