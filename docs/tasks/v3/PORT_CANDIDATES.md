# PORT_CANDIDATES — 既存ソフトウェアの移植候補 (v3 後半、P9)

> 状態: **計画 (2026-09-30)** — ユーザー決定 (2026-09-30): GUI アプリ群と移植用の層 (P9: Video HAL・VESA2 的互換層・SDL 1.2 の受け皿) は **v3 後半、基盤が整い次第**。**ZSNES は移植する**。**移植は難度の低いものから挑戦する**。この票はその候補の一覧と挑戦順。個々の移植の票はまだ無い (着手時に `TASK_PORT_<名前>.md` を切る)。
>
> 発行: コーダー `claude-fable-5-1` (feat/gui、PM の指示による)。上位は [V3_PLAN_DRAFT.md](V3_PLAN_DRAFT.md) §3 P9、思想は [DESIGN_APP_FIRST.md](../../DESIGN_APP_FIRST.md) §8〜§10。

読み方: §1 が移植の前提となる基盤 (いま有るもの・無いもの)、§2 が候補の一覧表、§2-1 が ZSNES の詳細、§3 が推奨の挑戦順、§4 が未確認の事項、§5 が出典。**ライセンスと実機性能の記述は §4 に書いたとおり推測を含む** — 着手時に各票で原典を確かめる。

---

## 1. 移植の前提となる基盤

「有る」は v2.1 の事実 (正典: [KAPI_SPEC.md](../../KAPI_SPEC.md)、[08_build.md](../../08_build.md) §8-5、`sdk/crt/syscalls.c`)。「無い」は v3 で用意するもので、P8 / P9 の票に落ちる。v3 の決定 (アプリは 0x80000000〜 の私有写像、`mem_map` / `mem_unmap`、協調型、KAPI/ABI の後方互換なし、C11、32 ビット限定) の正典は [TASK_MEMMAP_V3.md](TASK_MEMMAP_V3.md) §0。

