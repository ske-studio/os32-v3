# PPC_FBCON_DESIGN — TVRAM の無い機種の共通の FB 文字端末 (初段は PowerMac の OF の 8bpp)

> 状態: **現行 (2026-11-13)** — 共通の kernel/console に FB の後端 fbcon を足し、HAL の GfxBackend (OF_FB) と KCG の口 (東雲 16 から作る KCG1) を通して描く。PC-98 の CUI は TVRAM のまま (x86 の CUI の経路は変えない)。段の状態は票 TASK_PPC_SHELL が持つ。

> 出所: ~/os32-tmp/run/ppc-fbcon2/DESIGN.final.md (sol 起草 r1→r2、Opus 5.5 の第 2 回 条件付き確定、PM 反映)。中身は変えずに写し、末尾の追記 (段ごとの取り込みで決めたこと) を含む。PowerPC に固有の注意は [PPC_NOTES](PPC_NOTES.md)。


## r1→r2 の変更
N1: 初段を INDEX8 に限定し、OF_FB/LEDGER_SF/公開 OS32_SURFACE の末尾追加を §3 の [ABI] 手順に固定。
N2: ユーザー決定に従い東雲16 EBDT 由来 .kcgfont を ROM 無し KCG 本体に復帰、PPC は静的リンク。
N3: console_dual.c と PC-98 con_backend を維持し、PPC con_backend から共通 fbcon へ結線、x86 CUI の TVRAM 限定を変異試験で固定。
N4: 複数 TEXT・console_route・PC-98 FB 選択・PLANAR4 を後続票へ移し、共通性は偽 PC-98 寸法の host 試験で示す。
N5: Rust・TTF 輸送・長名 ramfs helper を削除、RAM root に短名の .kcgfont とライセンスを一組収録。
N6: v1_shim の mode_set/flip を確認し、通常面の同期 scanout 接続には不足するため of_fb_v2 を残す根拠を明記。
N7: /boot/kcg.mod、userland の KCG 描画先例を追記し、fontmgr は実体なし・FEP は別と訂正。
N8: provider 化の段 D に sep5_provider_host・surface_query_host と backend 値の追従を明記。
PM: §7 のユーザー判断を段ごとの内容/build_compare と G4 受入当日の時間確認に限定。

2026-11-13。照合基点: `361af19c`。対象: G4 PowerMac3,5 の USB キーボード＋画面だけで sh を使う。
本票だけを作成。製品・リポジトリ・生成物は変更しない。以下の新規 API 名は提案であり、実在 API と区別する。
単一タスク・協調切替、整数演算、文字列は kstring。共有コードに機種の条件コンパイルを置かない。

## 1. 実物の確認

| 層 | 実物・呼出しの道 (この作業木の file:line) |
|---|---|
| console の入口 | `kernel/console/console.c:195` shell_putchar → con_sink_push_print → putchar_raw → con_backend_putchar。shell_print は :205、console_write は :331 付近。UTF-8/JIS 変換・折返し・論理カーソルは同じ console が所有 |
| 画面の門 | 同 :53–57 の console_render_allowed、:103–108 の clear、:387 の cursor、:427 以降の GUI 出入。CONSOLE_DUAL 確定設計末尾の「GUI 中は消さない」を維持 |
| PC-98 の実際の字形 | `platform/pc98/console.c:55–60` が文字コードと属性を TVRAM に書く。字形は表示ハードが生成する。:71 が全角、:125 以降が GDC cursor。通常 CUI はソフトウェアでフォントをラスタライズしない |
| 出力先表 | `kernel/console/console_dual.h:6–14` は最大9行の役割・接続状態。`.c:22–35` は GUI/sink 有効時の TEXT 生存判定。:48 の getter は現在のキャッシュを返すだけで CUI 時に更新しない |
| PC-98 の結線 | `platform/pc98/display_dual.c:49–70` が GF 使用時に内蔵 TVRAM の TEXT 行を追加。`drivers/video/pc98/pc98_route.c` が安全性を管理。出力先表自体には描画関数がない |
| HAL | 実際の型は `include/gfx_hal.h:67–146` の GfxBackend、`include/gfx_mode.h:7–11` の GfxMode。`gfx/gfx_core.c:62–76` が provider から gfx2_adapter_register を呼ぶ。`gfx/v2/backend_reg.c:99–145` は既存登録口、`gfx/v2/enum.c:28` 以降は head 等の列挙 |
| 線形表示の手本 | `drivers/video/pc98/backend_pegc.c:452` の同期転送先、:550 の GfxBackend。`drivers/video/geforce/backend_geforce.c:781–795` の登録表。`drivers/video/pc98/pegc_v2.c:5–25` の CPU 描画能力を参考にする |
| HAL の制約 | `gfx/gfx_core_framebuffer.inc:64–82` は backend を機種の列挙で決める。`gfx/gfx_core_init.inc:11–17` は CLIENT があることを started の条件にする。単に OF の後端の表を足すだけでは起動しない |
| フォント | `drivers/video/kcg/kcg.c:45–52` → kcg_rom_read_ank/kanji。`kernel/console/font_source.c:62–79,207–238` は VFS の read lease であって字形生成器ではない。`font_guard.c:11–26` は IRQ/例外・再入を拒む |
| KCG の既存口 | `drivers/video/kcg/kcg_module.h:11–16` の read_ank/read_kanji/stats/invalidate、x86 は `/boot/kcg.mod`。`userland/lib/gfx/text/lconsole.c:357–360`・`gfx_kcg.c:54–98` は KCG 字形で描く先例。fontmgr は実体なし、FEP は変換辞書で字形供給とは別 |
| PPC 出力 | `build/ppc-kboot.mk:57,75–85` は kboot_console・console_stream・shell_putchar と con_sink をリンクし、共通 console/gfx core はリンクしていない。`kboot_console.c:27–30` は pm_log_write、`console_stream.c:7–12` は LAN/ESCC。`shell_putchar.c:5–9` は独自入口 |
| PPC 表示 | `platform/powermac/display_provider.c:6–19,52–55` は空の後端と空の列挙、display.c は空の機種口。現状の RH 表示は HAL を通らない段印専用 |
| root と入力 | `build/ram-root.mk:7–12` は motd と bin の ISO を kernel に埋める。`init/boot_root.c:13,28` は ISO root と /tmp ramfs。`init/ppc_kernel.c:314–324` は root→USB→sh。`input_boot.c:31–34` が LAN/USB 合流、`shell_boot.c:30–31` が接続/キー待ち |

