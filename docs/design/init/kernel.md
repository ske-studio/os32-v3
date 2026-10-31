# init/kernel.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `init/kernel.c`

**`GFX=geforce` は `GUI≠1` のとき許す**（F5・N4）。起動予約前に既存の設定読取りで判定し、GUI=1 なら拒否して pc98 を選ぶ。GUI 省略時は既定値 0（`O/init/kernel.c:521`）。最初の `gfx_init`（GUI または CUI の描画アプリ）で、承認待ち無しに devinit を一度行う（N5、`O/gfx/gfx_core.c:960,1014`）。

## 内部設計と契約 (D1)

- root の `vfs_mount("/", boot.root_dev, boot.root_fs)` と panic/halt は kernel に残す。HostDrv は root 成功後だけ呼び、検出なし/HDRV --、mount 失敗/HDRV NG と継続を維持。
- CD load → kcg_module_load → 子マウント → ROOT_READY、kdebug_load/自己試験/exec の位置は動かさない。

**lan_module_load と PCM の load は kernel.c の今の位置に残す**
palette_init・bootinfo_report/_image・HEAP と "MOUNT..." の表示は wrapper の外 (kernel.c) に残す。

子マウントの CD の判定は文字の判定 (`dname[0..1]=="cd"`) のまま残し、引数を root の名前に変えるだけ。

2. coreのFAT/FDCまたはIDE/ext2でrootを成立させ、PG/台帳/DMAを準備する現行順を保つ。root後にUnicode → SQLite → settings close → FEP → SERVICES_READY。起動部品は `/boot` から読みPAIR必須でカーネルと対に配る。
3. ATAPI/iso9660はFD/HDD root上からPOST_ROOTで読み、cdinst・USER_READYより前に登録する。MINIMALでもCD読取を残す。登録後に `/cd0` 自動mountとloop/hd上ISOのiso9660試行も実行する。CDはインストール元だけでrootにせず、FEP/settings/不変RO SQLite許可を接続しない。
4. HostDrvはcoreの補助マウントを保ち、hsync bootが途中で止まっても `/host` は残る。gfxの識別glue/永久予約はcore、最初のAS前に共通PDEとBBを準備し、probe/enableはGUI境界。通常部品の登録 → splash (core) → rshell/SerialFS → shellの順は維持する。

補助はすべて元の翻訳単位のstatic。
例外・戻り値・I/O・ログ・初期化順序を変えない。

`static void kernel_shell_loop(void)`
boot globalsは元位置、mem_kb/boot_drive依存の初期化はmainに残す

### T9. CUI シェル ⇄ gshell の切替 — 2026-09-05 追加

shell.bin と gshell.bin はどちらもシェル帯 (0x300000, Level 1) に常駐し、**同時には存在
しない**。切替は再起動ではなく**カーネルのシェル起動ループでの入れ替え**で行う:

> [要確認] 原文のシェル帯 0x300000 は旧配置。現行 include/memmap.h の MEM_SHELL_LOAD_ADDR は 0x400000。

- 切替のたびにシェル帯は作り直されるので、退避・復元は無い。CUI シェルの履歴・環境変数は
  `.profile` から再構築される (v1 の割り切り)。

- init/kernel.c:590 の予約、:640 の prepare は :642 の cirrus_module_load より先。未登録 Cirrus（drivers/video/cirrus/cirrus_module.c:16）は modes=NULL のまま skip し、probe/enable を追加しない。

> [要確認] 現行 init/kernel.c は gfx_boot_reserve() → gfx_prepare_backend() → cirrus_module_load() の順。原文の旧行番号は写していない。