| 項目 | いま有るもの (v2.1) | 移植に足りないもの / v3 で決めること | 落とし先 |
|---|---|---|---|
| **C コンパイラ・libc** | `i386-elf-gcc` 13.2 + newlib 4.4 **nano 構成** (`-lc -lgcc`、`libm.a` あり)。syscalls は `read/write/open/close/lseek/stat/fstat/unlink/isatty/gettimeofday/times/sbrk` の 18 本 (`sdk/crt/syscalls.c`)。ユーザーランドは `-std=gnu89 -march=i386` | **無い**: `opendir/readdir` (KAPI `sys_ls` はある)、`mkdir/rename` の libc 側 (KAPI `sys_mkdir/sys_rename` はある)、`signal`、`select/poll`、`mmap` (v3 の `mem_map` に対応させる)、環境変数、`clock_gettime`。nano の `printf` は **`%f` を既定で出さない** (`-u _printf_float` が要る、未確認 U1)。`sbrk` は OS32X ヘッダの `--heap` で上限が決まる (v3 では `mem_map` で伸長、D23) | P9 の「libc の穴」票 (最初の移植で埋める) |
| **C++ ランタイム** | **無い** — クロス環境は C だけ (`i386-elf-g++` も `libstdc++` も未ビルド、`ls ~/opt/cross/bin` 2026-09-30)。カーネルの規約は C89 → v3 で C11 | ScummVM / DOSBox / Snes9x / NXEngine は C++ が必須。`libstdc++` (または `libsupc++` だけ) をクロス環境に足すか、C++ を捨てるかは**別の決裁**。例外・RTTI・静的コンストラクタの起動順 (crt0 が `main` の先頭へ飛ぶ約束) も影響する | 決裁 (P9 の前)。候補の表では「C++」を依存に書いた |
| **浮動小数点 (x87)** | CR0.EM=0 で **ハード x87 を直接使う** (`kernel/kentry.asm`、`kernel/kernel.c:545`)。`-march=i386` でも x87 命令は出る。実機 (P100 / Ra266) は x87 内蔵、NP21/W も再現 | **アプリ切替時の x87 文脈の退避が無い** (`fnsave/frstor` はカーネルに無い、2026-09-30 grep)。協調型で切替が `OP_WAIT` の中だけなら壊れないが、v3 の「固まったアプリからカーネルが制御を取り戻す」(P6) が入ると要る。整数のみのライブラリ (Tremor / libmad / minimp3 既定) を優先すれば影響を小さくできる | P6 / P9 (x87 の退避を T2 の受入に足すか) |
| **画面 (Video HAL)** | `gfx_get_framebuffer` / `gfx_present_dirty` / `gfx_hw_blit` / `gfx_set_palette`。GUI 1.x は **8bpp PACKED8** (PEGC 640×480×256、9801 は planar 16 色)。Cirrus (NP21/W) / Trident (実機) の窓の帯 `0xFE000000` は実装済み、**16bpp 面は無い** | Video HAL の共通化 (framebuffer / pitch / VSync / flip / optional BitBLT)、**640×480×16bit** (DESIGN_APP_FIRST §5 の境界)、全画面 lease (D19)、VESA2 的な「LFB の番地 + pitch + モード一覧」だけの互換層。8bpp で足りる移植 (Doom / Wolf3D / テキスト系) は 16bpp を待たない | P9 (Video HAL の票、TASK_TRIDENT_DRIVER 段 3〜5) |
| **音 (PCM / FM)** | `pcm_open/pcm_write/pcm_status/pcm_close` (CS4231、[TASK_PCM_CS4231.md](TASK_PCM_CS4231.md) 受入待ち)、`fm_note_on` / `ssg_*` / `snd_bgm_play` (OPNA、MML)、`snd_se_play_raw` | **SDL 1.2 の音はコールバック (別スレッド) 前提** → OS32 では (a) メインループから PCM リングを埋める、(b) PCM の DMA 割り込み (カーネル文脈) からアプリのコールバックを呼ぶ (CPL=3 へ上がる経路が要る) のどちらか。**(a) を既定**にし、ゲームのフレームごとに `pcm_write` する (遅延 = リング長)。ミキサ (複数音の合成、`SDL_mixer` 相当) はユーザーランドのライブラリで | P8 (音の層) |
| **入力** | `kbd_trygetkey` / `kbd_is_pressed` / `kbd_trygetrawkey` / `kbd_get_modifiers`、`mouse_poll` / `mouse_set_bounds`、`libos32input` (割り当て表) | **押し離しのイベント** (SDL の `KEYDOWN/KEYUP`) を `kbd_trygetrawkey` から組む。**ジョイスティック KAPI は無い** (PC-98 のジョイポートは OPNA 経由 — 要るなら P8)。マウスの相対移動 (FPS 用) | P8 (入力の層) |
| **ファイル** | VFS (ext2 / FAT / iso9660 / HostDrv / SerialFS)、`OS32_ERR_*`、パスは `/`。`stdio` は newlib 経由で動く | ディレクトリ列挙の libc 側、`fopen` の `"rb"`/`"wb"` は動く。大きなファイル (ROM 4MB、WAD 12MB、ScummVM のデータ数十 MB) の**読みの速さ** — IDE は PIO (P5 の追加項目)。`mmap` 相当は無い (`mem_map` + 読み込みで代える) | P5 (IDE DMA)、P9 |
| **時間** | `get_tick` (10ms)、`sys_time_now` (µs、KAPI v59)、`rtc_read`、`sys_time` | `SDL_GetTicks` (ms) と `SDL_Delay` は `sys_time_now` + `sys_yield` で組める。**1kHz tick** は P3 の候補。VSync 待ちは Video HAL に | P9 (SDL 層) |
| **スレッド** | **無い**。協調型 (`OP_WAIT` / `sys_yield`)、前景 1 本 | `SDL_CreateThread` / mutex / semaphore は**提供しない** (協調型なので `SDL_INIT_NOTHREADS` 相当で組み、スレッドを使う移植元は書き換える)。ZSNES は SDL 版でも音をコールバックに頼る → 上の (a) | P9 (SDL 層の「しないこと」) |
| **メモリ** | アプリ帯は `0x500000〜`、`--heap` で上限 | v3: 私有写像 0x80000000〜、**8MB 機は私有 2MB が受入条件** (D9)、32MB / 64MB 機はそれ以上を池から借りる (R2)。ROM 4MB + エミュレート RAM + BB を持つ ZSNES は **32MB 機以上**が前提 | P1 (決定済み) |
| **ビルド** | `sdk/` (静的アーカイブ 25 本)、`mkos32x.py`、`main()` が最初の関数、OS32X ヘッダ | 移植元の `configure` / CMake は使わず、**OS32 側に Makefile を書く** (`apps/` の流儀)。`-march=pentium` / `-mmmx` は移植ごとに許す (カーネルは i386 のまま)。NASM は有る (ZSNES に要る) | 各移植の票 |

### 1-1. ライセンスと同梱の扱い (提案、ユーザー決裁待ち)

os32 は **MIT** (`LICENSE`)。os32-v3 はパブリック (ROADMAP §0-1)。移植したものをどこに置くかは**ライセンスで分ける**:

| 移植元のライセンス | 置き場 (提案) | 理由 |
|---|---|---|
| MIT / BSD / zlib / CC0 / Unlicense (zlib、libpng、libjpeg-turbo、libogg/libvorbis/Tremor、minimp3、Lua、stb、dr_libs、WordGrinder、sc-im) | **os32-v3 に同梱してよい** (`ports/` か `apps/` の submodule) | MIT と両立、表示義務だけ |
| LGPL 2.1 (SDL 1.2 本体、mpg123) | 同梱可だが**別リポジトリ + 静的リンクの条件** (LGPL §6: 再リンクできる形で提供 = `.a` と `.o` を配る、または shlib) | OS32 のアプリは静的リンクが基本なので、LGPL の再リンク条件を満たす配布の形を決める。**SDL 1.2 は OS32 向けに書き直す部分が多いので、API だけ真似た自作 (zlib 相当の sdl12-compat のヘッダ) にすれば LGPL を避けられる** (提案) |
| GPL v2 / v2+ (ZSNES、doomgeneric、Wolf4SDL、Frotz、Angband (GPL v2 と二重)、OpenTyrian、libmad、Quake、DOSBox) / GPL v3+ (ScummVM、nano、NXEngine) | **別リポジトリ・別配布** (`os32-ports-gpl` 等)。os32-v3 本体には**リンクしない** (KAPI 経由で呼ぶだけの独立バイナリなら本体の MIT は汚れない) | GPL は派生物に伝播する。アプリは独立バイナリ (静的リンクは newlib と自作 lib だけ) なので、**GPL のアプリが OS32 の MIT を GPL にすることは無いが、その逆 (OS32 の MIT 部品を GPL アプリに入れる) は MIT 表示で済む**。SDK ヘッダは MIT なのでリンクしてよい |
| 非商用限定 (Snes9x「個人利用に限る」、Duke3D の BUILD ライセンス) | **同梱しない**。挑戦するなら個人の作業として別置き | パブリックのリポジトリで再配布できない |
| ゲームのデータ (Doom の `doom1.wad` = シェアウェア、SNES ROM、Tyrian のデータ = フリーウェア化) | **一切同梱しない**。Freedoom (BSD) のような自由データを試験に使う | 著作物。`docs/hw/` と同じ扱い |

**未決**: GPL の別リポジトリを os32-v3 の submodule にするか (submodule は「集合」であって派生物ではない、という一般の解釈だが**法的助言ではない**)、リンクだけにするか。§4 U2。

---

## 2. 候補の一覧

列の読み方: **難度** は「OS32 の基盤が §1 のとおり揃った時点で」の見込み (低 = 数日、中 = 数週、高 = 月単位か基盤の追加が要る)。**CPU/メモリ** は P100/32MB (v3 の快適さの判定機、D15)、Ra266 = Pentium II 266 級 64MB (実機)、8MB 機 (私有 2MB) の 3 段で書く。**前提** は §1 の落とし先。**すべて推測を含む** (§4)。

### 2-0. 練習台になるライブラリ (画面も音も要らない → 基盤に依存しない)

| 名前 | 種類 | 言語 | 依存 | ライセンス | CPU/メモリの目安 | 難度 | 前提 |
|---|---|---|---|---|---|---|---|
| **zlib** | ライブラリ | C | 無し | zlib | どれでも動く。8MB 機 OK | **低** — `configure` 無しで `*.c` を並べれば通る。ゲストには `lib/puff.c` (展開だけ) が既にある (車輪の再発明を避ける方針) ので、**圧縮側 (deflate) を足す意味**で | 無し (newlib) |
| **libpng** | ライブラリ | C | zlib | libpng (BSD 系) | 640×480 の PNG で作業メモリ数百 KB。8MB 機 OK | **低** — `pngusr.h` で機能を削る。**stb_image (MIT/PD) の方が 1 ファイルで速く終わる**ので、libpng は「本物の移植の練習」として | zlib |
| **libjpeg-turbo** (または IJG libjpeg) | ライブラリ | C (+ SIMD asm 任意) | 無し | IJG / BSD 3 条項 | JPEG 640×480 の展開 P100 で 1 秒未満 (推測) | **低〜中** — `jconfig.h` を手書き。SIMD asm (NASM) は `-DWITH_SIMD=0` で外す。**MMX 版の asm を Ra266 で有効にする**練習ができる | 無し |
| **Lua 5.4** | ライブラリ / スクリプト | C (ANSI) | `libm`、`setjmp`、`double` | MIT | 200KB 級。8MB 機 OK | **低** — `luaconf.h` の `LUA_32BITS` で整数 32 ビット。**x87 の `double` を最初に本気で使う候補** (U1 の `%f` も踏む)。CUI の `lua` で電卓が動けば合格 | x87 (退避の要否は P6) |
| **libogg + libvorbis (Tremor = 整数版 libvorbisidec)** | ライブラリ / メディア | C | 無し (Tremor は整数のみ) | BSD 3 条項 | Tremor の 44.1kHz デコードは P100 で実時間の 2〜3 割 (推測、ARM 手持ち機の実績から) | **低〜中** — `configure` の生成物を手書き。**Tremor を選ぶ** (libvorbis 本家は `float`) | PCM (再生器にするとき) |
| **minimp3** | ライブラリ / メディア | C (ヘッダ 1 本) | 無し (整数既定、`MINIMP3_NO_SIMD`) | CC0 | 約 30KB。P100 で MP3 128kbps の実時間デコードは**微妙** (推測、要実測)。Ra266 は余裕 | **低** — 2 ファイル。libmad (GPL、32 ビット固定小数点) より置き場が楽 | PCM |
| **libmad** | ライブラリ / メディア | C | 無し (100% 固定小数点) | **GPL** | minimp3 より速い可能性 (486 時代の設計)。Ra266 で実時間 | **低** だが GPL → 別配布 | PCM |
| **stb (stb_image / stb_truetype / stb_vorbis)** | ライブラリ | C (ヘッダ) | `libm` (一部 float) | MIT / PD 二重 | 小 | **低** — 1 ファイルずつ。**stb_truetype は T7b (OpenType) の候補でもある** | 無し |
| **SQLite** | (既存) | C | — | PD | — | 済み (カーネル同一リンク → v3 でモジュール、T4) | — |