依頼文・旧案との食い違いは **7件** (上表で訂正):
1. GfxBackend/GfxMode の定義場所は gfx/gfx.h ではない。
2. PC-98 通常 CUI に既存の「ソフト字形描画器」はない (TVRAM ハード表示)。
3. CONSOLE_DUAL の表は多出力の dispatch 表ではなく、CUI では更新もされない。
4. font_source は app/shell lease であり、resident console 用 owner と glyph API は未実装。指定範囲で fontmgr という実体も確認できず、FEP を無条件にリンクすれば解決するとはしない。
5. PPC は空の provider をリンクしさえすればよい状態ではなく、共通 console/gfx のリンク・CLIENT/台帳結線が欠ける。
6. RAM root の現レシピはフォント無し、ISO level 1 (`tools/mkroot_image.py:23–34,64`)。本案では短名で直接読める KCG ファイルを収録する。
7. `platform/powermac/rh_observe.c:62–79` は帯の頁だけの採取。全画面 alias は旧票の設計であって現物に実装済みではない。

## 2. 方式

### 共通の文字端末と出力先表

- `kernel/console/fbcon.[ch]`: セルの文字/属性・字形展開・反転・ソフトカーソル・行移動。機種別条件を置かない。
- `platform/powermac/console.c` の con_backend_* が共通 fbcon を呼ぶ。safe は準備済み target/font の状態で決め、con_out getter を描画の必須条件にしない。
- `gfx/gfx_console.[ch]`: HAL の面・同期・present を使う内部の console 用接続。fbcon は PA/VA、OF、TVRAM、I/O を知らない。
- `console_dual.c`、PC-98 の con_backend_* と display_dual は変更しない。con_out の既存8バイト配置・役割値・GUI/sink の門を維持。console_route と別の内部 dispatch 表は追加しない。
- PPC の `platform/powermac/display_dual.c` が OF head の TEXT を1行返す。root/font/面の準備完了後に live=1、未準備/LOST は live=0。列挙は描画・DDC・ログを行わない。
- 既存 console_dual の getter 更新契約は変えない。PPC の描画は con_backend に直接結線し、列挙表が CUI で更新されなくても使える。

```c
/* 新規内部契約の概形。既存 con_backend/GfxBackend/con_out の配置は変えない。 */
int fbcon_attach(struct fbcon *c, struct gfx_console_target *t,
                 const KcgModuleApi *font, unsigned top_pixels);
int gfx_console_open(u32 adapter, u32 head, struct gfx_console_target *t);
/* PPC con_backend_putchar -> fbcon のセル更新 -> read_ank/read_kanji */
/* -> HAL MONO/CPU 展開 -> dirty present */
```

