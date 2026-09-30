## 第5部 デバイスドライバ

### §5-1 キーボード (kbd.c / kbd.h)

PC-98キーボードコントローラ制御。IRQ1割り込みハンドラで入力を取得。

| 項目 | 仕様 |
|------|------|
| I/Oポート | 0x41 (データ), 0x43 (コマンド) |
| 割り込み | IRQ1 (INT 0x21) |
| バッファ | u16リングバッファ (256エントリ) |
| バッファ形式 | 上位8bit=スキャンコード, 下位8bit=ASCII |

**API**:

| 関数 | 説明 |
|------|------|
| `kbd_init()` | キーボード初期化、IRQ1有効化 |
| `kbd_getchar()` | ASCII文字を1文字返す (ブロッキング) |
| `kbd_getkey()` | スキャンコード+ASCII (u16) を返す |
| `kbd_has_key()` | バッファにデータがあるか |
| `kbd_get_modifiers()` | 修飾状態 (`SHIFT_*`: SHIFT 01 / CAPS 02 / カナ 04 / GRPH 08 / CTRL 10) |

**修飾キー**: SHIFT・CTRL・GRPH は make で ON、break で OFF。**カナ・CAPS は機械式ロック**で、ロックすると make、
外すと break が来るので同じく make で ON、break で OFF (方式 B。2026-09-26 までは make で反転して break を捨てていた
ため、外しても ON のままだった)。根拠は BIOS ワークエリア 0000:053Ah (INT 09h が make でセット・break でリセット) と
NP21/W の観測 — 経緯と未確認点 (実機) は [TASK_KBD_NAV](tasks/gui/TASK_KBD_NAV.md) §2。カナ・CAPS は V86 セッション中も
追う。起動時は `kbd_init` が 053Ah のカナ・CAPS ビットを引き継ぐ (起動行 `[kbd] ... lock=XX`)。

**特殊キー (kbd_getkey()のスキャンコード)**:

| キー | scan | ASCII |
|------|------|-------|
| ↑ | 0x3A | 0x00 |
| ↓ | 0x3D | 0x00 |
| ← | 0x3B | 0x00 |
| → | 0x3C | 0x00 |
| Home | 0x3E | 0x00 |
| Del | 0x39 | 0x00 |
| Tab | - | 0x09 |
| ESC | - | 0x1B |
| BS | - | 0x08 |
| Enter | - | 0x0D |

### §5-2 フロッピーディスク (fdc.c / fdc.h)

BIOS (INT 1Bh) を使用せず、I/Oポート直接制御および DMA (μPD8237A ch2) 転送による完全なハードウェア制御を実装している。

| 項目 | 仕様 |
|------|------|
| I/Oポート | 0x90 (メインステータス `FDC_MSR`), 0x92 (データ `FDC_FIFO`), 0x94 (コントロール `FDC_CTRL`) |
| 転送方式 | DMA (μPD8237A ch2)。アドレス 0x09 / カウント 0x0B / バンク 0x23 / マスク 0x15 / モード 0x17 / F-F クリア 0x19 (`drivers/fdc.h`) |
| 割り込み | IRQ11 (`fdc_irq_fired`) を待つ。上限は**機構の最悪値**から導く: SEEK/RECALIBRATE 1.5s (SRT 8ms × 80 トラック)、READ/WRITE 1s (最大 2 回転 + ヘッドロード)、リセット 0.5s (`drivers/fdc.h`)。以前の 200ms 一本値は NP21/W 基準で実機のシークに足りなかった (POLICY_DEBUG §4-51) |
| 割り込みの回収 | SEEK/RECALIBRATE の前に SENSE INTERRUPT STATUS で未回収分を排水 (上限 4、ST0=80h で尽きる)。pending 無しの SIS は **ST0 1 バイトだけ**返るので PCN を読まない。タイムアウト後も SIS を 1 回出し、SE が立っていれば取りこぼしとして完了扱い。RECALIBRATE の EC は再試行の合図 (2 回まで)。判定は `drivers/fdc_decide.c` (純粋関数、ホスト試験 `make check-fdc-seek-host`) |
| リトライ | READ/WRITE は 3 回。失敗した試行の後に `fdc_recover()` (リセット → Specify → 排水 → recalibrate)。最終失敗だけ `[fdc] read fail ... st0/st1/st2` を 1 行出す ([V4]) |
| DMAバッファ | 1 セクタ分 (1024B) を **1024B 境界に揃えた**静的バッファ — 64KB 境界をまたげない ([HW2])。`fdc_init()` が起動時に検査する |

**API**:

| 関数 | 説明 |
|------|------|
| `fdc_init()` | FDCコントローラの初期化 |
| `fdc_read_sector(drv, cyl, head, sect, buf)` | CHS指定で1セクタ読込 |
| `fdc_write_sector(drv, cyl, head, sect, buf)` | CHS指定で1セクタ書込 |
| `disk_read_lba(lba,count,buf)` | LBA指定でセクタ読込 (ユーティリティ) |
| `disk_write_lba(lba,count,buf)` | LBA指定でセクタ書込 (ユーティリティ) |

### §5-3 FM音源 (fm.c / fm.h)

YM2203 (OPN) FM音源チップ制御。FM 3ch + SSG 3ch。

| 項目 | 仕様 |
|------|------|
| アドレスポート | 0x0188 |
| データポート | 0x018A |
| FM チャンネル | 3ch (CH1-3) |
| SSG チャンネル | 3ch (A-C) |
| SSG基準クロック | 3,993,600 Hz |

**API**:

| 関数 | 説明 |
|------|------|
| `opn_init()` | OPN初期化 |
| `fm_note_on(ch, note)` | FM発音 |
| `fm_note_off(ch)` | FM消音 |
| `fm_startup_sound()` | 起動ジングル再生 |
| `fm_play_mml(mml)` | 簡易MML文字列再生 |
| `ssg_beep()` | BEEP音 |
| `ssg_tone(ch, period)` | SSGトーン設定 |

### §5-4 RS-232C (serial.c / serial.h)

μPD8251A (USART) 内蔵シリアルポート制御。

| 項目 | 仕様 |
|------|------|
| データポート | 0x30 |
| コマンドポート | 0x32 |
| 信号ポート | 0x33 |
| 割り込みマスク | 0x35 |
| ボーレートタイマ | PIT カウンタ#2 (0x75) |
| 受信バッファ | 256バイト リングバッファ |
| IRQ | IRQ4 |

**API**:

| 関数 | 説明 |
|------|------|
| `serial_init(baud)` | 初期化 (38400bps等) |
| `serial_putchar(c)` | 1文字送信 (TxRDY待ち+hltスリープ) |
| `serial_puts(str)` | 文字列送信 (16Bごとにフロー制御) |
| `serial_getchar()` | 1文字受信 (ブロッキング) |
| `serial_trygetchar()` | 1文字受信 (ノンブロッキング) |

**フロー制御**: `serial_puts`は16バイトごとに`io_wait`を挿入し、NP21/Wのパイプバッファ溢れを防止する。

### §5-5 グラフィック (gfx/ HAL / libos32gfx)

CPU直接描画＋バックバッファ方式。カーネル層 (`gfx/`) はバックバッファ管理とVRAM転送を担当し、高レベルな描画機能は外部プログラム用ライブラリ `libos32gfx` (`userland/lib/gfx/`) に分離されている。

**HAL バックエンド表 (GUI v1.1、2026-09-06 現行)**: `include/gfx_hal.h` の `GfxBackend`
(probe / init / query / present_rect / set_palette / fill_rect / blit / enter / leave / shutdown、
`bb_base` / `bb_size` / `bb_pitch` / `bb_format`) を 3 枚持ち、`gfx_core.c` が **probe 順
Cirrus → PEGC → 9801** で最初に通ったものを使う。`/etc/system.cfg` の `GFX=pc98|pegc|cirrus|auto`
(`gfxmode` コマンドが書く) で強制できる。能力は `gfx_screen_info()` (契約 G5) で問い合わせ、
GUI もアプリも 400 ライン / 16 色 / プレーンを決め打ちしない。

| バックエンド | 画面 | バックバッファ (CPU が描く面) | 表示面へ | 能力ビット |
|---|---|---|---|---|
| `backend_pc98.c` (9801) | 640×400×16 (4 プレーン) | 主記憶 0x6A000 (128KB、`MEM_GFX_BB_BASE`) | CPU 転送 + ページフリップ | TEXT_OVERLAY, PAGE_FLIP |
| `backend_pegc.c` (9821 PEGC) | 640×480×256 (PACKED8) | 主記憶末尾から 300KB (`sys_reserve_top`) | CPU 転送 (F00000h リニア窓、09A8h・GDC クロック・SYNC・PITCH・SCROLL を明示して 480 ライン) | TEXT_OVERLAY (合成ありと実測) |
| `backend_cirrus.c` (CL-GD5430、Xe10 内蔵) | 640×480×256 | カード VRAM のクライアント面 (リニア窓 01000000h + 04B000h、300KB) | エンジン BLT (`present_rect` = 非表示面 → 表示面)。塗り / 転送も HW、256 画素以下は CPU 直書き | HW_FILL, HW_BLT (映像はリレーで切替、TEXT_OVERLAY 無し) |