### 2-1. ZSNES (必須候補、ユーザー決定 2026-09-30) — 難度 **高**

| 項目 | 事実 (出典 §5) | OS32 への含意 |
|---|---|---|
| **言語** | x86 アセンブリ (NASM) + C + C++。1.50 で「アセンブリの約 15% を C に移した」段階、**本体の多くが asm のまま**。「100% x86 互換の CPU が絶対に要る、他アーキへの移植は不可能」と公式が明記 | **32 ビット限定 (ユーザー決定 2026-09-17) の OS32 とは相性が良い** — asm を書き直さずに済む。ただし NASM の asm は**DOS (DJGPP) / Linux (ELF) の呼出規約と `_` 前置**の差を吸収する必要があり、i386-elf 環境では **ELF 側の流儀**を採る。C++ の部分 (GUI・設定・一部のチップ) は §1 の C++ ランタイムの決裁が先 |
| **最終版・状態** | 1.51 (2007-01-24)。開発は事実上停止。GPL-2.0-only (1.50〜) | 更新が無いので「移植したら終わり」。バグ修正は自前 |
| **必要 CPU/RAM (公式)** | DOS 版: Pentium II 233MHz、32MB (48Mbit ROM に 17MB 空き)。SDL 版: 266MHz (500MHz 推奨)。FAQ は「486/100 は古い DOS 版を使え」 | **P100/32MB は公式の最低を下回る** (DOS 版でも PII 233)。Ra266 (PII 266、64MB) が**公式最低ちょうど**。**8MB 機は対象外** (私有 2MB では ROM も入らない)。フレームスキップ前提で P100 が「遊べる」かは実測 (§4 U5)。**MMX** で 15 ビットの透過が速くなる → Ra266 の PII は MMX あり、P100 は無し |
| **画面** | DOS 版は VESA 2.0 + LFB (16 ビット色) を要求。解像度 320×240 / 512×448 / 640×480、8bpp モードもある (透過は 16bpp のみ) | **P9 の Video HAL / VESA2 的互換層の最初の顧客**。640×480×16bit は Trident (実機) か Cirrus (NP21/W) の窓、PEGC は 8bpp 止まり → PEGC では 256 色モード (透過なし) で動かす。**フリップ面の lease (D19、TASK_MEMMAP_V3 §2-2)** で LFB の番地を渡す |
| **音** | DOS 版は SB Pro/SB16、Windows は DirectSound、SDL 版は SDL のコールバック。SPC700 + DSP のエミュレーションは本体の CPU 負荷の大きい部分 | **PCM リング (CS4231) にメインループから書く** (§1 の (a))。ZSNES の音は 32kHz 固定小数点、CS4231 は 44.1kHz/48kHz → リサンプルか、CS4231 を 32kHz に設定 (可能かは U6) |
| **入力** | キーボード + ジョイスティック (DOS は直接、SDL は ManyMouse も) | `kbd_is_pressed` / `kbd_trygetrawkey` で押し離し。ジョイスティックは無し (P8 の候補) |
| **メモリ** | ROM 最大 6MB (48Mbit) + SNES RAM/VRAM 数百 KB + セーブステート数 MB + BB | **32MB 機で私有 12〜16MB** を `mem_map` で借りる (R2)。8MB 機は不可 |
| **ファイル** | ROM (`.smc/.sfc`、zip 対応は zlib 経由)、`.srm`、ステート `.zst` | zlib が先 (§2-0)。ROM 4MB を PIO の IDE から読む時間 (P5) |
| **スレッド** | 無し (DOS 版の設計) | 協調型と衝突しない |
| **障害 (難度が高い理由、順に)** | (1) **C++ ランタイムの決裁** (無いと GUI と一部が組めない → 先に C++ を捨てた「core only」ビルドを試す)、(2) **性能: P100 は公式最低の半分以下**、asm 部分の最適化は Pentium 向け (paired pipes) なので P100 でも最速だが、SPC/DSP と 16bpp 描画をフレームスキップで削るしかない、(3) **Video HAL (16bpp LFB + フリップ) が P9 の後半**、(4) **音のコールバックをメインループ駆動に書き換える** (DOS 版の SB の割り込み駆動と同型なので手本はある)、(5) NASM の asm の ELF 化 (DJGPP 版の `_` 前置と `int 0x31` の DPMI 呼び出しの置換)、(6) DOS 版は **DPMI で物理 VRAM を写像**する (`__dpmi_physical_address_mapping`) → OS32 の lease で代える | 着手は **§3 の最後**。手前に Doom (8bpp、C、asm 無し) と Wolf4SDL (SDL 1.2 の受け皿の検証) を置く |
| **代替 (参考、非採用)** | Snes9x (C++、**非商用限定** → パブリックに置けない、PII 300 以上を要求)、bsnes (C++、Core 2 級 → 論外) | ZSNES は x86 asm のおかげで**この世代の CPU で動く唯一の選択**。ユーザー決定でもある |