- 論理カーソル・UTF-8・タブ/改行/BS は既存 console.c 一箇所。fbcon はストリームを独自に解釈しない。
- セルは半角8×16、全角16×16、寸法は floor(width/8) × floor((height-top)/16)。G4 は top=64 で80×26、QEMU は100×33。端数走査線には描かない。
- readchar はセル表から返し、reverse/cursor は元の字形を描き直す。カーソルの下のセルを破壊せず、scroll 前に消す。scroll は予約行を残し、空いた行を空白化する。
- 初段の TEXT は PPC の1面。PC-98 通常 CUI は gfx_init 前・DUAL=0・GF 無しでも現行 pc98_console_safe を通って TVRAM だけへ出す。
- 現行 GF GUI＋内蔵 TVRAM の二画面動作は維持。FB と GUI が同じ head を共有して同時に描くことは認めない。
- 複数 TEXT・console_route 内部表・PC-98 FB 選択・PLANAR4 は後続票。共通 fbcon を偽の PC-98 640×400/640×480 target でも実行し、機種に依存しないことを示す。

### OF の GfxBackend と HAL v1/v2

- `drivers/video/of_fb/backend_of_fb.[ch]` と `of_fb_v2.c` を追加。OF の探索・translate は platform 側、後端は検証済みの線形表示記述子だけを受け取る。
- 登録は既存 `display_provider_select/each` → gfx core → `gfx2_adapter_register`。**v1 の登録表＋v2 の同期 CPU 後端**とし、console の描画は v2。v1 と v2 を別の独立画面にしない。
- 固定 mode は採取した width/height/pitch と INDEX8 の1個。G4:640/480/768/8、QEMU は `-g 800x600x8` (pitch は採取値、800なら全体480,000B)。16/32bpp は文字端末対象外、既存帯診断の扱いを維持。
- CLIENT の native-format shadow を台帳に確保し、DISPLAY は全画面 alias に結線。v1_bridge は既存 alias 管理を使用。bb_base に VRAM を入れない。
- bb_format は既存 INDEX8 のまま。公開 surface format の TABLE=2/TEXT=3 を転用しない。GfxBackend/GfxMode/GfxBackendV2 の欄・幅・順序は変更しない。

| GfxBackend の口/欄 | OF 後端の実装 |
|---|---|
| probe/prepare/init/query | 全画面・shadow 準備の検証/状態保持/同期 target の結線/能力回答。GPU mode の書換なし。query は副作用なし |
| modes/selected_mode | 検証済みの固定 mode。u16 寸法の上限・pitch 積・format を検査 |
| bb_base/pitch/format/size | 台帳の CLIENT shadow。INDEX8 の形式と実サイズを返す |
| present_rect | clipping 後、shadow→alias を pitch 別に転送。生 index のまま転送。画面 padding・上の段印には触れない |
| enter/leave/shutdown | ソフト上の所有権/同期資源の管理。OF mode・CLUT は保つ。shutdown 後に live を落とし古い面へ描かない |
| set_palette | v1 は変更不可の no-op。query で変更能力を出さない。v2 palette は NOSYS |
| fill_rect/blit/shadow_* | ハード加速なし、NULL。共通 CPU 実装が shadow を更新 |
| direct_ok/direct_ops、put/mono/get/fill_rop | DIRECT を許さない。0/NULL |
| set_mode | 同じ固定 mode の検証のみ成功、違う mode は NOSYS。GPU レジスタ操作なし |
| dpms/cursor_*/vsync/page_flip/gamma/osri_show | NULL (未対応)。カーソルは共通 fbcon のセル描画 |
| v2/v2_mod | 組込みの of_fb_v2 / NULL。必須 hook は全て実装し、未対応は NOSYS を返す関数を置く |

- v2 caps: 1 head/connector、固定 scanout format、HW能力0、既存 CPU 2D の能力。heap_range=0、VRAM alloc/GART/emit/readback は NOSYS、CPU shadow は共通 sysmem 管理。
- v2 check=NOSYS、kick/stop は同期完了、非同期 fence は発行しない。fence_state は完了/失敗0、未発行 fence 待ちは既存同期後端と同じ拒否。enum_head/conn/mode は固定情報を返す。
- v2 mode_set は同じ形状の shadow を同期 scanout に attach するだけ。flip は同期転送のみ (vblank要求は NOSYS)、flip_done は未処理無し。edid/palette/gamma/ハードcursor/dpms/vblank は NOSYS。
- gfx_console は内部 owner の通常面を確保し lock/unlock・CPU MONO/COPY・同期 present に載せる。v1 の公開禁止 alias を直接 lock する例外は作らない。台帳・面の寿命/LOST の規則は既存通り。
- 8bpp は固定白黒 (白0、黒255)。raw_attr の表示/反転を白黒に落とし、カラー属性の色再現は約束しない。未知 CLUT の8bppは対象外、起動情報で Apple palette と確認したものだけ許可。
- v1_shim 採否 (N6): `gfx/v2/backend_reg.c:106` は v2=NULL で shim を選ぶが、`v1_shim.c:49–55` は面 s を捨て set_mode(id) だけを呼び、:58–66 は page_flip が NULL なら NOSYS。本案の通常 sysmem 面を同期 scanout に接続・転送する契約を満たさないため専用 of_fb_v2 を残す。列挙/CPU 描画だけなら shim で足りるが、その経路だけを合格条件にはしない。
- PPC の MMIO 書込み幅・sync は後端/arch の責務。整列32bit単位の転送を使い、端の語は shadow の隣接画素と合成し、padding/段印を越えない。VRAM 読みは不要。