デバイス窓は master PD に **supervisor + PCD** で張り、CPL=3 に見せるのはクライアント面だけ
(表示面へ直接描かせない = 契約 G4。写像の規則は [02 §2-1](02_memory.md))。counters
(`gfx_stats`: present_bytes / hw_ops / io_accesses / commits) で「CPU が運んだ量」と
「エンジンに任せた回数」を測る (NP21/W では時間が測れないため)。設計と経緯は
[tasks/gui/DESIGN.md §5〜§8](tasks/gui/DESIGN.md)、Cirrus の資料は `include/wab_xe10.h` と
`drivers/wab_*`。NP21/W では Cirrus は ini の `USEGD5430=true` / `GD5430TYPE=91` (Xe10) を要する ([D2])。

**PEGC 480 ラインの SYNC を実機の ROM から採る — `v86 -g`** (KAPI v68 `v86_gdc_capture`、
`kernel/v86_gcap.c`、票 [TASK_PEGC480_REALHW](tasks/realhw/TASK_PEGC480_REALHW.md) §3 段 1):
`include/pegc.h` の 480 ラインの SYNC は NP21/W の `bios18.c` 由来で、uPD7220 の SYNC は読み戻せない。
そこで V86 の監視部で**実機の ROM の INT 18h** を呼ぶ — AH=31h で今のモードを読み、その値の bit の並び
(NP21/W の bit2 / Bible 3-2 の bit3。その並びとして正しい値になるのがちょうど 1 つのときだけ決め、決まらなければ 30h を呼ばない。
AL bit0 = ラスタスキャンはどちらの並びでも許して 30h へ保つ — 実機 Ra266 は AL=0Dh を返す)
から AH=30h の 640x480 の引数を決めて呼び、同じ AH=30h で元のモードへ戻す (480 を断られたら無変化なので戻しは呼ばない。
戻しが 05h で返らなければ `pegc_restore_text_sync` が AH=31h で読んだ周波数の 400 ラインへ — 24kHz 固定にはしない。
戻すのは同期 (09A8h・SYNC・6Ah・GDC STOP) と、PEGC の probe が通った機種では起動時に記録した GDC クロックと
それに合う PITCH だけで、テキスト CRTC とテキスト GDC の PITCH / CSRFORM は戻さない)。その間、
09A8h・09A0h・60h〜7Ah の偶数・A0h〜A6h の偶数を捕まえて**記録してから実機へ幅どおりに**通し、OUT を畳まずに
512 件まで積む (溢れ・INS/OUTS・66h 付きの IN/OUT EAX・AH≠05h は失敗で、列は出さない)。通常の V86 で素通しの
ポート (GRCG 7Ch・7Eh、EGC 4A0h〜、5Fh、パレット、FM) は捕まえないので記録に出ない。ROM の 1 呼び出しは IF を立てたまま
入る (`v86_gcap_keep_if`) ので 3 秒の見切りと脱出ホットキーが効く (ROM が自分で CLI して回るときだけは #GP の番犬頼み)。通常の V86 の開始・終了
(画面を 200 ライン / 400 ライン・16 色へ決め打ち) は通らず、最後に CUI を作り直す。出力は画面と rshell の
両方に出る 1 件 1 行の形。`v86 -g -t` は決まった I/O 列の試験ゲストで記録器を確かめる (実機へ通さない)。
NP21/W の ROM は SYNC を内部状態へ直接書くので、エミュレータでは SYNC の列は採れない (件数は構成次第)。
純粋部 (記録・判定) のホスト試験は `make check-v86-gcap-host`。

**PEGC の 480 ラインへの出入りは起動時の BIOS 状態に頼らない** (2026-09-29〜、票
[TASK_PEGC480_REALHW](tasks/realhw/TASK_PEGC480_REALHW.md) §2 H2・H3・H5):
`pegc_apply_timing` が 1 か所で 09A8h → 6Ah GDC クロック (83h・85h = 5MHz) → 両 GDC の SYNC → グラフィック GDC の
PITCH (80) → SCROLL → 表示開始の順に書く (クロックは SYNC の前 — Bible 3-2 表3-2)。戻りは起動時に 09A0h
(sel 09h の bit0 = CLOCK-1、読み値 bit1 = CLOCK-2) で読んだクロックへ戻し、PITCH (書き込み専用)・グラフィック GDC の SYNC・
SCROLL の IM もそのクロックとの組で選ぶ (両方 5MHz なら PITCH 80・C/R 4Eh・IM 1、それ以外は 40・26h・0 — Bible 2-6 表2-27、2-7)。値は `include/pegc.h` §10 に集めてあり、**実機の `v86 -g` の記録で差し替える箇所はそこだけ**
(今は NP21/W 由来)。GDC へは 1 バイトずつステータスの FIFO を見てから書く (コマンドの前は EMPTY、パラメータの前は FULL でない。
上限 2µs × 5000 回で打ち切って数える — `pegc_gdc_fifo_timeouts`、kernel.map から読める)。09A8h へは bit1,0 だけを書く
(実機の読みは 81h だが bit7〜2 は「常に 0」、PLL を持つ機種がある)。ホスト試験は `make check-pegc-mode-host`。

| 項目 | 仕様 (9801 バックエンド) |
|------|------|
| 解像度 | 640×400 (`gfx_init`) / 640×200 (`gfx_init_200`、縦は HW が 2 倍表示) |
| 色数 | 16色 (4プレーン) |
| ページフリップ | 両モードで自動有効 (ポート A4h = 表示ページ / A6h = アクセスページ) |
| 描画方式 | システムRAMバックバッファ → VRAM一括転送 |
| プレーンサイズ | 32,000バイト (80×400) |
| パレットI/O | 0xA8 (idx), 0xAA (G), 0xAC (R), 0xAE (B)。PEGC / Cirrus はバックエンドの `set_palette` (256 色、下位 16 はシステム色) |

**ページフリッピング** (DEVELOPMENT.md から 2026-09-05 に移動):
PC-9801 の VRAM は各プレーンに物理 2 バンク (64KB) あり、A4h (表示) / A6h (アクセス) で
切り替えられる。非表示ページへ BB から転送してから切り替える (約 3µs) ので、ティアリングが
無く、表示中ページと CPU の書き込み先が異なるためバス競合も起きない。VSYNC 待ちループ
(最大 17ms) は不要。前フレームのダーティ矩形を次フレームにマージする 2 フレーム追跡で
ステイルページを防ぐ。`gfx_present_raster()` はフリップ転送後に VSYNC 同期でパレット
だけ書き換えるのでフリップと併用できる。外部プログラムへの影響は無い (BB ポインタ不変)。
ハードウェア制約 ([HW1] EGC/GRCG/GDC 描画禁止) の理由は [POLICY_DEV.md §3](POLICY_DEV.md)。

**カーネル層 (gfx/) 主要API**:

| 関数 | 説明 |
|------|------|
| `gfx_init()` | 640x400x16初期化 + バックバッファ確保 |
| `gfx_shutdown()` | テキストモード復帰 |
| `gfx_present()` | バックバッファ全面→VRAM転送 |
| `gfx_present_rect(x,y,w,h)` | 矩形領域のみVRAM転送 |
| `gfx_present_dirty()` | ダーティ矩形のみVRAM転送 (KernelAPI経由) |
| `gfx_add_dirty_rect(x,y,w,h)` | ダーティ矩形の登録 (KernelAPI経由) |
| `gfx_get_framebuffer(fb)` | バックバッファ情報取得 (KernelAPI経由)。パックド系は `planes[0]` だけ、`planes[1..3]=NULL` |
| `gfx_screen_info(si)` / `gfx_hw_fill_rect` / `gfx_hw_blit` / `gfx_stats` / `gfx_lease_palette` | HAL の問い合わせ / HW 塗り・転送 (無いバックエンドは `OS32_ERR_NOSYS`、`gfx_init` 前も NOSYS) / カウンタ / パレットリース (KernelAPI v40〜v42) |
| `gfx_hardware_scroll(lines)` | GDCハードウェアスクロール |
| `gfx_clear(color)` | 画面クリア (カーネル内部用) |
| `gfx_fill_rect(x,y,w,h,c)` | 矩形塗りつぶし (カーネル内部用) |
| `gfx_draw_sprite(x,y,spr)` | スプライト描画 (カーネル内部用) |

**libos32gfx (外部プログラム用ライブラリ) 主要機能**:

| モジュール | 説明 |
|------------|------|
| `libos32gfx_core.c` | `libos32gfx_init` (gfx_init + attach) / `libos32gfx_attach` (gshell 配下のアプリと shlib: framebuffer 取り直し + **画素形式 `gfx_packed` の判定** + プール初期化。判定をここに集約しないと PACKED8 で漢字が描けない、2026-09-06) |
| `gfx_draw.c` | 描画プリミティブ (pixel, hline, vline, line, rect, fill_rect)。4 プレーンと PACKED8 の両経路 (`gfx_packed`) |
| `gfx_surface.c` | サーフェス管理 (create, free, clear, pixel, fill_rect) |
| `gfx_sprite.c` | スプライト管理 (create, free, draw, 背景退避/復帰) |
| `gfx_blt.c` | ブリット (矩形の退避/復帰) |
| `gfx_font.c` | KCGフォントレンダリング (ANK/漢字/UTF-8文字列) |
| `gfx_kcg.c` | KCGフォント読み出し (ANK/漢字) |
| `gfx_circle.c` | 円/楕円/円弧/太線円の描画 |
| `gfx_bezier.c` | ベジェ曲線 (2次/3次/太線、de Casteljau整数演算) |
| `gfx_math.c` | 整数sin/cos 互換ラッパー (実体は libos32math/trig.c) |
| `gfx_raster.c` | ラスタパレット管理 (clear, add, present) |
| `gfx_dump.c` | VRAM＋パレットのBMPダンプ出力 |
| `gfx_util.asm` | NASM高速ユーティリティ (memcpy/memset最適化等) |
| `lconsole.c` | グラフィックモード用論理コンソール |

### §5-6 ATAPI CD-ROM (atapi.c / atapi.h)

IDEセカンダリバンクに接続されたATAPI CD-ROMデバイスをPIOモードで制御する。SCSIコマンド (CDB) をPACKETコマンド経由で発行し、セクタ読み出しを行う。

| 項目 | 仕様 |
|------|------|
| I/Oポート | タスクファイル 0x640-0x64E、代替ステータス/デバイス制御 0x74C。バンク切替は 0x430/0x432 (`IDE_BANK0`/`IDE_BANK1`) で、ここはポートではなくプライマリ/セカンダリの選択に使う (`drivers/ide.h`) |
| コマンドプロトコル | ATA PACKET (0xA0) + SCSI CDB |
| セクタサイズ | 2048バイト |
| アドレッシング | LBA (READ(10) CDB) |
| 対応CDB | TEST UNIT READY, REQUEST SENSE, START STOP UNIT (開始のみ), READ CAPACITY, READ(10) |

**API**:

| 関数 | 説明 |
|------|------|
| `atapi_init()` | CD-ROM検出 (セカンダリバンクのATAPIシグネチャ確認) |
| `atapi_present()` | CD-ROMドライブ存在チェック |
| `atapi_test_unit_ready()` | メディア挿入確認 |
| `atapi_read_capacity(cap)` | メディア容量取得 (AtapiCapacity構造体)。UNIT ATTENTION は REQUEST SENSE で消して出し直す、NOT READY は ASC 3Ah (媒体なし) だけ `ATAPI_ERR_NO_MEDIA` で確定し、04h/02h (initializing command required) には START STOP UNIT (開始) を 1 回だけ出して待たずに出し直し、他は 250ms 置いて `ATAPI_READY_RETRIES` (20) まで出し直す (待ちの合計は 1 装置あたり最大 5 秒 — トレイを閉じた直後の準備中 2〜5 秒を待ちきる。装置が 2 台なら `atapi_init` で最大 10 秒。`cpu_delay_us` は 1 回 100ms (`CPU_DELAY_US_MAX`) で丸めるので、`atapi_delay_us` が 100ms 以下の塊に分けて回す)。準備中のまま諦めたら `[atapi] NOT READY, gave up drv= st= asc/ascq=04/01` の形で最後の ASC/ASCQ を 1 行出す |
| `atapi_read_sectors(lba, count, buf)` | セクタ読み出し (2048B/セクタ, LBA指定)。連続する count セクタを `ATAPI_READ_MAX_SECTORS` (既定 16 = 32KB) ずつの READ(10) で読む。複数セクタが失敗したらその範囲を 1 セクタずつ読み直し、1 セクタでも落ちればそこで失敗 |
| `atapi_media_gen()` | 媒体の世代。エラーレジスタのセンスキーが UNIT ATTENTION (6) / NOT READY (2) のたびに進む。READ(10) の UNIT ATTENTION は `ATAPI_UA_RETRIES` (3) 回まで出し直す (UA を複数積む装置がある) |
| `atapi_get_stats(out)` | READ(10) の数・セクタ数・1 セクタずつへ落ちた数・DEVICE RESET / SRST の数・容量確認の出し直しの数・START UNIT の数・バスが死んだ印で断った数 (`dead_fails`) |

- **PIO の受け取り**: byte count limit (Cylinder Low/High) には `min(バッファ, ATAPI_PIO_BCL_MAX = 0xF800)` を書き、
  データは DRQ ごとに Cylinder Low/High のバイト数だけ読む。各ブロックの後に ALT_STATUS の空読みで 400ns 置く
  (置かないと前のブロックの DRQ=1 を次のブロックと取り違える)。受け取ったバイト数がちょうど要求どおりでなければ失敗
- **装置の選択**: `atapi_init` はセカンダリの**マスターとスレーブの両方**のシグネチャを見る (居ない装置は ALT_STATUS が
  0xFF なので BSY を待たない)。2 台あれば READ CAPACITY で**媒体の入っている方** (マスター優先) を選ぶ。
  `atapi_drive_index()` = 0 マスター / 1 スレーブ。
- **PACKET の順序** (`atapi_send_cdb`、ATA の規定): バスがいま選んでいる装置が BSY/DRQ で、それが**シグネチャの出た装置**
  なら終わるのを待つ (0xFF や、居ない装置の値は待たない) → DRV_HEAD → 400ns → 選んだ装置の BSY/DRQ クリア待ち →
  Features / Byte Count → PACKET。`atapi_select_bank(1)` はバンクを切り替えるだけで DRV_HEAD は書かない。
  選択の待ちが期限切れ (下の「待ちの上限」) なら **DEVICE RESET (08h、その装置だけ。BSY でも受ける)** → それでも戻らなければ **SRST** (バンクで選んだ
  バスの 2 台。プライマリの HDD には届かない — UNDOCUMENTED io_ide 074Ch、NP21/W `ideio_o74c`。**同じバンク (セカンダリ) の
  ATA HDD (`ide.c` の drive 2 / 3) も戻す**)。SRST は解いてから 2ms 置き (`ATAPI_SRST_SETTLE_US`)、マスターの BSY=0 を最大 31 秒待ってから
  (マスターが居なければ待たない) 使う装置を選び直す (DRV_HEAD → 400ns → BSY=0)。スレーブの SRST 後の BSY は、選び直した後の
  待ち (`s_wait_limit_us`、ふだん 10 秒) までしか待たない。31 秒経っても BSY なら DRV_HEAD を書かずに
  期限切れを返す (`atapi_srst` の戻り値。`atapi_init` はそこで諦める)。どちらの後も装置は
  UNIT ATTENTION を立てるので、次のコマンドの出し直しは呼び手が行う。
  2026-09-26 まではマスターしか見ず、NP21/W で ide2 が空の CD のまま ide3 (セカンダリのスレーブ) に ISO を付けると
  `cd0: block 1 sects` (NP21/W は空のドライブの容量を 0 と答える) になり、1 セクタも読めなかった
- **待ちの上限 (秒、2026-09-26 [TASK_ATAPI_TIMEOUT](archive/realhw_v21/TASK_ATAPI_TIMEOUT.md))**: BSY / DRQ の待ちは ALT_STATUS を
  1 回読むごとに `cpu_delay_us(ATAPI_POLL_US = 100µs)` を挟み、挟んだ時間の合計で上限を数える (`atapi_wait_clear`)。
  それまでは `IDE_TIMEOUT_LOOP` (100 万回の inp、Ra266 で 0.5〜1 秒) で、実機のスピンアップ (READ(10) で 2〜4 秒 BSY) に足りず
  健全な装置を DEVICE RESET で捨てていた。`tick_count` で数えないのは、呼ばれる文脈 (起動時の `atapi_init`、KAPI 経由の読み) で
  PIT の割り込みが来ていると決められないから (時計は `cpu_delay_us` の 1 本だけ。誤差 ±10% は上限の余裕で吸う)。

  | 上限 | 値 | 根拠 |
  |---|---|---|
  | `ATAPI_CMD_TIMEOUT_US` | 10 秒 | 通常の PACKET・装置の選択・DEVICE RESET の後。スピンアップ 4 秒の 2 倍 + 余裕 (-10% でも 9 秒) |
  | `ATAPI_INIT_TIMEOUT_US` | 5 秒 | `atapi_init` の間だけ (シグネチャ・2 台のときの容量確認)。スピンアップ 4 秒は待ちきり、起動の最悪時間を抑える。電源投入直後の長い BSY は SRST の 31 秒が受け持つ |
  | `ATAPI_SRST_TIMEOUT_US` | 31 秒 | SRST の後のマスターの BSY。ATA の規定の最大 |

  **「遅いだけ」と「固まった」の境目**: コマンドの待ちが期限切れになっても DEVICE RESET はしない (期限切れを返すだけ)。
  次のコマンドの装置選択で**さらに上限まで待っても** BSY / DRQ のときだけ DEVICE RESET する — READ(10) の経路では
  BSY が 20 秒続いた装置だけがリセットされる。スピンアップ 3〜9 秒の READ(10) は 1 回目で読める (ホスト試験 `np2_spinup`)。
  DEVICE RESET / SRST の期限切れ / START UNIT は `[atapi] DEVICE RESET (BSY/DRQ past the limit twice) drv= st= limit=10s`、`[atapi] SRST: BSY did not clear, bus marked dead drv= st= limit=31s`、`[atapi] NOT READY, START UNIT ... asc/ascq=04/02` の行を
  `ATAPI_DIAG_MAX` (8) 行まで出す。

  **バスが死んだ印** (`s_bus_dead`): SRST が 31 秒で期限切れになったら立てる。立っているあいだ `atapi_read_sectors` /
  `atapi_read_capacity` / `atapi_test_unit_ready` はバスに触らず即 `ATAPI_ERR_TIMEOUT` を返し (`AtapiStats.dead_fails` を数える)、
  最初の 1 回だけ `[atapi] bus dead since SRST timeout, failing at once until atapi_init drv= st= limit=31s` を出す。
  それまでは `ls /cd0` のたびに約 61 秒 (選択の前後 10 + 10、DEVICE RESET の後 10、SRST 31) 待っていた。解くのは次の
  `atapi_init` だけ (呼び直しがバスの再試行)。ホスト試験 `np2_bus_dead`

  **起動の最悪時間** (`atapi_init`、`cpu_delay_us` が数える時間。ホスト試験 `np2_boot_worst` が同じ数に固定):
  装置なし (浮いたバス 0xFF) は SRST の 2ms だけ。2 台とも電源投入から BSY のまま (シグネチャも出ない) は
  マスター 5 秒 + スレーブ 5 秒 + SRST 2ms + SRST の後 31 秒 = **41.0 秒**で「CD なし」。2 台ともシグネチャは出るが最初の
  PACKET で固まる形は、容量確認 5 秒 + スレーブを選ぶ前の待ち 5 秒 + DEVICE RESET の後 5 秒 + SRST 2ms + 31 秒 = **46.0 秒**
  (これが上限)。どちらも `cpu_delay_us` の誤差 (±10%) と inp の時間 (読み 1 回ごとに約 1µs = +1%) の分だけ前後する。
  居ない装置の ALT_STATUS が BSY に見える機械 (0x80 など) では、居る装置が 1 台でもあればシグネチャの確認が居ない装置の
  分だけ 5 秒延びる。**1 台も居なければ 41.0 秒** — シグネチャが出ないので SRST し、`s_present_mask` が空のまま 0x80 を
  「マスターの BSY」と見て 31 秒待つ (上の「2 台とも BSY」と同じ形。`np2_boot_worst` の (c) 5 秒 / (d) 41.002 秒)
- **読みの失敗の行**: `[atapi] READ(10) drv= lba= n= ret= st= err= sense= got= req=` を起動から `ATAPI_DIAG_MAX` (8) 行まで出す。
  `lba` / `n` と `st` / `err` / `sense` / `got` は**最後に落ちた READ(10)** (複数セクタが落ちて 1 セクタずつ読み直したなら
  その 1 セクタ) とその終わった時点の値、`req=` は呼び手の範囲。`sense=5` は範囲外か空のドライブ (NP21/W は空のドライブへの
  READ(10) を ILLEGAL REQUEST で返す)、`ret=-1` は待ちの期限切れ (`st` はそのときの ALT_STATUS、`got` はそこまでの数)、
  `got` が n×2048 未満は転送が足りない
- READ CAPACITY は 8 バイトちょうど来なければ失敗 (受け皿の残りを容量にしない)。有効バイト数は Byte Count
  (`xfer_size`) で数え、奇数バイトの DRQ で 1 ワード多く読んでも数に入れない (7 バイトを 8 と数えない)
- **ドライブとの相性で困ったら `ATAPI_READ_MAX_SECTORS` を 1 にする** (`drivers/atapi.h` の 1 か所、旧来の 1 セクタずつに戻る)
- **NP21/W は READ(10) で UNIT ATTENTION を返さない** (媒体の交換は TEST UNIT READY だけが報告する)。
  世代の経路は実機でしか踏まない。NP21/W の DRQ は 2048 バイトずつ
- 試験: `make check-cd-read-host` (`tools/tests/test_cd_read.py`、記録 `tools/tests/cd_read_tdd.md`)

### §5-7 マウス (mouse.c / mouse.h / mouse_bus.c / mouse_seamless.c)

PC-98バスマウスおよびNP21/Wシームレスマウスに対応するポーリングベースのマウスドライバ。
カーネル初期化時に自動検出し、利用可能なモードを選択する。

| 項目 | 仕様 |
|------|------|
| バスマウスI/O | 0x7FD9 (データ `MOUSE_DATA`), 0x7FDD (制御 `MOUSE_CTRL`), 0x7FDF (8255 モード `MOUSE_MODE`)。IRQ13 (スレーブ PIC IR5, INT 0x2D) |
| シームレスマウス | NP21/W拡張 (NP2SysP `getmpos`)。ホストカーソルの絶対座標 (0..65535) を `mouse_poll` ごとに読み、**現在の移動範囲 (`mouse_set_bounds`) へ比例配分**する (2026-09-06 まで 639/399 決め打ちで 480 ラインの下端に届かなかった)。ボタンは両モードとも 8255 ポート A から読む |
| 座標系 | 画面座標 (0,0)-(639,399)。gshell は PEGC / Cirrus で bounds を (639,479) にする |
| 検証 (NP21/W) | ai-debug の `POST /api/mouse`: シームレスには `ax`/`ay` (0..65535 の絶対座標の上書き、`abs=off` で解除)、バスマウスには `dx`/`dy`、ボタンは `btn`/`hold` (両モード共通) |
| ボタン | 左/右/中 (3ボタン) |
| ポーリング方式 | `mouse_poll()` でフレーム単位取得 |

**API**:

| 関数 | 説明 |
|------|------|
| `mouse_init()` | マウス検出・初期化 (シームレス優先、フォールバックでバスマウス) |
| `mouse_poll(info)` | 現在の座標・差分・ボタン状態を `MouseInfo` に取得 |
| `mouse_available()` | マウスの利用可能状態 (0=なし, 1=バス, 2=シームレス) |
| `mouse_set_bounds(x_min, y_min, x_max, y_max)` | 座標クランプ範囲設定 |

**マウスカーソル (カーネル管理)**:

| 関数 | 説明 |
|------|------|
| `mouse_cursor_set_mode(mode)` | カーソルモード設定 (NONE/TEXT/GFX) |
| `mouse_cursor_show()` | カーソル表示 |
| `mouse_cursor_hide()` | カーソル非表示 (画面更新時のhide/showパターン) |

TEXTモードではTVRAM属性反転 (ビット2 XOR) によるカーソル表示を行う。漢字2セル境界を自動検出し、左半分から反転する。

---

### §5-8 RTC (rtc.c / rtc.h)

µPD4990A カレンダ時計をI/Oポート直接制御で操作する (PC9800Bible §2-4, §4-3)。

| 項目 | 仕様 |
|------|------|
| I/Oポート | 書込み 0x20 (`RTC_SET` — DI/CLK/STB/C2-C0 ビット制御)、読出し 0x33 (`RTC_READ` — bit0 = CDAT = DATA OUT) |
| 読み出し | レジスタシフトモードでシリアル読み出し |
| API | `rtc_read(rtc_time)` — 年月日・時分秒を取得 (KernelAPI 経由でも公開) |

### §5-9 漢字キャラクタジェネレータ (kcg.c / kcg.h)

PC-98内蔵フォントROMから文字パターンを読み出す。起動時にコンベンショナルメモリ
(0x01000〜) へフォントキャッシュを構築する。

| 項目 | 仕様 |
|------|------|
| I/Oポート | 0xA1 (JIS下位), 0xA3 (JIS上位-0x20), 0xA5 (ライン+L/R), 0xA9 (パターン) |
| 対応文字 | ANK (8×16) / JIS第1・第2水準漢字 (16×16) |
| API | `kcg_init()`, `kcg_read_ank(ch, buf)`, `kcg_read_kanji(jis, buf)`, `kcg_set_scale(s)` |

### §5-10 NP21/W 通信 (np2sysp.c / np2sysp.h)

NP21/W エミュレータの np2sysp 拡張ポートと通信し、エミュレータ検出・バージョン取得・
HostDrv 状態確認を行う。

| 項目 | 仕様 |
|------|------|
| I/Oポート | 0x7EF (文字列コマンド/レスポンス), 0x7ED (32bit値シフトレジスタ) |
| API | `np2_detect()`, `np2_get_version()`, `np2_get_cpu()`, `np2_get_clock()`, `np2_check_hostdrv()` |

### §5-11 デバイス抽象化層 (dev.c / disk.c)

- `dev.c` — ブロック/キャラクタデバイスの登録・列挙 (`dev_count`, `dev_get_info`)。初期登録: `fdd0` (2HD), `con` (コンソール)。IDE 検出時に `hd0`〜 が追加される
- `disk.c` — FDD セクタI/Oユーティリティ (`disk_read_lba` / `disk_write_lba`)。FDC ドライバ経由で BIOS 不使用 (PM PIO)

### §5-12 整数数学ライブラリ (libos32math)

FPU非依存の整数数学ライブラリ。KernelAPIへの依存なし。純粋C89整数演算のみで構成され、外部プログラムライブラリ群の最も基底に位置する。

| モジュール | 説明 |
|------------|------|
| `fix16.c` | Q16.16固定小数点四則演算 (64bit中間値でオーバーフロー防止) |
| `trig.c` | sin/cos LUT (512エントリ, 15bit精度, 値域 -32767～+32767) |
| `sqrt.c` | 整数平方根 (ニュートン法) + 高速距離近似 (α-max-plus-β-min) |
| `atan2.c` | CORDIC方式整数atan2 (8回反復, シフト+加算のみ) |
| `recip.c` | 逆数LUT 257エントリ (b=1～256の高速除算) |
| `random.c` | xorshift32擬似乱数 (周期 2^32-1) |
| `vec2.c` | 2Dベクトル演算 (Q16.16ベース, 12関数) |
| `lerp.c` | 線形補間 + 7種イージング関数 |

**依存関係**:

```
libos32math  (依存なし — 最も基底のライブラリ)
     ↑
     ├── libos32gfx   (math + KAPI)
     ├── libos32snd   (math + KAPI)
     ├── libos32tilemap (math + gfx)
     └── ゲーム本体    (math + 任意のlib)
```

**リソース使用量**: 合計約4KB (コード~1.6KB + LUTデータ~2.3KB)。

詳細は [LIBMATH_DESIGN.md](archive/v21/libmath/LIBMATH_DESIGN.md) を参照。

---