### 2-2. テキスト系・小規模ゲーム (8bpp どころかテキスト画面で足りる → 基盤への依存が最小)

| 名前 | 種類 | 言語 | 依存 | ライセンス | CPU/メモリの目安 | 難度と理由 | 前提 |
|---|---|---|---|---|---|---|---|
| **Frotz (dumb frotz = `dfrotz`)** | ゲーム (Z-machine) | C | 無し (dumb は curses も不要) | GPL v2+ | 386 でも動く。8MB 機 OK | **低** — `dfrotz` は `stdio` だけ。Z-code のデータ (Infocom) は非同梱、自由な Z-code (IF アーカイブ) で試験。**日本語は非対応** (UTF-8 の出力は `shell_print_utf8` に流せる) | 無し。GPL → 別配布 |
| **Angband 4.2** | ゲーム (ローグライク) | C | curses (前端) | GPL v2 と Angband license の二重 | 8MB 機 OK | **中** — `main-gcu.c` (curses) を **OS32 の TVRAM 前端 `main-os32.c`** に書き直す (`tvram_putchar_at` / `console_*`)。前端の口 (`term`) は小さく、移植例が多い | 無し。GPL → 別配布 |
| **NetHack 3.6** | ゲーム (ローグライク) | C | curses/tty | NetHack GPL (コピーレフト、GPL 非互換) | 8MB 機 OK | **中〜高** — `sys/` と `win/tty` の移植。ファイル構成 (レベルファイル、ロック) が多く `link`/`unlink` を使う。Angband の後 | 無し。NGPL → 別配布 |

### 2-3. 8bpp のゲーム (Video HAL の 8bpp 面 + PCM + 押し離し入力 = P8 / P9 の前半)

| 名前 | 種類 | 言語 | 依存 | ライセンス | CPU/メモリの目安 | 難度と理由 | 前提 |
|---|---|---|---|---|---|---|---|
| **doomgeneric (Doom)** | ゲーム | C | 無し (`DG_Init/DG_DrawFrame/DG_SleepMs/DG_GetTicksMs/DG_GetKey` の 5 本を書くだけ) | GPL v2 | 原作は 386SX + 4MB (VGA 320×200×256)。**P100 で余裕、8MB 機 (私有 2MB) は WAD 次第** (doom1.wad 4MB をメモリに載せると入らない → ファイルから逐次読み) | **低〜中** — 移植手順が最も整った候補。`DG_ScreenBuffer` は 32bpp (`uint32_t`、doomgeneric の実装) なので **8bpp 面へ変換して転送** (原作の 8bpp パレット描画に戻す改造 `CMAP256` があり、それを採る)。音は無し (原作の音は別途 `i_sound` を書く)。**Video HAL 8bpp + `pcm_write` の最初の顧客** | P9 (8bpp 面)、`sys_time_now` |
| **Wolf4SDL (Wolfenstein 3D)** | ゲーム | C++ (C 風) | **SDL 1.2** + SDL_mixer | GPL v2+ (原作は id license か GPL) | 原作は 286/386。8MB 機 OK (データ 1MB) | **中** — SDL 1.2 の受け皿 (`SDL_SetVideoMode` 8bpp / `SDL_OpenAudio` / キーイベント) を**最初に通す教材**。C++ だが `new`/`delete` と参照程度 (要確認、libstdc++ 無しで `-fno-exceptions -fno-rtti` で通るか U3)。データはシェアウェア版 (非同梱) | P9 (SDL 1.2 層)、P8 (PCM ミキサ) |
| **OpenTyrian** | ゲーム (縦シューティング) | C | SDL 1.2 (Net 無しで可) | GPL v2 | 原作は 386 + 4MB。P100 で余裕、8MB 機 OK (データ 5MB は逐次読み) | **中** — 純 C の SDL 1.2 ゲーム。データはフリーウェア化 (再配布は許可の確認、U4) | P9 (SDL 1.2 層)、P8 |
| **Quake (WinQuake ソフト描画)** | ゲーム | C (+ x86 asm 任意) | 無し (直接 SDL に載せた派生が多い) | GPL v2+ | 原作最低 **P75 + 8MB (DOS)**。P100 で 320×200 が遊べる線。8MB 機 (私有 2MB) は**不可** (`-heapsize` 8MB) | **中〜高** — x87 を本気で使う (透視補正)。`-march=pentium` の asm 版で半分速くなる。**x87 の退避 (P6) の試金石** | P9、x87 |
| **NXEngine-evo (Cave Story)** | ゲーム | C++ | SDL2 (evo) / SDL 1.2 (旧 nxengine) | GPL v3+ | 8MB 機 OK (推測) | **高** — C++ 必須、evo は SDL2。旧 nxengine (SDL 1.2) も C++ | C++ の決裁 |