### フォント供給と起動

- **2026-11-13 ユーザー決定**: 字形源は「KCG の口＋東雲16から作る .kcgfont」。GUI と同じ `JF-Dot-Shinonome16.ttf` の16px・1bpp EBDT ドットをビルドで取り出す。ipaexg、TTF の輪郭ラスタ、Rust のカーネルリンクは使わない。
- `tools/gen_font16.py:16–22` の KCG1 キャッシュ形式を復帰する。16Bヘッダ (サイズ/flags は LE)、94×94×32B 漢字＋同数 fetched＋256×16B ANK＋256B fetched。初段は非圧縮 flags=0、総計295,956B。読み込み側は flags≠0 を拒否する (bit0=LZ4 は既存定義だが初段は扱わない)。
- 生成器に EBDT 専用入口を追加し、既存 `tools/gen_g16.py:96` の strike 読出しを共用する。JIS→Unicode と ANK の ASCII/半角カナ対応は gen_font16.py:75–89 を利用し、16×16/8×16 へビットのまま配置、拡縮/閾値処理なし。EBDT 無し・セル外の画素はビルド失敗。
- 未定義 ANK は空白、未収録の有効文字は東雲の置換字形を同じ幅に配置する規則を生成時に固定。全角は左右2byte×16行、ANK は1byte×16行、MSB左。ANK/漢字の代表値と全収録字を独立に EBDT と比較する。
- `drivers/video/kcg/` に ROM 無し用ファイル本体を追加し、KcgModuleApi と同じ read_ank(u8,16B)/read_kanji(u16 JIS,32B)/stats/invalidate を供給。PPC は静的リンク、ROM 本体や x86 module loader を引かない。PC-98 ROM 経路と `/boot/kcg.mod` は現状維持。
- `kcg_load_font` は現状 `drivers/video/kcg/kcg.c:55–59` で NOSYS。PPC の起動時は内部 loader を使用し、既存公開 slot は NOSYS のまま維持する。復帰するのはファイル形式と ROM 無し機種の KCG 本体であり、実行中の公開フォント差替えは追加しない。
- `font_source.[ch]` に内部 `FONT_LEASE_RESIDENT=3` と kernel resident 専用 owner/世代を追加。app/shell と同じ VFS read_at/validate/close を使い、PROTECT/PIN と専用上限1は読み込み中だけ (FD の予算 APP 3+SHELL 3+RESERVED 7=13 の式は変えない、vfs.h:323-326・vfs_fd.c:57)。app quota・app終了・shell世代変更/終了から独立させ、公開 KAPI から resident id を利用できないよう kind を検査する。
- RAM root (埋込み ISO) の `/fonts/shino16.kcg` と `/fonts/shino16.txt` に派生物と東雲ライセンスを一組置く。拡張子 .kcg は ISO level 1 の8.3名であり、中身は .kcgfont と同一 hash。パスは [C4] の定数管理。長名 ramfs helper・TTF 同梱は不要。
- root 後に font_guard を通して resident lease を開き、ヘッダ/正確な長さ/予約値/flags/fetched を検証し常駐キャッシュへ読込む。BE 上も LE を明示 decode、部分読込みを公開しない。検証と読み込みが済んだら lease を close する (FD を占有し続けない)。通常描画は常駐字形のみ参照して VFS・割当を行わない。
- invalidate は ROM 再取得に転ばず常駐原本を維持して統計/派生状態だけをリセット。終了時は live を落として利用者を外し、キャッシュ解放 (lease は読み込み後に閉じ済み)。失敗時も部分確保と lease を回収し、IRQ/例外では読み込み/再ロードしない。
- **「配布しない」の変更箇所**: `build/assets.mk:87–91` の fixture 専用扱いを、旧 ipaexg fixture と製品用東雲派生物に分ける。東雲版の生成依存・PPC RAM root の収録 manifest を追加し、既存 x86 配布集合は維持。現行フォント仕様/配布文書にもユーザー決定と KCG1 用途を記録する (更新先は実装段で INDEX から特定)。派生物は git へ入れない。
- PPC root の前は既存段印/LAN/ESCC 診断。root→フォント常駐→HAL初期化→TEXT準備→clear/cursor→USB/sh の順。フォント欠損/破損時は理由を stream に残し FB を live にせず LAN/USB の既存経路を継続。

### 写像・失敗・LAN

- 旧確定版から引き継ぐのは全画面採取と入力待ち。KBOOT_SH/KBOOT_SHELL の FB slot は `FE100000–FE2fffff` (2MiB)、他の診断像は現行帯用 slot を保持。帯/full の二つの別名を同時に作らない。
- OF の address=0x9c008000 は VA。OF 切離し前に stride×height、offset加算、頁丸め、VA/PA終端の overflow、slot 隣接を検査し、丸めた全頁を translate。物理連続性とページ offset 一致を確認。
- G4 全体368,640B=360KiB/90頁、QEMU は pitch=800 なら480,000B、丸め483,328B/118頁 (offset=0、実際は採取 pitch で再計算)。alias は supervisor RW/I/G、既存 install_alias/observe_alias_safe/walk の全範囲検査を適用。
- 既存帯 capture が -1 なら現行 FAIL:KB:FB の停止を維持。非display=0 と fullだけの失敗は継続し帯またはFB無し。全画面失敗の descriptor を後端へ公開しない。
- top=64 の段印領域は RH 所有。fbcon の clear/scroll/present は下側だけ。後端初期化で全画面 shadow を無条件 present しない。
- KBOOT_SH の最初の USB キーを消費せず sh へ渡す。LAN未接続でも `pm_input_usb_has_key()` で進む現行待ちを維持。キー待ち案内は fbcon_attach 後に共通 console の通常出力で表示する。
- kboot_console/shell_putchar の同名代替定義を FB 像から外し、fd1 の pm_fd1_write は共通 console_write へ統一。console の既存 stream mirror を platform の LAN/ESCC に結線する。
- shell_putchar は現行共通版に mirror が無いので、既存 mirror 出力 helper の呼出しを足す (描画の門は増やさない)。一回の byte を一回だけ送る。診断 pm_log_write と端末送信を再帰接続しない。
- console_write の早期/描画不可でも stream が届くことを固定。LAN 切断・送信詰まりで画面入力を待たせず、既存の有限キュー・poll を維持する。

### 写像の参照資料と検証限界