### 2-4. 重いもの (C++ 必須・スレッド・性能) — v3 では**挑戦しない候補**として記録

| 名前 | 種類 | 言語 | 依存 | ライセンス | CPU/メモリ | 難度と理由 |
|---|---|---|---|---|---|---|
| **ScummVM** | エミュレータ (アドベンチャーの VM) | **C++** | SDL2 (旧版 1.x は SDL 1.2) | GPL v3+ | 「控えめ」と言われるが**本体 10MB 級、データ数十 MB**。P100 で SCUMM v5 (Monkey Island) は動く見込み (DOS 時代の原作は 286)、8MB 機は不可 | **高** — C++ の本体 (`Common::`、テンプレート、仮想関数) に libstdc++ が要る。旧版 ScummVM 0.x〜1.x (SDL 1.2) を選べば軽い。**ZSNES の後、v4 の候補** |
| **DOSBox** | エミュレータ (x86 PC) | **C++** | SDL 1.2 (0.74) | GPL v2+ | ホスト P100 で 286 相当の速度が出るかどうか (動的コアは x86 ホスト向けがあるが、OS32 の私有写像で実行可能ページが要る) | **高** — 性能的に「実機の中で 286 を動かす」意味が薄い。**OS32 は V86 で本物の DOS を動かせる**ので採らない |
| **Snes9x / bsnes** | エミュレータ | C++ | — | 非商用 / GPL v3 | PII 300 以上 / Core 2 以上 | **採らない** (§2-1) |

### 2-5. メディアのビューア・再生器 (画像 / 音楽 — GUI アプリ群の入口)

| 名前 | 種類 | 言語 | 依存 | ライセンス | CPU/メモリ | 難度と理由 | 前提 |
|---|---|---|---|---|---|---|---|
| **画像ビューア (自作、stb_image / libpng / libjpeg-turbo で PNG / JPEG / BMP)** | メディア | C | §2-0 | MIT / BSD | 640×480 の JPEG で P100 1 秒未満 (推測) | **低** — `mgxview` (既存) に形式を足す。ROADMAP §2「image viewer (VBZ / VDP / BMP)」と同じ入れ物 | §2-0 |
| **音楽再生器 (自作、Tremor / minimp3 / dr_wav)** | メディア | C | §2-0、PCM | BSD / CC0 | Ogg 44.1kHz の実時間デコードが P100 で成立するかは実測 (U5) | **低〜中** — `pcm_write` の実運用。ROADMAP §2「music player (FM 音源 BGM)」の隣 | PCM (E4〜E6) |
| **動画** | メディア | — | — | — | — | **候補にしない** (MPEG-1 320×240 は P100 で限界、Host Service の codec に回す — LEGACY_LIVING_PRESERVATION) | — |

### 2-6. Word / Excel 相当の目標に向けた軽量エディタ・表計算

「Word/Excel 級の起動 2 秒以内、入力→描画 100ms 以内」(V3_PLAN_DRAFT §2-1) の判定に使う本物のアプリの候補。GUI 版は libos32gui に載せ直すので「移植」というより「エンジンの流用」。

| 名前 | 種類 | 言語 | 依存 | ライセンス | CPU/メモリ | 難度と理由 | 前提 |
|---|---|---|---|---|---|---|---|
| **WordGrinder** | ツール (ワープロ、端末) | C + **Lua** | ncurses (端末前端)、Lua 5.x、iconv (任意) | MIT | 8MB 機 OK (推測) | **中** — 本体のロジックが Lua、前端が C。**Lua を先に移植しておく**と前端 (`dpy_*` の数十本) だけ TVRAM / libos32gui に書き直せば動く。ODT / HTML 出力。**日本語 (UTF-8) を扱える**のが大きい | Lua (§2-0)、TVRAM 前端 |
| **sc-im** | ツール (表計算、端末、vim 風) | C | ncurses、(任意) libxlsxwriter / libxls / libzip | BSD 4 条項 (要確認 U4) | 8MB 機 OK | **中** — `sc` の後継で活発。前端の書き直しは sc と同型。**Excel 級の目標の「再計算」の計測台** | TVRAM 前端 |
| **sc** | ツール (表計算) | C | curses | Unlicense / PD | 8MB 機 OK | **低〜中** — 小さいが古い (K&R 風)。sc-im が代替 | 同上 |
| **GNU nano** | ツール (エディタ) | C | ncurses | GPL v3+ | 8MB 機 OK | **中** — curses 前端が深い。OS32 には `edit` (自作) が既にあるので**優先度は低い** | TVRAM 前端。GPL → 別配布 |
| **AbiWord / Gnumeric** | GUI ワープロ / 表計算 | C (GTK) | GTK+、glib、Pango、… | GPL v2+ | 数十 MB | **採らない** — GTK の山。目標は「同種のアプリ」であって移植ではない (自作 + 上の 2 つのエンジン流用で測る) | — |

**候補の総数**: §2-0 が 8 (SQLite は既存なので数えない)、§2-1 が 1、§2-2 が 3、§2-3 が 5、§2-5 が 2、§2-6 が 4 → **挑戦する候補 23**。採らない・除外は 5 行 (§2-4 の 3、動画、AbiWord/Gnumeric)。

---

## 3. 推奨の挑戦順 (難度の低いものから、前提の基盤が揃う順に)

| 順 | 候補 | 待つ基盤 | 何を確かめるか (合格の目安) |
|---|---|---|---|
| 1 | **zlib** → **libpng / stb_image** | 無し (v2.1 の SDK で今日からできる) | 「他人の `*.c` を OS32 の Makefile で通す」手順、newlib nano の穴の洗い出し (U1)。`gzip -d` 相当の CUI と PNG → MGX 変換 |
| 2 | **Lua 5.4** | x87 (退避の要否だけ P6 で決める) | `double` と `%f`、`setjmp/longjmp`、`libm`。対話環境が動く。**WordGrinder の前提** |
| 3 | **Frotz (dfrotz)** | 無し | `stdio` だけの GPL アプリの**別配布の形** (§1-1 U2) を最初に決める |
| 4 | **Tremor / minimp3** → 音楽再生器 | PCM (E4〜E6 の受入) | `pcm_write` の実運用、P100 の実時間デコード (U5) |
| 5 | **Angband** (TVRAM 前端) | 無し | `term` 前端の書き方 = **後の curses 系 (sc-im / WordGrinder / nano) の共通部品** (`libos32curses` 的な薄い層を切り出すかはここで判断) |
| 6 | **libjpeg-turbo** → 画像ビューア | 無し | MMX asm を Ra266 で有効にする手順 (`-march` を移植ごとに変える流儀) |
| 7 | **doomgeneric** | **P9 前半: Video HAL 8bpp 面 + 全画面 lease**、`sys_time_now` | 8bpp 全画面 + 押し離し入力 + フレーム時間。**GUI アプリ群と P9 の受入の最初の実アプリ** |
| 8 | **WordGrinder / sc-im** (TVRAM 版 → libos32gui 版) | Lua、5 の前端 | **Word / Excel 級の判定** (起動 2 秒、入力→描画 100ms、再計算) の計測台 |
| 9 | **Wolf4SDL → OpenTyrian** | **P9: SDL 1.2 の受け皿** (8bpp `SetVideoMode`、`OpenAudio` のメインループ駆動、キーイベント)、P8 のミキサ、C++ の可否 (Wolf4SDL、U3) | SDL 1.2 層の設計を**実アプリ 2 本**で固める。ここで「SDL 1.2 の API のうち提供しないもの (スレッド・CD・ジョイスティック)」を確定 |
| 10 | **Quake (WinQuake)** | x87 の退避 (P6)、32MB 機 | x87 を本気で使うアプリ、`-march=pentium` の asm 版、8MB 超の私有メモリ (R2) |
| 11 | **ZSNES** | **P9 後半: 640×480×16bit の LFB + フリップ (Trident 段 3〜5 / Cirrus)**、C++ の決裁、PCM 32kHz (U6)、NASM の ELF 化 | 設計思想の負荷試験 (DESIGN_APP_FIRST §10)。Ra266 で公式最低、P100 はフレームスキップの実測 |
| (v4) | ScummVM (旧版) / NetHack | C++、データの大きさ、P5 のディスク | 記録だけ |

**順序の理由**: 1〜6 は v2.1 の SDK で今日からでき、P9 を待たない (基盤が整うまでの「練習」に充てられる)。7 から P9 前半、9 から SDL 層、11 で P9 後半。**ZSNES を最後に置くのは難度が最高で、待つ基盤が最多だから** — 手前の 7・9・10 が ZSNES の障害 (1)〜(6) を 1 つずつ先に踏む (7 = 8bpp 面と lease、9 = 音のメインループ駆動と SDL、10 = x87 と大きな私有メモリ)。

---

## 4. 未確認の事項 (推測と明記)

| # | 事項 | 確かめ方 |
|---|---|---|
| U1 | newlib nano の `printf` が `%f` を出すのに `-u _printf_float` が要るか、そのときの容量増 (数十 KB と推測) | zlib / Lua の移植で実測 |
| U2 | GPL の別リポジトリを os32-v3 の submodule にしてよいか (「集合」の解釈)。**法的助言ではない** — ユーザー決裁 | 決裁 |
| U3 | Wolf4SDL の C++ が `-fno-exceptions -fno-rtti` + `libsupc++` 無しで通るか (`new` の実装だけ自前で足りるか) | ホストで `i386-elf-gcc -x c++` を試す (g++ フロントエンドの有無を先に確認) |
| U4 | ライセンスの細部: sc-im (BSD 4 条項と記した、要原典)、OpenTyrian のデータ (Tyrian 2000 のフリーウェア化の再配布条件)、libjpeg-turbo (IJG + BSD + zlib の 3 本)、Angband の二重ライセンスの選び方 | 各票の着手時に `LICENSE` を読む。**§2 の表のライセンス列は Web 検索の要約 (§5) であって原典の確認ではない** |
| U5 | 実機性能はすべて推測: P100 での Tremor / minimp3 の実時間デコード、Doom の fps、Quake 320×200、**ZSNES のフレームスキップ**。NP21/W は速度を模擬しないので**実機でしか測れない** | Ra266 で実測 (P100 相当は Ra266 のクロックを落とせないので推定のまま) |
| U6 | CS4231 を 32kHz に設定できるか (ZSNES の内部レート)。できなければ 44.1kHz へのリサンプル | TASK_PCM_CS4231 のレジスタ表 |
| U7 | x87 文脈の退避 (fnsave 108 バイト) を P6 の「制御を取り戻す」経路に足す必要 — 前景 1 本なら不要、裏のアプリが x87 を使っていれば要る | P6 の票 |
| U8 | doomgeneric の `CMAP256` (8bpp 出力) が現行の master に残っているか | fork 時に確認 |
| U9 | 8MB 機 (私有 2MB) で動く候補の見積り (Doom の WAD 逐次読み、OpenTyrian のデータ 5MB) はすべて推測 | 各票の受入 |