- [PowerPC Open Firmware binding 2.1](https://www.devicetree.org/open-firmware/bindings/ppc/release/ppc-2_1.html) §4.1.1/4.2.6/6.1: OF address の意味、I/G と translate の契約を確認。機種固有の GPU mode register は扱わない。
- BSD 側の手本は既存 `platform/powermac/rh_observe.c:2` が挙げる NetBSD `sys/arch/macppc/dev/ofb.c:195–202` (firmware display address)。[上流ソース](https://github.com/NetBSD/src/blob/trunk/sys/arch/macppc/dev/ofb.c) の今回の取得は失敗し、当該行・版・ライセンス本文の再照合は未完了。C段着手前に固定版で照合する技術ゲートとし、確認済みとは数えない。
- Apple の当該 firmware/display のメーカー資料の追加照合もC段のHW3ゲートに残す。G4寸法・CLUTは依頼で与えられた実測値を根拠とする。GPL/QEMUソース・archive は参照していない。

## 3. 共通の場所への変更と本線への先行通知

| ファイル群 | 変更 | x86 生成物への影響 |
|---|---|---|
| fbcon (新)、PPC console/display_dual | 共通描画器、PPC からの結線 | x86 既定では fbcon をリンクしない。PC-98 con_backend/console_dual は維持 |
| console.c | shell_putchar の mirror を既存条件下で統一 | kernel/R1 が変わる。門の意味は維持、段 A の CUI 回帰を再実行 |
| font_source.c/.h、KCG ファイル本体・生成器 | resident lease、東雲 KCG1 読込み | 共通 lease コードの kernel 変更を計測。PC-98 ROM/module と公開 kcg_load_font の NOSYS は維持 |
| gfx_console (新)、gfx_core.c/.inc、display_provider.h | PC-98 固有名参照を provider に移し CLIENT/shadow を結線 | kernel/R1 と gfx 利用像が変わる。PC98 fallback/CLIENT の意味を維持 |
| mm/pgalloc.h、gfx/v2/gfx2_int.h・enum.c、公開 os32_surface.h | OF_FB の backend 値を末尾追加、max/name/分類/検査追従 | 公開 ABI 値の追加。生成元・SDK・部品の ABI/CRC と再packを照合 |
| build/assets.mk・ram-root.mk・root manifest | 東雲派生物を生成し短名で収録 | PPC 有効時だけ root 増加。x86 既定 root のバイト一致を要求 |

- **[ABI] 段 D**: `GFX2_KIND_OF_FB=6`、`LEDGER_SF_OF_FB=6`、`OS32_SURFACE_OF_FB=6` を各末尾へ追加。既存1〜5を動かさず、kind/ledger/公開値の一致を static assert と surface_query で検査する。format の INDEX8/TABLE/TEXT は不変。
- 手本は Trident=5 の追加 (637ab739「版据え置き」)。KAPI_SPEC.md:1595-1596「番号だけの追加は §3-2 と同じく版数を据え置く」に従い、KAPI の版は 84 のまま、gen_kapi.py・kapi_rust_gen.py は回さない。`sdk/include/os32/os32_surface.h` の末尾に `OS32_SURFACE_OF_FB 6`、exec/surface_query.c に STATIC_ASSERT、KAPI_SPEC:1595 の列挙に 1 行。上限の検査も追従: exec/surface_query.c:112 の `s->backend > LEDGER_SF_TRIDENT`、gfx/v2/gfx2_int.h:28 の `GFX2_KIND_MAX`、gfx/v2/enum.c:20 の names[]。関数追加はなく既存 slot・型配置は変えない。
- **構造体/vtable の形の変更は0件**。それでも公開定数の追加は ABI 作業として扱う。N8: D の受入は `sep5_provider_host`・`surface_query_host` を明記し、後者の LEDGER_SF 全値走査 (:77) を6まで拡張、旧5種の応答も維持する。
- 本線へ先に通知: stream mirror、resident owner、HAL/provider/ledger 結線、OF kind、x86 kernel と部品の再pack/CRC。console_dual の更新契約と PC-98 TVRAM の出力先は維持する。
- 共通変更を含むため「x86生成物不変」とは宣言できない。共有/公開値を変更する段ごとに、具体的差分・対象像・基点/候補の build_compare を示し、§7 の取り込み判断を求める。

## 4. 大きさの見積もり

KiB=1024B。以下はリンク実測前の予算幅、圧縮前。追加依存の GC 後 map を段ごとに測り直す。

| 対象像/項目 | .text/.rodata の増分 | RAM増分/根拠 |
|---|---:|---|
| PPC SH/SHELL: 共通console・fbcon | 12–24KiB | G4セル80×26×4=8.2KiB、QEMU100×33×4=12.9KiB、管理領域1–3KiB |
| 同: gfx core/v2/sync/台帳依存、OF後端 | 80–180KiB | shadow G4 360KiB、QEMU pitch=800で468.75KiB。別の全面作業面を持つなら同量を追加し上限に算入 |
| 同: font_source/guard/KCGファイル本体 | 8–24KiB | キャッシュ295,940B (約289KiB)、lease/管理領域1–4KiB。通常出力時の追加確保0 |
| 同: root 内東雲KCG1/ライセンス | F=295,956B＋ライセンス/ISO管理分 | 埋込みISOにFを1回収録。上のキャッシュと合わせ約578KiB＋管理分、TTF/ramfs複製なし |
| 診断専用PPC kboot | 原則0 | full FB/font/HALをリンクしない、帯だけ |
| x86既定 TVRAM | 共通provider/lease/mirror分を map/build_compare で計測 | 新たなFB面・字体キャッシュなし |

- PPC 追加コード合計約100–228KiB＋F。現状の小型 kboot からの増加を隠さない。
- alias 自体はRAMの画素バッファではない。90/118 PTE (上記条件) の実占有とHPT衝突容量を別に確認する。
- 実装リンク集合: console.c/con_sink/console_dual/fbcon/font_source/font_guard、KCGファイル本体、UTF変換/bootlog/stream依存、gfx_core/palette/v2/sync/台帳・面・owner依存。FEP・GUI shell・GPU moduleをダミーの成功stubで代用しない。

## 5. ファイル群ごとの段 (1段=1実装者)

| 段 | 担当ファイル群・独立受入 | 依存/並列 |
|---|---|---|
| A | x86 CUI 回帰 host 試験。実物 console.c/console_dual/PC98 con_backend を結線し TVRAM 限定を変異で固定、製品経路は変更しない | 共有変更より先に基準を固定 |
| B | font_source/guard・KCGファイル本体。resident供給/所有者試験と公開NOSYS維持 | A後、Cと並列可 |
| C | rh_observe.c/.h・kernel_boot.c/.h。全画面採取/slotと失敗継続 | A/Bと独立、既存alias試験も担当 |
| D | gfx_console・gfx_core群・provider・OF backend/ledger/公開定数とABI生成。sep5_provider_host・surface_query_host | A後。共有 gfx ファイルは一人が所有、clean→all |
| E | of_fb後端/v2・powermac display_provider/display/fallback・display_dual | C/D後、Fと並列可 |
| F | 共通 fbcon と PPC con_backend・セル/カーソル/scroll/INDEX8 展開。偽PC98寸法でも同じ実装を試験 | B/D後、Eと並列可 |
| G | gen_font16 の EBDT 入口・assets/ram-root.mk・短名manifestとroot試験、配布方針の文書追従。PPC の root に /sys/unicode.bin を収録 (漢字の道 unicode_to_jis → unicode_file_lookup、lib/utf8.c:214-218) | Bの形式固定後、E/Fと並列可 |
| H | ppc-kboot.mk・ppc_kernel・PPC stream/shell結線、console.c mirror とその試験。PPC に kernel/console/unicode_file.c をリンク | 全段後、リンク担当一人。診断像/SH/SHELLを全部buildし A 再実行 |
| J | QEMU INDEX8画素fixture/受入台本・G4台本・成果物サイズ比較 | H後。実機判定はPM/ユーザー |

各段の試験は同じ担当が同時に更新。統合担当は必要リンク一覧・固定hash・dep-dir を洗う。
段 I (PC-98 FB 選択)・複数 TEXT/console_route・PLANAR4 は後続票へ。PM が `docs/tasks/DEFERRED_TESTS.md` に範囲・初段を限定する理由・各機能の実装/取り込み前の受入関門を登録し、本票の合格に数えない。

## 6. 受入項目 → 試験・変異

| 受入 | 試験/観測 | 実行時REDにする変異 |
|---|---|---|
| R1 共通経路 | libc-free host: console.c→PPC con_backend→共通fbcon。偽PPC/PC98寸法の target で同じ描画器を実行 | 640幅固定、top無視、全角後半セル更新除去 |
| R2 字形・セル | host: 東雲EBDTとKCGのASCII/半角カナ/JIS期待mask、空白/BS/CR/LF/tab/折返し/予約行/最下行/2回scroll/反転/旧cursor消去 | 字形bit順反転、scroll行数±1、旧cursor再描画削除 |
| R3 HAL契約 | host: INDEX8・pitch余白・clipping・端語・面の寿命・LOST・再bind。列挙に1head、通常面の同期mode_set/flip、mode変更拒否、VRAMに直接CPUラスタしない | pitch=width、面接続除去、境界guard除去 |
| R4 写像 | libc-free OF stub: 全90/118頁 (採取値に従う)、末頁/不連続/offset/overflow/2MiB超/隣接slot/PTE RW-I-G、帯保持 | full bytesをband bytesへ、全頁translateを先頭だけへ |
| R5 フォント供給 | real VFS＋RAM root: 短名open/read_at・生成物hash、LEヘッダ/サイズ/短read/破損/欠損、app/shell終了後もresident生存、invalidate後の字形維持、close 後も字形が保たれる、IRQ描画にVFS呼出し0 | residentをshell ownerに、長さ/世代検証除去、invalidateで原本消去、flags の検査を外す |
| R6 QEMU画素 | mac99 `-g 800x600x8`、固定文字列→clear→cursor→scroll、QMP停止/PPMの独立座標・色・最下行比較。段印64行不変 | 白黒index交換、top=0、全画面aliasを帯へ |
| R7 二重出力・待ち | QEMU: fd1/kprintf/echoをLAN受信byteと画面で照合、切断後も画面更新、USBのみの開始キー保持 | mirror除去/二重呼出し、USB開始キー消費 |
| R8 PC98回帰 | A/Hのhost: gfx_init前・DUAL=0・GF無し・CUIの各条件で TVRAM のみ更新、FB/表示列挙I/Oは0回。GF GUI/98 TEXTとGUI clear門も実物で比較。NP21/W節目受入 | safeをTEXT行数に置換して起動ログ消失をRED、console.cのsink/dual門反転で描画先/回数不一致をRED |
| R9 G4単体 | OF4.2.1f2/ATY,R200、LAN無しで待機案内→USBでsh→コマンド/clear/長出力/BS、白黒・最下行・段印保持を写真とログで照合。LAN追加時も同じ出力 | 8bpp白黒交換はhostでもRED、最終合否は実機 |

- new CHECK dict に source/header/font fixture/build の全入力を宣言。変更/除去行の既存mutation anchorを tools/tests で検索し移設。各anchorは一意、正常対照PASS・変異版runtime RED。
- 実装時は依頼SHAのpreflight、対象像のbuild、追加/変更試験 --mutate、`python3 -B tools/checks/run.py --lint`。foregroundで完走。check-changed/全変異は回さず、取り込みcheck/buildはGitHub CI。
- 本票ではbuild・preflight --fix-fonts・変異・実機試験は未実行。PACK2 が設計差分のみ・リポジトリ書込み禁止と指定しているため。照合基点は r1 の361af19cを継承。独立レビューはPMの次の設計ラリーで行う。

## 7. 未決 (ユーザー判断: 2件)

1. **共通の場所・公開 ABI 値の末尾追加で x86 生成物が変わる段の取り込み**: 段 B/D/H など、対象段ごとに中身と基点/候補の build_compare (kernel/R1/SDK/部品CRC・root、理由付き差分) を示し判断を仰ぐ。TVRAM 限定の変異試験と必要ビルドの結果も添える。一括で未提示の変更の承認を求めない。
2. **G4 実機受入**: §6 R9 の実施当日にユーザーの時間を聞き、CI成果物を固定して画面＋USBだけのsh・写真/ログで判定する。

それ以外の実装の分割、定数名/ownerの具体値、配布文書の更新先、HW3資料照合、後続票と検証の調整は PM が決める。東雲KCG1の復帰と初段INDEX8は決定済み。
既存層を迂回する**新しい経路の提案は0件**。起動前の既存RH段印とstream診断は維持するが、文字端末には転用しない。

反論: 0件。N6 は shim の不足を確認したうえで専用 v2 を残す条件付き提案への回答。

## 確定 (2026-11-13、Opus 5.5 の第 2 回の条件付き確定、PM が反映)
- M1 (P1): 公開値の追加は Trident=5 と同じ「版据え置き」(KAPI_SPEC:1595-1596、637ab739)。版は 84 のまま、生成器は回さない。
- M2 (P2): 上限の検査 surface_query.c:112・gfx2_int.h:28 の GFX2_KIND_MAX・enum.c:20 の names[] を段 D で追従。
- M3 (P2): resident lease は検証と読み込みの後に close。PROTECT/PIN と上限 1 は読み込み中だけ、FD 予算の式は不変。R5 は close 後も字形が保たれること。
- M4 (P2): PM 決定 — PPC の root に /sys/unicode.bin を収録し unicode_file.c をリンク (段 G・H)。漢字が □ にならないように。
- M5 (P3): 読み込み側は flags≠0 を拒否、R5 に flags の検査を外す変異。


## 本線 PM の取り込みの条件 (2026-11-13)
1. 段 D (公開値の追加) は docs/KAPI_SPEC.md:1596 の backend の列挙と、os32_surface.h の照合の試験を同じ段で更新する。
2. 段 H (console.c の shell_putchar の mirror) は x86 の kernel R1 を変える。ユーザーの判断の後、取り込みの前に本線 PM の回で NP21/W の受入 (CUI の ver・shell の出力・CONSOLE_DUAL の 2 画面) を行う。統合の枝を渡すときにその旨を書く。
3. 段 A の試験は H より前に main に入れる。H の差分がその試験を通ることを取り込みの条件にする。
各段の取り込みは [skip ci] の枝で依頼。

## 段 C の取り込みで決めたこと (2026-11-13、PM)
- 実装の名前は設計と違えたものを正とする: `pm_rh_capture_full()`・`pm_rh_full_framebuffer()`・`pm_rh_discard_full()`、上限は `PM_RH_FB_FULL_LIMIT` (rh_observe.h)、結果は `enum pm_rh_full_result {OK, ABSENT, SIZE, TRANSLATE, ALIAS}`、理由は `boot_devices.fb_full_rc` (設計の fb_reason)。exec 像の窓は kernel_boot.h の `PPC_BOOT_FB_VA`/`PPC_BOOT_FB_LIMIT`。後の段 (E・F・H) はこの名前を使う。

## ユーザー決定 (2026-11-13、§7 の (a))
「本線の PM が認めたらバイナリが変わることを認めます」— 共通の場所・公開値で x86 の生成物が変わる段は、中身と build_compare を本線 PM に示し、本線 PM の承認をもって取り込む。

## 段 F の取り込みで決めたこと (2026-11-13、PM)
- 色の index は機種の CLUT による (G4 の OF: 0 白・255 黒、PC-98 の PEGC は別) ので、共通の fbcon には書かない。`fbcon_set_colors(c, fg, bg)` で platform が渡す (未設定の既定は 15/0)。PowerMac は fg=0・bg=255。

## 段 E・F のレビューで決めたこと (2026-11-13、PM)
- 8bpp の色は決め打ちしない: OF を切り離す前に screen の `color@` で index 0 と 255 の色を読み、明るい方を前景・暗い方を背景にする。読めない・同じ明るさなら INDEX8 の FB 端末は使わない。
- fbcon のカーソル (手本 yaft MIT・Flanterm BSD-2 に照らした、ユーザー指示): 非表示の字の上でもカーソルは独立に合成、表示位置は最終列に制限、変化の無い更新は描かない、スクロールは送り量を 1 回で処理し描いた内容の表と比べて変わらないセルは描かない、全角の右半分では左右 2 セルにカーソル。

## 段 H の取り込みで決めたこと (2026-11-13、PM)
- QEMU の OpenBIOS は screen の `color@` に応えない (error ffffffdf)。色の決め方を 3 段にする: (1) `color@` で index 0・255 が読めれば今までどおり明るい方を前景 (CLUT は変えない、G4)。(2) 読めなければ OF の切り離し前に `color!` で index 0 = 黒 (0,0,0)・255 = 白 (255,255,255) を書き、前景 255・背景 0 (理由の値を fb_color_rc に別に残す)。(3) `color!` も失敗したら INDEX8 の端末は使わない。
- SH/SHELL 像のヒープは台帳から 1 MiB (字形の常駐 約 296KiB が固定 192KiB に入らない)。確保に失敗したら既存のヒープ。