---

## 5. 出典 (Web、2026-09-30 参照)

- ZSNES: [Wikipedia](https://en.wikipedia.org/wiki/ZSNES) (言語・ライセンス・1.51・asm 15% を C へ)、[公式 Readme](https://zsnes-docs.sourceforge.net/html/readme.htm) (必要 CPU/RAM、VESA 2 + LFB、SB、MMX)、[公式 FAQ](https://zsnes-docs.sourceforge.net/html/faq.htm) (100% x86、486/100)
- doomgeneric: [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) (GPL-2.0、5 関数、doom1.wad)
- Wolf4SDL: [fabiangreffrath/wolf4sdl](https://github.com/fabiangreffrath/wolf4sdl)、[Debian](https://packages.debian.org/sid/games/wolf4sdl) (GPL v2+)
- Snes9x: [snes9xgit/snes9x LICENSE](https://github.com/snes9xgit/snes9x/blob/master/LICENSE) (個人利用限定)、[Emulation General Wiki](https://emulation.gametechwiki.com/index.php/Snes9X)
- Frotz: [David Griffith](https://davidgriffith.gitlab.io/frotz/) (GPL v2+、dumb / curses / SDL)
- Angband: [docs/copying.rst](https://github.com/angband/angband/blob/master/docs/copying.rst) (GPL v2 と Angband licence の二重)
- NetHack: [NetHack General Public License](https://nethack.org/common/license.html)
- ScummVM: [Wikipedia](https://en.wikipedia.org/wiki/ScummVM) (C++、GPL v3+)、[FAQ](https://www.scummvm.org/faq/)
- DOSBox: [Wikipedia](https://en.wikipedia.org/wiki/DOSBox) (GPL v2+)、[System Requirements](https://www.dosbox.com/wiki/System_Requirements)
- Tremor: [Xiph GitLab](https://gitlab.xiph.org/xiph/tremor) (BSD 3 条項、整数のみ)
- libmad: [Underbit](https://www.underbit.com/products/mad/) (GPL、100% 固定小数点)
- mpg123: [Wikipedia](https://en.wikipedia.org/wiki/Mpg123) (LGPL 2.1)
- minimp3: [lieff/minimp3](https://github.com/lieff/minimp3) (CC0、固定小数点既定、約 30KB)
- SDL 1.2 / sdl12-compat: [libsdl.org license](https://www.libsdl.org/license.php) (1.2 は LGPL)、[libsdl-org/sdl12-compat](https://github.com/libsdl-org/sdl12-compat) (zlib)
- Lua: [Wikipedia](https://en.wikipedia.org/wiki/Lua) (5.0〜 MIT)
- stb: [nothings/stb](https://github.com/nothings/stb) (MIT / PD 二重)
- WordGrinder: [davidgiven/wordgrinder](https://github.com/davidgiven/wordgrinder)、[Wikipedia](https://en.wikipedia.org/wiki/WordGrinder) (C + Lua、MIT)
- sc / sc-im: [Wikipedia sc](https://en.wikipedia.org/wiki/Sc_(spreadsheet_calculator))、[andmarti1424/sc-im](https://github.com/andmarti1424/sc-im)、[FreshPorts](https://www.freshports.org/math/sc-im)
- GNU nano: [Wikipedia](https://en.wikipedia.org/wiki/GNU_nano) (GPL v3+)
- Quake: [id-Software/Quake](https://github.com/id-software/quake) (GPL v2+、asm 無しだとソフト描画が半分の速度)、[必要環境](https://gamesystemrequirements.com/game/quake) (P75 + 8MB)
- Doom の必要環境: [gamesystemrequirements.com](https://gamesystemrequirements.com/game/doom) (386SX + 4MB)
- OpenTyrian: [opentyrian](https://github.com/opentyrian) (GPL v2、SDL 1.2)
- NXEngine-evo: [FreshPorts](https://www.freshports.org/games/nxengine) (GPL v3+)
- TinyGL (参考、候補外): [C-Chads/tinygl LICENSE](https://github.com/C-Chads/tinygl/blob/main/LICENSE)
