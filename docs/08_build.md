## 第8部 ビルドシステム (GCC/NASM/Make)

### §8-1 ビルド手順

```bash
# 全体ビルド (カーネルおよびプログラム, 統合環境イメージ images/os32_boot.d88 等を生成)
make all

# パッケージ生成 + ISOイメージ生成 (CDインストーラ用)
make iso

# 掃除
make clean
```

#### ビルド前提: 日本語フォント (IPAex) は初回ビルド時に取得する

`ipaexg.ttf` / `ipaexm.ttf` (IPAex フォント Ver.004.01) は**リポジトリに含めない** (ユーザー決定
2026-09-30)。`make all` は `assets/fonts/ipaexg16.kcgfont` を作るために `assets/fonts/ipaexg.ttf` を
要求し、無ければ `tools/fetch_fonts.py` (`build/assets.mk` の規則) が IPA (文字情報技術促進協議会) の
公式配布 `https://moji.or.jp/wp-content/ipafont/IPAexfont/IPAexfont00401.zip` (SHA-256
`bcf8374a…7974b`、スクリプトに固定) を取得して 2 本を `assets/fonts/` に展開する (zip と各 ttf の
SHA-256 を照合。第三者ミラーは使わない。プロキシは `https_proxy` 等の環境変数)。
**取得の前に IPA Font License Agreement v1.0 への同意が要る**:

- 端末から `make all` または `make fonts` を打つと、同梱のライセンス全文
  (`assets/fonts/IPA_Font_License_Agreement_v1.0.txt`) を標準出力に出して `同意しますか [y/N]` と聞く。
  `y` 以外なら rc=2 で止まり、最後に案内が 1 行出る。
- 端末が無い (CI・パイプ・`</dev/null`) ときは **`OS32_ACCEPT_IPA_LICENSE=1`** を付けたときだけ同意と
  みなす。無ければ rc=2。GitHub Actions (§8-6) はリポジトリ所有者が同意している前提でこれを付け、
  取れた ttf を zip の SHA-256 を key に `actions/cache` で持つ。
- 同意は `assets/fonts/.license_accepted` (日時とライセンス文の SHA-256。追跡しない) に記録され、
  次回は聞かない。
- 2 本が揃っていて SHA-256 が合えば、何も聞かず何も取らない (mtime だけ更新)。取り直しは
  `python3 tools/fetch_fonts.py --force`、オフラインのホストは手で取った zip を `--zip PATH` で渡す。
- **`make clean` (と `clean-assets`) は ttf も `.license_accepted` も消さない** — 同意を聞くのは初回
  ビルドの 1 回だけ。消したいときだけ `make fonts-clean` (ttf 2 本と同意の記録を消す)。
- ttf はゲストに配らない (`userland/deploy.yaml` にフォントの項目は無い)。ゲストが使うのは ttf から
  焼いた `ipaexg16.kcgfont` (`/sys/font/default.kcgfont`) だけ。再配布の条件は `assets/fonts/README.OS32`。

試験: `tools/tests/test_fetch_fonts.py` (`make check-tools-host` の 1 行。ネットワーク無し)。

### §8-2 ビルドパイプライン

```
=== カーネルビルド ===
boot/*.asm      →  nasm (-f bin / -f elf32)  →  boot/*.bin / boot/*.o
kernel/*.asm    →  nasm (-f elf32)           →  kernel/*.o
*/*.c           →  gcc (-m32 -ffreestanding) →  */*.o
                                               ↓
*.o + kernel/*.o → ld (-T os32.ld)           → build/out/kernel.elf (0x100000〜 + SQLite 0x200000〜)
                                               (+ build/out/kernel.map)
kernel.elf      →  objcopy (-O binary)       → build/out/kernel.bin (カーネル本体)
                →  objcopy (--only-section)  → build/out/sqlite.bin (SQLite拡張域)
                                               ↓
kernel.bin + sqlite.bin → mkvmkernel.py      → build/out/vmkernel.lz4 (LZ4圧縮カーネルイメージ)
                                               LZ4 高圧縮 (HC level 12、展開側は同じ形式で無変更)。
                                               合計が MAX_IMAGE_SIZE (boot/boot_defs.h、508KiB) を
                                               超えたら出力を消して失敗 (HDD ローダが読めない)。
                                               試験: make check-vmkernel-lz4-host
                                               形式は VK32 v2 (エントリごとの展開後 CRC32 +
                                               完全長 + ファイル全体の CRC32)。両ローダが
                                               全部検査して外れたら止まる。
                                               試験: make check-vk32-crc-host
kernel.elf のリンクに build/out/build_id.c   → tools/gen_build_id.py (コミット ID。中身が
                                               変わったときだけ書くので、組み直すのは
                                               build_id.o とリンクだけ)。試験: make check-build-id-host

※ カーネル関連のビルド成果物はすべて `build/out/` に集約される (`BUILD_OUT`、gitignore対象)。
  `tools/gen_unicode` の出力 `unicode.bin` も同ディレクトリへ移動される。

=== HDD デプロイ ===
boot_hdd.asm    →  nasm (-f bin)             →  boot_hdd.bin (512B IPL, LBA 0)
loader_hdd.asm + boot_main.c + ext2_mini.c + lz4_mini.c
                →  nasm + gcc + ld (-T loader.ld) → loader_hdd.bin (LBA 2-5)
build/out/vmkernel.lz4 → ext2 FS 内 /boot/vmkernel.lz4 に配置 (nhd_deploy.py)

=== FDD デプロイ (FAT12) ===
boot_fat.asm    →  nasm (-f bin)             →  boot_fat.bin (1024B FAT12 IPL)
loader_fat_new.asm →  nasm (-f bin)          →  loader_fat_new.bin
mkpkg.py --fd-args (中身 = CD の BOOT + MINIMAL、build/packages.yaml の fd:)
  → mkfat12.py --tree で FAT12 イメージを構築:
  /LOADER.BIN = loader_fat_new.bin (1.44MB は loader_fat144.bin),
  /VMKRNL.LZ4 = build/out/vmkernel.lz4, ほかは MINIMAL の各ファイル (8.3 名)
                                             ↓
                     images/os32_boot.d88 および .img、images/os32_boot144.img

=== 外部プログラム ===
userland/**/*.c → gcc -m32                   → *.o
                → ld -T sdk/link/app.ld      → *.elf (newlib-nano -lc -lgcc リンク)
                → objcopy                    → *.raw
                → sdk/mkos32x.py             → *.bin (OS32X ヘッダ付き)

apps/ と game/ は staged SDK (build/sdk/) だけを使い、それぞれの
Makefile が同じ流れを回す。OS のソースツリーは参照しない。

=== GUI シェルと共有ライブラリ (2026-09-06) ===
userland/gshell (Rust, cargo)  → libgshell.a
  + crt0 + libos32gfx (GFX_OBJ) → ld -T sdk/link/app_sys.ld → userland/gshell.bin (シェル帯 0x300000、make gshell)
userland/rust/libos32gui       → ld -T sdk/link/shlib.ld     → userland/libos32gui.elf/.raw
                → tools/mkshlib.py --api 42 → userland/libos32gui.shlib (/sys/lib、make shlib)
                   (make check-shlib = 番号表の突き合わせ。ジャンプ表は末尾追記のみ)
GUI アプリ      → libos32gui_stub (ジャンプ表への薄いスタブ) を静的リンク、libos32gfx は入れない
```

日常のターゲット (`make all` に含まれる): `kernel` `programs` `libs` `gshell` `shlib` `external`
(`apps` + `game`)。検査: `make check-fast` / `make check-changed` / `make check` の 3 通り
([§8-4 検査の 3 段](#検査の3段))。`emu_agent` (ローカル AI) の `make` は
許可リスト (`tools/emu_agent/agent.py` の `MAKE_TARGETS`) に載ったターゲットしか実行しない。
`make all` の最後に `build/out/deploy-set.json` を生成する。期待集合・allow-list・名札照合と外部対象外の扱いは [T2H_EXTERNAL_GATE](tasks/v3/T2H_EXTERNAL_GATE.md)。

**GitHub Actions** (`.github/workflows/check.yml`、os32-v3 で 2026-09-30 に作り直し): push / PR で、
クロスツールチェーンも rustc も無しで回せる検査だけを自動ゲートにする — KAPI 版番号の一致、
`sdk/kapi.json` からの生成物がコミット済みと一致すること ([ABI1])、`check-kapi-out`、CONSTRAINTS ⇄ CLAUDE.md、
`mkshlib --check`、GUI プロトコルの C ⇄ Rust 照合 (`check_gui_proto.py`)、`check-arch-asm` / `check-le-access`、
対応表 (`check-map` / `check-check-select-host`)、文書 (lychee のリンク・孤児・状態行・試験一覧)、
ne2000 リングと gcc + python3 だけで回るホスト試験 17 本。どの検査が CI に載っているかは
`build/out/TESTS.md` ([生成手順](INDEX.md#生成文書の見方)) の CI 列 (yml の `make <target>` 行から生成)。
残り (末尾で `i386-elf-gcc` を使うホスト試験、rustc が要るもの、`check-manifests` / `check-packages-host` /
`check-memmap` のように `make all` の成果物を読むもの) は `build.yml` の `make check-fast` が回す (§8-6)。

**コンパイラとフラグ** (実体は `build/config.mk`。ここは読むための写しで、値は config.mk が正しい):

| 対象 | コンパイラ | 主なフラグ |
|---|---|---|
| カーネル | i386-elf-gcc | `-std=gnu11 -Werror=implicit-function-declaration -Werror=implicit-int -Werror=vla -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector -O2` (言語指定は `C_STD` / `C_DIALECT_ERRORS`、機械の旗は `CFLAGS_MACHINE`) |
| ブートローダ (C) | i386-elf-gcc | 同じ言語指定 (`CFLAGS_BOOT`) + `-Os` |
| SQLite | i386-elf-gcc | **`-std=gnu89`** (`C_STD_SQLITE`) + 機械の旗 + `-Os -ffunction-sections -fdata-sections` (サイズ優先)。userland の SQLite 単体も gnu89 |
| userland・SDK の実装 | i386-elf-gcc | カーネルと同じ言語指定 + `sdk/link/app.ld` でリンク |
| 外部プログラム (apps / game / `sdk/example/hello`) | i386-elf-gcc | 各 Makefile の `-std=gnu89` のまま (公開 SDK ヘッダの C89 互換の検証例) |
| アセンブラ | NASM | `-f elf32` (カーネル) / `-f bin` (ブートセクタ) |

クロスコンパイラは `$CROSS_DIR` (既定 `/usr/local/cross`)。構築手順は §8-5。

インクルードパスは `Makefile` で細かく制御されており、基本的にソースファイルから他のヘッダディレクトリは `-I` によって自動解決できるため `#include "file.h"` で問題なく参照可能。

### §8-3 ディレクトリ構造

```
os32/
├── boot/           ブートローダー (16bit/32bit ASM + C)
├── kernel/         カーネルコア・メモリ等・割り込みルーチン群 (gui.c / shlib.c / sysconfig.c / ring3_entry.asm を含む)
├── drivers/        ハードウェアドライバ (kbd, rtc, fm, fdc, disk, ide, atapi, kcg, mouse, np2sysp, loop_dev, dev, wab_* (Cirrus / Xe10 グルー) 等)
├── gfx/            グラフィック HAL (gfx_core + backend_pc98 / backend_pegc / backend_cirrus)
├── fs/             ファイルシステム (vfs, ext2, fatfs, iso9660, hostdrv 等)
├── exec/           OS32X(外部プログラム) のロードと環境設定
├── kapi/           外部プログラム向け KernelAPI リダイレクタ
├── lib/            汎用ライブラリ (utf8, path, sqlite3, zlib, microtar 等。vendor したものは各ディレクトリの README.OS32 が出所とライセンスの正典)
├── include/        システム統合用共通ヘッダ群 (memmap.h, gfx_hal.h, wab_xe10.h 等)。io.h / cpu.h は原始命令の**契約**だけ
├── arch/           CPU 依存の実装 (x86/arch_io.h — 割り込み制御・CPU 停止・IDT ロード、x86/arch_cpu.h — CR3/CR0・リング降下、x86/x86_desc.h — GDT/TSS ロード)。`ARCH ?= x86` で選ぶ。足し方は arch/README.md
├── platform/       機種依存の実装 (pc98/platform_io.h — ポート I/O・I/O ウェイト)。`PLATFORM ?= pc98` で選ぶ
├── userland/       ユーザー空間 (shell/, gshell/ (GUI シェル, Rust), cmds/, system/, tests/, rust/ (libos32gui 等), lib/)
├── .github/        GitHub Actions (workflows/check.yml: 静的ゲート、workflows/build.yml: 本体ビルド + check-fast)
├── apps/           git submodule (ske-studio/os32-apps) — 標準アプリ。make external / make apps
├── game/           git submodule (ske-studio/os32-game) — 対戦スゴロク RPG。make external / make game
├── docs/hw/        PC-98 資料のローカルミラー (git 管理外、tools/sync_hwdocs.sh)
├── sdk/            配布 SDK (allocator/ (nano入力台帳・表示), include/, crt/, link/ (app.ld / app_sys.ld / shlib.ld), rust/, example/)、kapi.json と生成器
├── build/          モジュール化 Makefile 群 (config.mk, kernel.mk, programs.mk, libs.mk, deploy.mk, image.mk, sdk.mk 等) + リンカスクリプト
│   └── out/        ビルド成果物 (kernel.bin, sqlite.bin, vmkernel.lz4, unicode.bin, kernel.elf/.map)
├── assets/         データアセット (DB, 辞書, profile 等)
├── tests/          テストスクリプト
├── tools/          ホスト上でのイメージ生成・デプロイ・検査ツール (nhd_deploy, mkshlib, check_*, emu_agent/ (ローカル AI の実機操作), np21w_mcp/, np21w_ctl (NP21/W の停止・起動))
├── packages/       生成された .PKG (make packages)
├── images/         生成されたブートイメージ (make all / iso)
├── Makefile        マスタービルドスクリプト (build/*.mk を include)
├── .env            ローカル環境設定 (CROSS_DIR / NP21W_DIR / HOSTDRV_DIR)
└── docs/           仕様書ドキュメント群
```

### §8-4 ホスト側イメージ生成ツール

#### `tools/nhd_deploy.py` (現行の標準デプロイツール)
NHD HDDイメージの管理をホスト側で一元化するデプロイメントツール。`deploy.yaml` に基づき、カーネル・ローダー・全ファイルを一括デプロイする。

```bash
python3 tools/nhd_deploy.py init       # NP21/WからNHDをコピー+パーティション更新+フォーマット+マウント
python3 tools/nhd_deploy.py sync       # deploy.yaml に基づくフルデプロイ
python3 tools/nhd_deploy.py write-boot boot/loader_hdd.bin  # ブート領域書き込み
python3 tools/nhd_deploy.py sync-from-hostdrv  # HostDrv (C:\os32) から ext2 へ同期
python3 tools/nhd_deploy.py deploy     # ローカルNHD (build/nhd/os32.nhd、無ければ自動 pull) をNP21/Wにコピー
python3 tools/nhd_deploy.py copy userland/shell.bin  # 個別ファイルのデプロイ
make deploy                                                # HostDrv 同期 → ゲストで hsync (再起動不要)
# 他: mount / umount / ls / rm / mkdirs / format / write-boot
```
- 配備対象・ゲストパス・タグは層ごとの deploy.yaml で定義する
  (`build/core.yaml`, `userland/`, `apps/`, `game/`)。マージは `tools/deploy_manifests.py`
- ext2ファイルシステムへの書き込みはLinux loopデバイス経由
  (sudo NOPASSWD 推奨: `mount, umount, losetup, e2fsck, mkfs.ext2, mke2fs, cp, mkdir, rm`)
- `config.h` の `SYS_*` 定数と `deploy.yaml` のパスは必ず整合させること

> ⚠️ **NP21/W 実行中は `deploy` が反映されない**: NP21/W が os32.nhd を開いたままの
> 状態ではコピーが失敗またはサイレントに無効化される。**必ず
> `tools/np21w_ctl.py stop` → `make deploy-kernel` → `tools/np21w_ctl.py start --ini np21x64w.ini`
> の順で実行**し、デプロイ後は `ver` の Build タイムスタンプで反映を確認すること
> (POLICY_DEBUG.md §2 / §4-9)。

<a id="ビルドターゲット"></a>
Makefile ターゲットとの対応 (`build/deploy.mk`)。**このリポジトリで
ターゲット一覧の正典はこの表**で、他のドキュメントはここを指すこと:

| ターゲット | 動作 |
|-----------|------|
| `make fd144` | **1.44MB フロッピーイメージ** `images/os32_boot144.img` (生イメージ、1,474,560 バイト)。2HD の `images/os32_boot.d88` とは別物で、既定は 2HD のまま。2026-09-24 から `make all` も作る (中身が CD の MINIMAL と同じなので容量の検査を毎回通す)。票 [`tasks/realhw/TASK_FD144.md`](tasks/realhw/TASK_FD144.md) |
| `make deploy` | HostDrv (`C:\os32`) への同期 — 再起動不要 |
| `make deploy-kernel` | HostDrv同期 + HostDrv→ext2同期 + NHDコピー — **要NP21/W再起動**。名前に反して**カーネル単独ではなく一式** (ユーザーランド・`/sys` も NHD へ書く) |
| `make nhd-migrate-pt` | 旧配置の区画表を PC-98 標準配置へ + ローダ + カーネルを同時に (KAPI v64 への初回だけ、[下の節](#区画表の移行-v64))。NP21/W 停止中 ([D1]) |
| `make deploy-boot` | ブートローダー (loader_hdd.bin) をNHDブート領域へ書き込み |
| `make deploy-nhd` | deploy.yaml フルデプロイ + NHDコピー — **要NP21/W再起動** |
| `make prune-stale` / `make prune-stale-delete` | 配備先 (HostDrv + NHD) に残ったマニフェストに無い *.bin を一覧 / 削除。deploy 系は既定で削除まで行う (`NO_PRUNE=1` で一覧のみ) |
| `make apps` / `make game` | 外部リポジトリ (git submodule `apps/` = os32-apps、`game/` = os32-game) を SDK 経由でビルド。空なら `git submodule update --init` を促す |
| `make external` | 上記 2 つをまとめて。KAPI / SDK ライブラリ変更後に再ビルドする。ポインタ更新条件は下記参照 |
| `make clean-external` | 外部リポジトリの生成物を削除 |
| `make deploy` → ゲストで `hsync` | HostDrv 経由の配送 — 再起動不要。`hsync` は既定で `/sys` を外す (稼働中のシェル・共有ライブラリ)。入れ替えるときは `hsync sys` |
| `make nhd-pull` | Windows 側 NHD を作業イメージ `build/nhd/os32.nhd` に取り込む (フォーマットしない)。deploy 系は無ければ自動で pull する |
| `make nhd-init` | 初回セットアップ — **フォーマットするのでゲスト側データが消える** |
| `make nhd-mount` / `make nhd-umount` | 作業イメージの手動マウント・アンマウント |

ビルド側のターゲットは `make all` / `kernel` / `libs` / `programs` / `sdk` /
`apps` / `game` / `clean` / `clean-kernel` / `clean-libs` / `clean-programs`。
KernelAPI の構造体を変えたときは `make clean` → `make all` が必須
(古い `.o` が残ると ABI 不整合で静かに壊れる)。

#### T2h の試験専用 kernel と隔離 NHD

`make kernel-r1` は `OS32_R1_FIXTURE` を付け、object / 依存ファイルを
`build/out/r1/obj/`、像・map・`manifest.json` を `build/out/r1/` に作る。
製品の object と像は共有しない。`check-r1-fixture-host` は CPU 入口を模擬して
実台帳と停止経路を検査し、clean を挟まない両ビルド順で製品 hash が変わらないことを
確認する。両順ビルドは先頭 runner だけが `$TMPDIR` 内のソースの写しで行い、
その写しだけで `__DATE__` / `__TIME__` を `SOURCE_DATE_EPOCH` で固定する。

試験像だけにある BSS `r1_fixture_arm[7]` の各添字に
`R1_FIXTURE_ARM` (`kernel/r1_fixture.h`) を `emu_write_mem` で書くと一度だけ発火する。
1 は次の timer IRQ で割当て、2 は次の #UD で解放を要求し、`R1 context` で停止する。
4 は USER syscall 中の開いた V86 session の終了で #UD を起こし、
`exec teardown stopped` に至る。arm は停止前に消費し、未 arm / 不正値は無操作。
復旧型は 3 = V86 session 構築後の CPL0 #UD (K1)、5 = 子の終了時に生存親を毒化
(5a)、0 = PARKED を resume commit 前に毒化 (5b)、6 = CLIENT lease attach の
公開前に一度だけ FULL を返す。0 / 5 / 6 は同じ添字の `r1_fixture_id` と
`r1_fixture_generation` に対象 app ID と AS 世代を先に書き、最後に arm を publish する。
ID・世代不一致では arm を消費しない。0 は PARKED + `parked_from_wait`、5 は生存中の
RUNNING 親に限る。3 は USER syscall 中の開いた session に限り、終了中には発火しない。
host 試験は移譲後の通常文脈での解放・次起動、親の syscall 出口 kill、PARKED の abort 保持と
PD 読込み前 kill、隔離 owner の page 保持と `owner-exit` の pages / leftover、
lease 失敗後の描画停止と次の待機返却時の再 attach を検査する。
製品 KAPI には操作口を追加しない。実ゲストでの停止・復旧確認は PM の R1c で行う。

`nhd_deploy.py <command> --profile t2h` は remote を
`NP21W_DIR/os32_t2h_install.nhd`、local を `build/nhd/os32_t2h.nhd`、
mount を `/tmp/os32_t2h`、来歴を `build/nhd/os32_t2h.nhd.pulled` に固定する。
`OS32_NHD_REMOTE` / `OS32_NHD_LOCAL` / `OS32_NHD_MOUNT` / `OS32_NHD_STAMP` が
異なる名前を指定すれば拒否する。既定像への symlink / hardlink も拒否する。

各交換・復元は **stop → umount (残っていれば) → pull → copy または sync → deploy → start**。
停止・起動には `np21w_ctl.py` を使う ([D1])。隔離 NHD の上書きも各回 [D2] の対象。
`pull --profile t2h` は既存 local も必ず取り直す。
正常一式は `sync --profile t2h` (本体全層、外部 apps/game は H-7 で除外) で同期する。
`make deploy-kernel-r1 PROFILE=t2h` は試験像を隔離 local の `/boot/vmkernel.lz4` に
copy する段だけを担当する。続けて `deploy --profile t2h` で反映する。

`verify-set --profile t2h --set build/out/deploy-set.json` は h1 の期待表の全ファイルの
存在・size・sha256 を読み取り照合し、管理ディレクトリ (`/boot`、`/sys`、`/bin`、
`/sbin`、`/usr`、`/etc`) の余剰も列挙する。欠損・改変・余剰は非 0 終了。
allow-list の集合は `deploy-set.json` を正とし、`check: exists` の絶対パスを受け付ける。
完全一致の設定は存在だけを検査し、末尾要素の `*` は同じ階層の 0 本以上を許す
(例: `/var/log/*`)。再帰 glob と未知の検査種別は拒否する。
期待表にある `/home` 等の配備ファイルも照合するが、その周囲のユーザーデータは余剰にしない。
`generations-manifest.json` は配備集合ではないので渡さない。
ゲストが書いた後は **stop → umount → pull → verify-set**。
`np21w_ctl.py start` は起動前にこの作業木の t2h stamp を失効させる
(別 ini の起動も安全側に失効)。remote の hash が変わった場合も、pull を省いた照合・配備を拒否する。

<a id="検査の3段"></a>
#### 検査の 3 段 (`check-fast` / `check-changed` / `check`、2026-09-26)

依頼パックの事実は `python3 tools/check_select.py --pack --files <触るファイル…>` の出力を貼る。着地前に `python3 tools/test_changes.py --base <SHA> --pack <依頼パック.md>` で試験変更を確認する。
票の段の記録は `check-docs-status` が 10 行超・台帳 ID のない未実施/延期/未確認を警告する (`--strict` で失敗)。

`HOST32_RUNNERS` は既定 `native qemu`。対象試験の一覧は `make tests-inventory` で
`build/out/HOST32.md` に生成する ([生成文書の見方](INDEX.md#生成文書の見方))。
正常対照は指定した全 runner、変異は先頭の runner で実行する。native への自動 fallback はしない。

どれも `make all` の後に回す (成果物を読む検査がある)。規則と登録は
`build/checks.d/<検査名>.mk` に置き、`build/sdk.mk` が include する。
`CHECK_PAR_ORDER` の番号で従来の順序を保ち、`CHECK_PAR_TARGETS` を集める。

| ターゲット | 回すもの | いつ | 所要時間 |
|---|---|---|---|
| `make check-fast` | 全検査を変異なし | 作業中 | 304.24秒 (2026-10-04、-j4/qemu、build変更によるmake変種9回込み) |
| `make check-changed` | 当たった検査だけ変異込み、残りは変異なし。docs だけなら文書系のみ | 作業中の差分検査 | 選択集合に依存 |
| `make check` | 全検査を変異込み | 取り込み前に1回 | 967.50秒 (16分7.5秒、2026-10-04、-j4/qemu、レビュー修正後)。改修前の当日ログは12〜48分で条件差あり |

今回の条件・実測・RED/GREEN は [検査整理の記録](archive/v3/TASK_T2D_T2H_RECORDS.md#検査の仕組みの整理-ci-select) を参照。
`MUTATE=1` / `0` / `sel` と `MUTATE_TARGETS` で変異を切り替える。
recipe には `$(MUT)` / `$(MUTS)` を使う。HOST32 は `$(call host32_check,test_x.py)` の1行。
`check-c-dialect-host` のmake変種9回は、変異ありの場合、または Makefile・build の規則・
言語検査器が変わった場合だけ回す。実物の lint は各本体検査に集約し、ホスト試験はfixtureを使う。

**誰がいつ回すか** ([ROLES §0](tasks/agents/ROLES.md)): 実装者は依頼パックに列挙された `make check-<名前>` と新しい試験だけ。
`check-changed` / `check_select.py` は PM が列挙を作る・変更が広がったときに候補を出し直すための道具。
全体 (`make check`) は PM が取り込みのまとまりごとに統合状態で 1 回 — 修正のやり直しの中では回さない。
取り込みは `make all` → `make check` → rc=0 なら push (`make check` は各試験の正常の対照も回すので `check-fast` を兼ねる)。

**`check-changed` は作業中の近道で、取りこぼしの保証はしない。保証は取り込み後の `make check` が担う。**
変更は基点からHEADまでと、未コミット (staged/unstaged)・未追跡の和。
基点は `tools/check_map.yaml` の `base_refs` (main、origin/main、feat/gui、origin/feat/gui の順) に見つかる merge-base。
merge-base が HEAD と同じ作業枝では **HEAD** を使い、直前の取り込みを変更に数えない。
基準枝自身 (main を含む。origin/ 接頭辞を除いた枝名) の上では HEAD~1 を使い、直前のコミットを検査する。
`BASE=<ref>` で明示、`FILES="a b"` で選択を試せる (makeの型照合は基点が要る)。

- 変更なし → 全部を変異なし (`fast`)。
- `full:` または表に無い入力 → 全部を変異込み (`full`)。
- `build/kernel.mk` / `libs.mk` / `programs.mk` → 対応表の入力検査、broad の走査器と `artifact_readers`。
  ただし追加・削除行に `check-` / `MUT` / `HOST32` / `.ONESHELL` / `export` / `override` / `CHECK_PAR` があれば full。基点版なしも full。
- `sdk/kapi.json` → KAPI検査、両生成器の出力15ファイル (generation・link scriptを含む) と生成器を読む検査、および `artifact_readers`。
- 文書だけ → 当たった検査と `docs_always` (`docs`)。
- その他 → 当たった検査は変異込み、残りは変異なし (`sel`)。
  `notest` の md・tilemap 等も broad に当たれば、その走査器は変異込みで回す。
  broad の `**` だけの一致は「表に載っている」の判定から除く。

Makefile・その他のbuild規則を絞る型は `tools/check_select.py` の `TPL_*_RE` が正典。
makeを実行して判定せず、基点から**追加だけ**であることをLF単位で調べる。
新しい規則は末尾の塊だけ、既存recipeへの追加は規則直後の連続tab行だけ。
空行・コメント・条件・継続行をまたいでrecipeの持ち主を変える配置は拒否する。
許すrecipeは `python3 -B tools/tests/<file>.py` + 小文字の旗 + 末尾の `$(MUT)` / `$(MUTS)`、
またはHOST32マクロの1行。scriptは木と当該検査の表に必要。
`.PHONY: check-<新名>` は、その新規則のrecipeの**後ろ**だけ許す。
既存規則とrecipeの間への挿入、`.PHONY` の後のtab行は拒否する。
分割ファイルの追加は名前と `CHECK_PAR_ORDER += <番号>:<検査名>` の一致を確認する。
既存の登録番号・位置の変更、規則の削除・変更、未知のmake構文はfull。
旧形式の単一リストも、固定マーカーによる位置と名前集合の比較を保つ。

**新しい試験の足し方**:

1. `build/checks.d/check-<name>.mk` を追加する。登録と規則はこの1ファイルに置く。
   `CHECK_PAR_ORDER += 133:check-<name>`、規則、recipe、最後に `.PHONY` の順。
2. `python3 tools/check_select.py --suggest check-<name>` を下書きに
   `tools/check_map.d/check-<name>.yaml` を追加する。手で列挙するのはソース・試験・データ。
   **ヘッダは手で列挙しない**。検査時の gcc -MM と静的includeの保守的な和から補う。
   gccで処理できないターゲット専用TUや非活性の条件分岐も、既存の静的include走査を残して拾う。
3. `make tests-inventory` で `build/out/` の試験表と runner 一覧を確認する (コミット不要)。
4. `make check-map` と関連ホスト試験で確認する。`check-tests-inventory` は登録・入力対応を照合し、生成本文の鮮度は照合しない。

全体設定 (`base_refs/ignore/full/docs_only/broad/docs_always/notest/artifact_readers`) は `tools/check_map.yaml`。
lint は「手書きの一覧 + 自動ヘッダ依存」とrecipeからの入力を照合し、ソースの漏れ、古いglob、
列との不一致、試験が読む入力へのnotest指定を拒否する。`--inputs` はヘッダ込み、`--suggest` はヘッダを除く。
入力の組み立て方やコンパイル条件を完全には解釈しないため、動的な走査には広いglobを残す。
選択で省くのは主に変異で、selでは正常試験を全て実行する。

- `tools/check_tree_unchanged.py` の番人 ([POLICY_DEBUG §4-40](POLICY_DEBUG.md)) は 3 通りとも段の前後で回る。
- **変異試験は実物のソースを書き換えない** (2026-09-26〜)。変異は `tools/tests/mutpar.py` の
  `mutant_tree` / `build_in_tree` / `run_script_in_tree` で一時ディレクトリの写しの木 (変異を当てるファイルと
  gcc -MM の依存は実体、残りは symlink) に当て、変異なしの写しが GREEN であること (`CONTROL`) を
  先に確かめる。新しい変異試験もこの作りにする — 実物を書き換える試験は列に足さない
  (票 [TASK_CHECK_MUT_PARALLEL](archive/tools/TASK_CHECK_MUT_PARALLEL.md) §5)。

<a id="kapi-v63-移行"></a>
#### KAPI v63 への移行 (データ欄の固定配置、票 [TASK_KAPI_DATA_FIELDS](archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md))

v63 で KernelAPI のデータ欄を 0x4B8 に固定し、OS32X ヘッダを v3 にした
([KAPI_SPEC.md](KAPI_SPEC.md) §4-0)。**v62 以前のバイナリ (アプリ・常駐シェル・
`libos32gui.shlib`) は一度だけ全部断られる**ので、初回は次の順で入れ替える:

1. `make clean && make clean-external` — 古い `.o` は crt の `kapi` の改名
   (`os32_kapi_v63`) でリンクが落ちるが、`.bin` は残るので必ず消す
2. `make all external` (実機向けは `make fd144` も) — 全部ヘッダ v3 で作り直す
3. **NHD は NP21/W を止めて一式** (`make deploy-nhd`、カーネル・`/sys`・shlib・
   userland をまとめて。順序は問わない)。実機は **FD / CD を入れ直す**
4. HostDrv + `hsync` だけでは移れない — 旧 `hsync` は名札の `kapi=` を見ないし、
   `/sys` (常駐シェル) が旧いまま新カーネルに載ると起動時に止まる
   (`FATAL: shell.bin: rebuild required (KAPI data layout)`、FDD の shell があればそちら)

**v64 以降は「カーネルを先、ユーザーランドを後」** (ユーザー決裁 2026-09-24)。
データ欄が固定になったので、新しいカーネルは古いユーザーランドをそのまま動かせる。
逆 (新しいユーザーランド + 古いカーネル) は、`min_api_ver` (exec と shlib ローダ) と
`hsync` の「配備物の版 > カーネルの版」の拒否 (`reason=kapi_newer_than_kernel`) が止める。
shlib ローダが要求版で断ったときは、GUI を選んでいても CUI shell で起動し
`GUI shlib: needs a newer kernel -> CUI shell` (カーネルを先に更新する案内) を出す —
配置違いの `rebuild required (KAPI data layout)` とは直し方が逆なので案内を分けてある。

<a id="区画表の移行-v64"></a>
**v64 の区画表の移行 (NHD、票 [TASK_HDD_INSTALL](archive/realhw_v21/TASK_HDD_INSTALL.md) N3)**:
v64 のカーネルとローダは区画表 (LBA 1) を **PC-98 標準配置**でしか読まない
(開始 = +8/+9/+10-11。2026-09-23 までの OS32 は +6/+7/+8-9 の独自配置で書いていた)。
旧配置の NHD を新しいカーネルで起動すると `/` (hd0) がマウントできず、新しいローダは
`No OS32 partition in LBA 1` で止まる。**`make deploy-kernel` だけでは移行しない**
(カーネルと ext2 の中身しか替えない)。NP21/W を止めて ([D1])、次の 1 操作で
区画表・第二段ローダ (LBA 2〜17)・`/boot/vmkernel.lz4` を**同時に**入れ替える:

```bash
make all                      # boot/loader_hdd.bin と build/out/vmkernel.lz4
make nhd-migrate-pt           # = python3 tools/nhd_deploy.py migrate-pt (push まで)
```

`migrate-pt` は空でない項目が**ちょうど 1 つ**の OS32 区画 (sid 0xE2) で、旧配置で読んだ
範囲の先頭に ext2 があり FS が区画に収まるときだけ、**同じ開始 LBA・長さ**を標準配置で
書き直す (カーネル → ローダ → 区画表の順、区画表は読み戻して比較)。それ以外は NHD を
1 バイトも変えずに断る。既に標準配置なら何も書かない。`--no-push` で NP21/W 側へ送らずに
止められる。確認は起動 → `/` のマウント → 既存ファイルの md5 (受入 H2)。
CD / FD のインストーラ (`cdinst` / `install`) は段 2 から標準配置・BIOS 幾何で書く。
空の hd0 か OS32 の区画 1 つ (旧配置の 8/17 の NHD を含む) を**作り直す**ので、旧配置の NHD は
`migrate-pt` の代わりに入れ直してもよい (中のファイルは消える)。規則は
[TASK_HDD_INSTALL](archive/realhw_v21/TASK_HDD_INSTALL.md) の「段 2 の実装メモ」。

- **旧配置の NHD には下の HostDrv の手順 (`make deploy` → ゲストで `hsync boot`) を使わない。**
  カーネルだけが v64 になり、次の起動で `/` がマウントできない (ローダは旧いままなので起動は
  進み、カーネルが `[EXT2] hd0: partition table is in the pre-v64 OS32 layout; migrate it
  (host: make nhd-migrate-pt) or reinstall` を出す)。ホスト側の `deploy` / `deploy-kernel`
  (`sync-from-hostdrv`) / `deploy-nhd` (`sync`) は、ローカルの NHD が旧配置でこのツリーの
  KAPI が v64 以上なら**配らずに断る** (`nhd_deploy.py` の `legacy_pt_guard`、`--force` でも
  通さない)。「旧配置か」は自動で移行できるかと**別に**判定する — 旧配置の OS32 項目に別の区画が
  並んでいて `migrate-pt` が断る NHD も断る (その理由も出す)。OS32 の項目が無い・どちらの配置でも
  読めない NHD も断る。通すのは **NHD のヘッダが無いファイル** (警告を出す) と**ファイルが無いとき**だけで、開けない・読めない・切り詰められた NHD や、移行の可否を調べる途中の例外 (OSError を含む) は断る。ただし **`deploy` (NP21/W の NHD を丸ごと上書きする push) は、ヘッダの無い・0 バイトの NHD も断る** (壊れたローカルの像で NP21/W の NHD を潰さない)。ローカルの NHD の読み取りの失敗 (OSError、旧配置の移行の可否を調べる途中のものを含む) は版に関係なく、`deploy` / `sync` / `sync-from-hostdrv` / `migrate-pt` のどれでも断る。
  ローカルの NHD が無いときは、取り込み (`ensure_local_nhd`) の**後・マウントの前**にもう一度見る。
  `hsync` はゲスト側なので止められない — 先に `make nhd-migrate-pt`。
- **v64 の `format N` は、区画の位置を BIOS 幾何で決められないと書かない** (`EXT2_ERR_INVAL`、
  `[EXT2] format: no BIOS geometry`)。当たる場面: DA 80h の HDD から起動して `format 1`
  (HDD ローダは自分の DA しか AH=84h を聞かない)、段 0 より前のローダ (0x7E00 のブート情報域を
  書かない) で起動した標準配置の NHD。FD から起動し直せば 80h / 81h の両方が得られる。
  マウント (読むだけ) は IDENTIFY の幾何でも通る。
- **逆の組み合わせ (v63 以前のカーネル + 標準配置の表) も危険**。旧カーネルは +6〜+9 を
  開始と読むので、標準配置の OS32 項目 (1632 = シリンダ 12) を **LBA 12** と解釈する。
  そのカーネルで `format 0` を打つと LBA 12 から書き、第二段ローダ (LBA 2〜17) と ext2 を
  壊す。移行した NHD・`hdprep` した HDD を旧カーネルで起動しない (FD 起動の FD も v64 に
  揃える)。

HostDrv 経由 (NP21/W を止めない) で v63 以降の稼働機を v64 以降へ上げる手順:

1. ホストで `make all external` → `make deploy` (HostDrv に一式と名札 `kapi_version=64…`)
2. ゲストで **`hsync boot`** — `/boot` だけに絞った同期は「版が新しい」の拒否から
   外してある (`NOTE: /boot だけの同期なので…` が出る)。配置違い
   (`kapi_layout_mismatch`) と名札の欠落・不正は `/boot` でも断る
3. 再起動 (`NOTE: /boot を更新した -> 再起動が必要`)
4. **`ver` の `API: v64` で版を確かめる** — 上がっていなければここで止まる
   (次の `hsync` は `kapi_newer_than_kernel` で断るので、壊れはしない)
5. `hsync` (ユーザーランド)、続けて `hsync sys` (常駐シェル・`libos32gui.shlib`)
   → shlib を替えたらもう一度再起動

`/boot` 以外 (`hsync bin`、全体同期、`/bootx` のような似た名前) は従来どおり断る。
**`make deploy-kernel` はカーネル単独の配備ではない** — HostDrv 同期の後に
HostDrv の中身 (ユーザーランド・`/sys` を含む一式) を NHD の ext2 へ書き、
NP21/W の停止が要る ([D1])。停止できるならこちらで一式を入れても順序の問題は出ない。

<a id="配備3経路"></a>
#### 配備 3 経路の使い分け (正典)

| 経路 | コマンド | 何が起きるか | 再起動 |
|---|---|---|:---:|
| **HostDrv** | `make deploy` | 成果物を `C:\os32` へ同期。ゲストは `/host` マウントで読む。速い反復用 | 不要 |
| **NHD** | `make deploy-kernel` | HostDrv 同期 + カーネル・プログラム・データを NHD の ext2 へ丸ごと書く | **必要** |
| **ブートセクタ** | `make deploy-boot` | `boot/loader_hdd.bin` を NHD のブート領域 (LBA 2〜17) へ。ローダを変えたときだけ | **必要** |

起動 FD 2 種は `make all` で生成だけ行い、`NP21W_DIR` へのコピーは `make deploy-fd` で明示する。

- **NHD への書き込みは NP21/W を止めてから** ([D1])。停止 → 配備 → 起動の順。
  `emu_pause`、breakpoint 停止、HTTP 無応答はプロセス終了の証拠にならない。
- **HostDrv だけでは検証にならない** ([V1])。ゲストの PATH は NHD の `/usr/bin` を先に見るので、
  古いバイナリが黙って動き、合格したように見える。
- ユーザーランドは `make deploy` → ゲストで `hsync` で再起動なしに差し替えられる
  (ホットデプロイの物理末尾 256KB 窓は 2026-09-09 に撤去)
  (カーネルと `/sys` は不可)。
- 配備マニフェストは所有層ごとに分かれている (`build/core.yaml`、`userland/deploy.yaml`、
  `apps/deploy.yaml`、`game/deploy.yaml`)。統合は `tools/deploy_manifests.py`。
  `host_only: true` の fixture (`/test/int80.img`、`v86 -b` 用) は HostDrv 同期だけに含め、NHD 同期と CD パッケージには含めない。
  マニフェストに無いバイナリは配備先で stale 化するので、`make deploy*` が
  `tools/prune_stale.py` で刈る (`NO_PRUNE=1` で一覧のみ)。
- **配備元には世代の名札が付く** (票 H4)。`make deploy` は全件成功の後にだけ
  `C:\os32\.deploy\manifest.txt` を書き、1 件でも失敗したら既にある名札を消す。
  中身は行指向の平文 (`format` / `build` / `generated` / `kapi` / `kapi_version` / `count`
  + `---` + 1 行 1 ファイル)。format=2 (KAPI v63〜) の `kapi=` は配備した OS32X バイナリの
  ヘッダ v3 から取る配置 (10 進) で、v3 でないもの・値の食い違うものが 1 つでもあれば
  名札を書かない (= 配備失敗)。
  `build` は `<短い SHA>`(+`dirty`) で、**「同じか違うか」を見るための名札。順序は表さない**。
  ゲストの `hsync` は起動時にこれを読んで 1 行目に `DEPLOY build=… count=… generated=…` を出し、
  `hsync --expect-build <ID>` は名札が違えば**1 件も書かずに**断る
  (`reason=build_mismatch`)。名札が無い / 壊れているときも「一致」とは扱わず、
  全体同期では断る (`manifest_absent` / `manifest_invalid`)。範囲を絞った同期
  (`hsync usr`) では表示だけして続ける — 名札はルートの世代を表すもので、
  絞った範囲の正しさは保証しないため。**読めて不一致と分かった場合は絞り込み
  でも断る** (「確かめた結果おかしい」と「確かめられない」は別)。
  **KAPI の門** (v63〜): 名札の `kapi=` がカーネルの配置と違う
  (`kapi_layout_mismatch`)、`kapi_version` がカーネルより新しい
  (`kapi_newer_than_kernel`)、名札が無い・壊れている・旧形式 (format=1) のときは、
  `--expect-build` の有無にも範囲にもよらず**既定で 1 件も書かずに断る**。
  越えるのは `--force-kapi` だけ (`-f` は同一判定の省略で別の意味なので開けない)。
  防ぐのは **「`make deploy` を忘れたまま `hsync` して、ゲストの新しいファイルを
  ホストの古いもので上書きする」** 事故 — 内容の違いは内容比較で分かるが、
  どちらが意図した版かは分からないため。詳細は `docs/manpages/hsync.1`。
  調査で名札を書かせたくないときは `hostdrv_deploy.py sync --no-manifest`
  (このときと `--tag` の部分配備では、**古い名札も消す**)。
  **名札が消えると、以後の `hsync` は KAPI の門で `manifest_absent` になり
  `--force-kapi` 無しでは 1 件も書かない** — 戻すには `make deploy` を通しで打つ。
  なお**コピーと名札の更新は原子的ではない**: 全件コピーの後・名札を書く前に
  ホストが落ちると、配備元は新しいのに名札は古いままになる。`--expect-build` は
  そこで断る (安全側)。復旧は `make deploy` をもう一度打つだけ。
- 環境変数: `HOSTDRV_DIR` (既定 `/mnt/c/os32`)、`NP21W_DIR` (既定 `/tmp/np21w`)。
- 判断と検証の進め方はスキル `os32-build-verify`、反映確認の手順は
  [POLICY_DEBUG.md §2](POLICY_DEBUG.md)。

#### submodule (`apps/` `game/`) の扱い

標準アプリとゲームは別リポジトリ (`ske-studio/os32-apps` / `ske-studio/os32-game`) で、
`apps/` `game/` に git submodule として置き、`make sdk` が作る `build/sdk/` を指してビルドする。

```bash
git submodule update --init     # 初回 / clone 直後
make external                   # apps + game (make apps / make game で個別)
```

- **KAPI を動かしたら `make external` で両方を再ビルドする。**
  再ビルドだけでは submodule のコミットもポインタも変わらない。submodule 側のソース変更を
  コミットして参照先が変わった場合にだけ、検証した組み合わせのポインタを親リポジトリで更新する。
  コミット・push はユーザーの明示的な指示がある場合のみ行う。
- **SDK のライブラリ (libos32gfx 等) を変えたときも同じ。** アプリは静的リンクなので、
  古い `.bin` は新しいバックエンド (PEGC の PACKED8 等) で #PF する
  (2026-09-06 に hello32 で実測)。

#### `tools/hostdrv_deploy.py`
HostDrv デプロイ先 (`HOSTDRV_DIR`, 既定 `C:\os32`) への差分同期。sudo 不要で高速。`make deploy` から呼ばれる。

#### `tools/mkpkg.py` と CD のパッケージ

CD インストール媒体 (`images/os32_install.iso`) の `.PKG` を作る。`make packages`
(`make iso` / `make all` から) が次を呼ぶ:

```bash
python3 tools/mkpkg.py --plan build/packages.yaml --output packages/ --base .
python3 tools/mkpkg.py --plan build/packages.yaml --check-plan   # 振り分けの検査だけ
python3 tools/mkpkg.py --list packages/NORMAL.PKG                # 中身の一覧
```

**中身は配備の正典 (`build/core.yaml` + `userland/deploy.yaml`) の `tags:` から決める。**
[`build/packages.yaml`](../build/packages.yaml) に書くのはタグとパッケージの対応と、
配備マニフェストに載らない媒体だけの物 (ブートセクタ、`/etc/settings.db`) だけ。
2026-09-24 までは一覧を別の YAML に手で写していて、配備 179 本のうち 27 本
(gshell / libos32gui.shlib / 既定フォント …) が CD に入っていなかった。

| パッケージ | 中身 (タグ) | cdinst の選択肢 | 2026-09-25 の実測 |
|---|---|---|---|
| `BOOT.PKG` | IPL + ローダ (`type: boot`、セクタへ直接書く。分割しない、無圧縮) | 全部 | 2 本 / 6,376 B |
| `MINIMAL.PKG` | `core` + `base` — **CUI のレスキュー兼インストーラ** (下)。カーネル、unicode.bin、shell、filetypes、settings.tsv、install / cdinst / hsync、less / grep / hexdump / cfg + 媒体だけの settings.db。**起動 FD と同じ集合** (下) | 1. Minimal 以上 | 13 本 / 861,245 B |
| `GUI.PKG` | `gui` — gshell、libos32gui.shlib、shlib を使うアプリ (filer / edit_gui / about)。shlib を使う試験アプリ (gui_demo / gdi_test / v12_api_test …) は `test` のまま DEBUG | 2. Normal 以上 | 5 本 / 430,530 B (2026-09-29、About を `ver` の中身に揃えた後) |
| `NORMAL.PKG` | `programs` / `docs` / `data` — コマンドとアプリ (sh / more / find / sort / head / tail / wc / tee / touch / sleep / diff / du / cal / man / sndctl / ime / v86 …)、man ページ、**既定フォント (`/sys/font/default.kcgfont`)**、FEP 辞書。**TTF は入れない** (2026-09-30: 日本語 OpenType は同梱しない、サブセット TTF は廃止) | 2. Normal 以上 | 95 本 / 6,569,090 B (2026-09-30、サブセット TTF 3,348,916 B と MGX 7 本を外した後。外す前は 10,130,774 B) |
| `DEBUG.PKG` | `test` — 試験バイナリと試験用データ | 3. Full | 62 本 / 1,272,396 B |

cdinst の展開は依存の順 BOOT → MINIMAL → GUI → NORMAL → DEBUG。Minimal だけの HDD で
`/etc/system.cfg` が `GUI=1` でも、gshell が無いのでカーネルは `gshell load failed -> CUI shell`
を出して CUI で上がる (`kernel/kernel.c` のシェル起動ループ。shlib が無いのは
`[shlib] ... not found (GUI shlib disabled)` で、拒否理由は立たない)。

**MINIMAL =「起動して、HDD に入れて、壊れたときに直して、残りを取ってこられる」
レスキュー兼インストーラ** (2026-09-25、ユーザー決定)。起動 FD と同じ集合なので 2HD に
収まる大きさに保つ。2026-09-24 までは「CUI のシェル + 基本コマンド」で既定フォント (184KB)
と一般コマンドも入れていて、2HD の FD の残りが 55KB しかなく、カーネルが約 4KB 増えただけで
FD が作れなくなった (`mkfat12.py` の「ディスク容量不足」)。

| 残す (MINIMAL) | 理由 |
|---|---|
| カーネル、unicode.bin、shell.bin (組み込みコマンド)、filetypes、settings.tsv / settings.db (FD は profile も) | 起動と設定 |
| install / cdinst | HDD へ入れる |
| hsync | 残り (NORMAL 以降) を `/host` から取ってくる。**同期元は HostDrv なので今は NP21/W の上だけ** (実機は SerialFS 待ち) |
| less / grep / hexdump | 壊れたファイルを見る |
| cfg | 設定を直す |

NORMAL へ移した物: 既定フォント、more / find / sort / head / tail / wc / tee / touch / sleep /
diff / du / cal / man / sndctl。残すコマンド・FD の profile・インストーラはこれらを呼ばない。

**既定フォントが無いとき** (MINIMAL の HDD と起動 FD):

- カーネルは起動画面に `FONT..NG` (赤) と `[KCG] kernel-init load failed: -1` を出して先へ
  進む (`kernel/kernel.c` → `kernel/boot_font.c`)。FD 起動では 8.3 の短い名前も試すので
  `[KCG] loading:` が 2 行出る。ファイルが開けない失敗は KCG キャッシュに触る前に返る
  (`drivers/kcg.c` の `kcg_load_font`)。
- テキスト画面 (シェル・kprintf) の文字は元から本体の CG (テキスト VRAM) なので変わらない。
  グラフィック画面に描く文字 (gshell / libos32gui の KCG 描画) はキャッシュに無い字を
  本体の漢字 ROM から読む — IPAex ゴシックの代わりに ROM の字形になり、初めて描く字は
  ポートを叩くぶん遅い。止まる経路は無い (2026-09-24 以前の FD はフォント無しで起動していた。
  `docs/tasks/realhw/TASK_FD144.md` §5-2)。
- cdinst の選択肢に「GUI はあるが NORMAL が無い」組み合わせは無い (2 = GUI + NORMAL が
  一緒)。FD → install.bin の HDD は MINIMAL と同じ (フォントも移したコマンドも無い)。欲しければ
  CD の Normal 以上か、**HDD から起動して `hsync` と `hsync sys` (+ リセット)** で入れる。
  全体同期の `hsync` はルート直下の `/sys` を既定で外す (稼働中のシェルと shlib) ので、
  既定フォント (`/sys/font/`) と GUI の共有ライブラリ (`/sys/lib/`) は `hsync sys` でしか
  入らない。**FD から起動して `hsync` は打てない** — 同期先 (宛先のマウント) がフロッピー
  なら何も書かずに `dest_on_fd` で断る (`userland/system/hsync.c` の `dst_on_floppy`)。

- **タグの規則**: 配備マニフェストの 1 行はちょうど 1 つのパッケージに当たるタグを持つ。
  `userland/tests/` 由来の行は必ず `test` (試験バイナリが NORMAL に混ざらないように)。
  パッケージに入れない物は `build/packages.yaml` の `exclude:` に**理由つきで**書く (今は無い)。
- **分割**: 1 本の項目数 (ファイル + ディレクトリ) が消費側の上限 128
  (`userland/lib/rt/pkg.h` の `PKG_MAX_ENTRIES`) を超えると、mkpkg が `NAME.PKG`,
  `NAME2.PKG`, … (最大 9) に分け、cdinst は連番を欠けるまで順に展開する。ベース名は
  ISO 9660 の 8.3 に連番 1 桁を足せるよう 7 文字以下。
- **パス長**: 格納パスは UTF-8 で 123 バイトまで (`/hd0` 前置 + NUL で 128)。
- **古い PKG**: `--plan` は出力先の他の `*.PKG` を消す (ISO は `packages/` を丸ごと焼く)。
- **apps/ と game/** (private submodule) は CD に入れない。`make all` では作られず CI では空なので、
  入れると媒体の中身が環境で変わる。これらは `make deploy` + `hsync` で配る。
- 検査は `make check-packages-host` ([`tools/tests/test_packages.py`](../tools/tests/test_packages.py)):
  振り分けの否定側、分割、実物の mkpkg で作った PKG と ISO の中の PKG を読み戻して
  配備の全ファイルが同じバイト列で揃うこと、cdinst.c のベース名と展開順との一致、
  GUI.PKG = `gui` タグ、既定フォントが NORMAL にあり MINIMAL に無いこと、
  起動 FD = BOOT + MINIMAL と FD の空きの下限 (下)。
- 試験用に `--defs` (ファイル一覧を直に書いた YAML) と `--name` (1 本) の形も残してある。

##### PKG の LZSS データ部

`pkg.h` / `mkpkg.py` の PKG1 は、32 バイトのヘッダ、ファイル表、表の終端
1 バイト、データ部の順。データ部は表の順に全ファイルを連結したもので、
ヘッダの flags bit 0 が立つ場合だけ LZSS を使う。LZSS の先頭 4 バイトは
展開長 (little endian)。窓は 4096 バイトを空白 (0x20) で初期化し、書込位置は
4096 − 18 から始める。フラグは下位ビットから 8 トークン分を読み、1 は
リテラル 1 バイト、0 は参照 2 バイト (位置 = 第1バイト + 第2バイト上位4ビット
× 256、長さ = 第2バイト下位4ビット + 3、最大18)。最後は展開長で止める。

参照は **1 バイトずつ読み、出力と窓へ書き戻す**。参照元と書込先が重なる
場合は、その参照自身が書いたバイトも再利用する。窓はファイル境界でリセット
しない。圧縮器もこの逐次コピーと同じ値で一致を判定する必要がある。
`cdinst.c` は `pkg.c` に展開を委ね、ホストの `mkpkg.read_pkg` も同じ規則を使う。
`make check-lzss-pkg-host` は固定種 0〜299 (既知の失敗入力は種250、SHA-256 を
試験に固定) と窓・重複参照・末尾の境界を、Python と実物の32ビット
`pkg_parse` / `pkg_extract` で照合する。`MUTATE=1` では更新前の窓だけを見る
旧圧縮器を復元し、両方の読み手で破損が検出されることも確認する。

##### 起動 FD と MINIMAL

**起動 FD (2HD `images/os32_boot.d88` / 1.44MB `images/os32_boot144.img`) の中身は
CD の BOOT + MINIMAL と同じ集合。** `build/image.mk` は一覧を持たず、
`python3 tools/mkpkg.py --plan build/packages.yaml --fd-args --fd-loader <LOADER.BIN のもと>`
が `mkfat12.py --tree` の引数を出す (2026-09-24 まで `FDD_MIN_CMDS` を手で持っていて
CD の MINIMAL とずれていた)。違いは `build/packages.yaml` の `fd:` に理由つきで書いた物だけ:

| FD | HDD / CD の同等物 | 理由 |
|---|---|---|
| `/LOADER.BIN` (only) | BOOT の `loader_hdd.bin` (ブート領域) | FAT の IPL が読む 2 段目。2HD は `loader_fat_new.bin`、1.44MB は `loader_fat144.bin` |
| `/etc/profile` (only、`assets/profile_fdd`) | 無し (`assets/profile` は配備マニフェストに載っていない) | FD 用の PATH (`/bin:/sbin`) |
| `/VMKRNL.LZ4` | `/boot/vmkernel.lz4` | FAT ローダはルートから読む。install.bin が `/hd0/boot/` へ写す |
| `/sys/boot_hdd.bin` | `/boot/boot_hdd.bin` (BOOT) | install.bin が LBA 0 へ書く元 |
| `/sys/loader_h.bin` | `/boot/loader_hdd.bin` (BOOT) | 8.3。install.bin が LBA 2〜へ書く元 |
| `/etc/filetype` | `/etc/filetypes` | 8.3。シェルの filer は長い名前が無く、**`/etc` が FD のルートと同じマウントにあるときだけ**短い名前を読む (KAPI `sys_stat` の `st_dev` を `/` と `/etc` で比べ、`vfs_devname("/")` が FD。KAPI にパスから FS 種別を引く口が無いため) |

短い名前へ落ちるのは LFN の無い FS (FAT) の上だけ — HDD で正規名が欠けたり壊れたりしても、
FD ルートの上に HDD の ext2 を載せても、残っている短い名前を黙って掴まない (ホスト試験:
`test_packages.py` case 8、`test_sh_truncation.py` 25n〜25q)。既定フォントの 8.3 名
`/sys/font/default.kcg` (`kernel/boot_font.c`、そのパスを載せているマウントが FAT のときだけ
読む) も同じ作りで残してあるが、2026-09-25 にフォントを NORMAL へ移したので**今の FD には
載っていない** (FD に手で置けば読む)。**install.bin (FD → HDD) は
この表を逆に当てて正規名で写し**、ブート領域へ書く物と FD だけの物は写さない
(`userland/system/install.c` の `fd_renames` / `fd_only`。case 9 が packages.yaml と
突き合わせ、実物の install.c を回して HDD の集合 = MINIMAL を見る)。

FD 上のパスはすべて 8.3 に収まること (mkfat12 は収まらない名前を黙って切り詰めるので、
mkpkg が先に断る)。**`core` / `base` に足した物は FD にも入る** — 空きは 2026-09-25 に
**2HD 359KB / 1221KB、1.44MB 568KB / 1424KB** (MINIMAL をレスキュー兼インストーラに絞った後。
絞る前は 2HD が数 KB 溢れて作れず、1.44MB は 211KB)。2HD が先に尽きるので、MINIMAL に
足すときは `make all` の FD の行 (`クラスタ使用: … 残り …KB`) を見る。**空きが 64KB
(`tools/tests/test_packages.py` の `FD_MIN_FREE_KB`、2HD と 1.44MB の両方) を切ると
`make check-packages-host` が落ちる** — 溢れて `make all` が割れる前に、MINIMAL に足す物を
見直すか FD から外す物を決める合図。
以前 FD にだけあった試験用の `/bin/timetest.bin` / `/bin/pcmtest.bin` は外した (DEBUG の
`/usr/bin/time_test.bin` / `pcm_test.bin`)。検査は `make check-packages-host` の case 7 が
実物の 2 つのイメージを FAT12 として読み戻し、構成とバイト列で等しいことを見る。case 7b は
配布物の `os32_boot.d88` のセクタを並べ直して `os32_boot.img` (RAW) と同じバイト列かを見る
(各トラックの R が重複なく 1〜spt であることも)。

#### `build/app.conf` (OS32X ヘッダ設定)

`sdk/mkos32x.py` に渡す引数をプログラムごとに決めるデータファイル。`build/programs.mk` の
`userland/%.bin` ルールが `awk` で引き、キーはリポジトリルートからの拡張子なしパス
(例: `userland/tests/blit_test`)。`#` 始まりはコメント、行が無いプログラムは既定値。

| 列 | 意味 | 省略時 |
|---|---|---|
| 1 | プログラム名 (キー) | — |
| 2 | 要求 KAPI バージョン (`--api`) | 7 |
| 3 | ヒープサイズ (`--heap`)。`0` で mkos32x の既定 | 0 |
| 4 | `gfx` = 全画面 GFX の宣言 (`--gfx` → `OS32X_FLAG_GFX` = 0x0001)<br>`cui` = CUI 専用の宣言 (`--cui-only` → `OS32X_FLAG_CUI_ONLY` = 0x0010)<br>`launcher` = 起動要求者の宣言 (`--launcher` → `OS32X_FLAG_LAUNCHER` = 0x0020) | 無し |

4 列目は宣言ビットで、`gfx` / `cui` / `launcher` のどれか 1 つ (または省略)。

`gfx` は「このプログラムは画面を丸ごと取る」という宣言 (票 T8 D1a)。`gfx_init` /
`gfx_init_200` を呼ぶプログラム — 直接でも `tilemap_init` のようにライブラリ経由でも — に立てる。
`libos32gfx_attach` だけで gshell の面に取り付く GUI アプリには立てない。立て忘れると GUI 中の
起動で画面の所有権を取れず、WM が上書きするか カーネルが `gfx_init` を蹴る (蹴られたアプリは
そのまま畳まれる — 断っただけでは描画 KAPI で描き続けて GUI を壊すため)。

`cui` は「GUI から起動してはいけない」という宣言 (票 T8-2)。`v86_selftest` / `v86_disktest` /
`v86_boot` / `v86_boot2` を呼ぶ V86 / VDM 系に立てる。これらは CPL=3 のプログラムだが KAPI の
向こうで低位メモリ・BIOS・テキスト VRAM を丸ごと使うので、`--cpl0` の砦では捕まらない
(`userland/cmds/v86.bin` の flags は 0x0 だった = 受入 F5 の不合格)。GUI からの `exec_start` は
`OS32_ERR_INVAL` で断り、CUI からは従来どおり通す。グラフィック VRAM を直接書く検証用
バイナリ (`ring3_hello` / `ring3_fault` / `ring3_guard`) は app.conf を持たないので
`build/programs.mk` の explicit ルールで `--cui-only` を付けている。

`launcher` は「このプログラムは `launch_req` で WM に外部プログラムの起動を頼む」という宣言
(票 T9 D1a)。カーネルの要求表はこの宣言を持たない CPL=3 からの `launch_req` を `OS32_ERR_INVAL`
で断る (認証ではなく協調的な宣言)。端末 (`userland/tests/t5a_display`) と
`userland/sh` — 常駐シェルと同じソースを `-DSHELL_AS_APP` で 0x500000 にリンクした CPL=3 版 — に
立てる。GUI アプリの通常の起動経路 (`session_launch`) はこの宣言と無関係。

`gfx` / `cui` は `make check-manifests` がソースの呼び出しと突き合わせて検出する (§2b)。
`launcher` は同じ §2b が書式 (4 列目に置けるのは 3 つの印か省略) だけを見る。
`apps/` `game/` は staged SDK 側でそれぞれの `Makefile` が `mkos32x` を呼ぶので、
そちらの GFX プログラムには各リポジトリで `--gfx` を付ける。

#### C ソース検査 (clang)

`tools/clang_ast/` が `make -n -B all` の実際の翻訳単位・旗を取得し、
i386 向け libclang AST を作る。ビルド自体は引き続き i386-elf GCC。
ホストには `clang libclang-dev llvm-dev python3-clang` (Ubuntu apt) が必要。
全 TypeLoc を辿る C++ 補助器の組み立てには LLVM 開発ヘッダと `libLLVM`、
`libclang-cpp` が必要で、clang/libclang/resource header と同じ版を使う。
導入後はリポジトリのルートでロードと組み立てを確認する:

```bash
python3 -c 'import clang.cindex; clang.cindex.Index.create()'
PYTHONPATH=tools python3 -c 'from clang_ast.type_occurrences import visitor_binary; print(visitor_binary())'
```

クロス GCC を導入しない静的 CI では、C AST 検査の標準ヘッダを Ubuntu の
`libnewlib-dev` (`/usr/include/newlib`) から取る。クロス newlib があればそちらを優先する。
clang の解析失敗・不足入力は非0で終了する。`make all` の後に検査する。
実ビルドで選ばれたソースとそこから取り込むヘッダが対象で、未使用ヘッダ・
非選択の `#if` 分岐は対象外。旗の読み替え・判定・比較の詳細は
[TASK_CLANG_CHECKS](archive/v3/TASK_CLANG_CHECKS.md) §4・§6。

- `make check-p2v`: canonical type とマクロ展開で物理キャスト・物理引数、
  user/lease の V2P、関数内 CONST を検査。[C5] の file:function:reason は
  `tools/check_p2v_allow.txt` (65件) を引き継ぐ。
- `make check-c-dialect`: GCC の実効言語モード・拒否探りを保持し、内部の
  atomic/TLS/restrict/VLA/匿名メンバー/旧式定義は clang の型・宣言・診断で検査。
  公開 SDK は gnu89/gnu11 診断と libclang token でも確認する。
- `make check-arch-asm`: AST の asm template (マクロ・連結文字列も含む) から
  hlt/cli/sti を検査。arch/*/arch_*.h と ARCH-ASM-OK の例外を保持。
- `make check-le-access`: ext2 / ISO9660 / KCG / utf8 の9ファイルの
  pointee alignment が上がる cast を canonical type で検査。
  外部形式は `include/endian_le.h` の le16/le32 アクセサを通す。

#### `tools/audit_cast_align.sh`

同じ AST 判定で全実ビルド TU の整列要件が上がる cast を列挙する手動監査。
`make check` の外で、人が候補を仕分ける。解析失敗は非0、候補があっても終了0。

```bash
tools/audit_cast_align.sh kernel   # userland/・SDK 以外の実ビルド TU
tools/audit_cast_align.sh user     # userland/・SDK の実ビルド TU
tools/audit_cast_align.sh all
```

候補が出ても実際に壊れるとは限らない。仕分けの記録は
[archive/arch_port/M0_PORTABILITY_AUDIT.md](archive/arch_port/M0_PORTABILITY_AUDIT.md)。
旧5本 (正規表現版) は比較のあと撤去した (ユーザー決定 2026-10-01、git の履歴 `8612b06` 以前とリポジトリ外のバックアップに残る)。
重複していた `create_fat12_d88.py` は撤去。FD生成は `tools/mkfat12.py` を使う。

#### `tools/check_docs_links.py`
文書のリンク切れ検査 (`make check` の `check-docs-links`)。`lychee` (Rust 製のリンク検査器) を
`--offline --include-fragments` で呼ぶ薄い包みで、**相対パスの実在**と**見出しアンカーの実在**の
両方を見る。日本語の見出し (`08_build.md#配備3経路`) も GitHub と同じ規則で判定できる。
対象は `docs/**/*.md` (gitignore されたミラーの `docs/hw/` だけ除く) と `CLAUDE.md` /
`README.md` / `arch/README.md` / `platform/README.md` / `tools/tests/*_tdd.md`。
**`docs/archive/` は除外しない** — 完了した票を archive へ移したあとも相対パスが生きていることを
見るのが目的の 1 つで、移動で 1 段ずれるのはいちばん起きやすい壊し方だから。外部 URL は
見ない (`--offline`)。900 リンクで 0.03 秒。`lychee` が無い環境では `SKIP` と出して終了コード 0
(`cargo install lychee` で `~/.cargo/bin` に入り、包みが PATH に無くても探す)。

#### `tools/check_docs_orphans.py`
孤児文書の検出 — リンク切れの裏返しで、「**どこからも指されていない**」文書を挙げる
(lychee の守備範囲外なので自前、Python 標準ライブラリのみ)。`docs/INDEX.md` を唯一の起点として
相対リンクを推移的に辿り、到達できない `docs/**/*.md` を列挙する。`tools/tests/*_tdd.md` は票の
根拠なので索引から辿れる必要はなく、起点集合に `docs/tasks/**` と `docs/archive/**` の票を含め
(受入完了して archive へ落ちた票も票)、票が慣例どおり素のパスで書いた言及も参照とみなす。
索引に載せないと決めた例外は `docs/.orphans-allow` (1 行 1 パス、`#` コメント可) に理由つきで書く。

```bash
make check-docs-orphans        # 単体
```
2026-09-15 の棚卸し時点では docs 31 本 + TDD 記録 20 本が未参照だった (検査の不備ではなく、
票を書いて `INDEX.md` に載せ忘れた取りこぼしの実数)。そのあいだは門にすると通すために例外表へ
全部書き写すことになり `.orphans-allow` が「黙らせる表」に化けるので単体運用にしていたが、
索引を直して 0 になったので `make check` の列へ入れてある。

#### `tools/move_docs.py`

**文書を動かし、リポジトリ中の `.md` の参照を追従させる** (検査ではなく、手で回す道具)。
受入完了した票を `docs/archive/<領域>/` へ落とすときに使う。`git mv` だけでは指していた側の
相対リンクが黙って壊れ、`check-docs-links` が次に回るまで気づけない。

```bash
python3 tools/move_docs.py --into docs/archive/network docs/tasks/network/TASK_N0.md --dry-run
python3 tools/move_docs.py --map moves.tsv          # 1 行 "移動元<TAB>移動先"
python3 tools/move_docs.py SRC DST [SRC DST ...]
```

動かす一覧は道具の中に持たず、引数か TSV で外から与える。書き換えるのは 3 つの形だけで本文には
触らない — (1) `](相対パス#見出し)` と参照定義、(2) 地の文のルート相対パス言及 (票と
`tools/tests/*_tdd.md` が互いを指す慣例の書き方)、(3) 表示文字がパスそのもののリンクのラベル。
動いた文書自身の中のリンクは深さが変わるので全部引き直す。`--dry-run` で一覧だけ出せる。
運用 (何を落として何を残すか、落としたあとに守ること) は
[archive/README.md](archive/README.md)。実行後は `check-docs-links` / `check-docs-orphans` /
`make check-tests-inventory` (登録の照合) の 3 つを回す。

**`--ext` / `--rewrite-only`** — 追従させるのは `.md` だけではない。票の番号や設計の正典は
ヘッダやモジュールの先頭コメントに「仕様: `docs/tasks/…`」と書く慣例があり、文書だけ動かすと
そこが古いまま残る (2026-09-16 のアーカイブで 27 ファイル取り残した)。

```bash
python3 tools/move_docs.py --map moves.tsv --rewrite-only \
    --ext .c,.h,.inc,.asm,.py,.rs,.mk,.toml,.yaml,.json,.sh,.1 \
    --exclude docs/hw,lib/sqlite3,lib/microtar,lib/zlib --dry-run
```

`--ext` は書き換え対象の拡張子 (既定 `.md`)。並べた拡張子**だけ**が対象になるので、`.md` も
一緒に書き換えるなら明示して並べる。`--exclude` はリポジトリ相対の接頭辞で、取り込んだ第三者の
ソースを外す。`--rewrite-only` は `git mv` を行わず**参照の書き換えだけ**を行う (移動が済んだ
後に取りこぼした種類のファイルを追従させるとき。移動元が無く移動先があることを確かめてから
当てる)。`.md` 以外では (2) 素のパス言及だけを当て、Markdown のリンク規則 (1)(3) は当てない
(C の `tbl[i](x)` に化けて当たる余地を残さないため)。**文字列リテラルの中も区別せずに当たる**
ので、ヘルプ文に文書パスを埋めている場合は `--dry-run` の一覧で確かめてから走らせる。
コメントだけの書き換えなら `.o` は変わらないはずで、疑わしければ `git archive HEAD` で展開した
無垢な木と同じフラグでコンパイルして md5 を突き合わせる。

### §8-5 開発環境の構築 (クロスコンパイラ)

OS32 の外部プログラムをビルドするためには、標準Cライブラリ (`newlib` - `libc.a`) と GCCライブラリ (`libgcc.a`) を含んだ `i386-elf` クロスコンパイラ環境が必要です。

> ⚠️ **コマンド名は `i386-elf-*` 固定**: `build/config.mk` は `i386-elf-gcc` /
> `i386-elf-ld` / `i386-elf-objcopy` というコマンド名と
> `$(CROSS_DIR)/lib/gcc/i386-elf/13.2.0` というパスをハードコードしている。
> apt の `gcc-i686-elf` は `i686-elf-*` という名前になるうえ newlib を含まないため、
> **ソースからの構築が必須** (バージョンも 13.2.0 固定)。

#### ソースからの構築 (必須手順)

下の 4 段をそのまま実行するスクリプトが `tools/ci/build_cross.sh` にある
(tarball の sha256 検証と、出来上がりが nano 構成であることの検査つき。
`--src-dir` に tarball があればダウンロードしない)。GitHub Actions も同じスクリプトで
ツールチェーンを作る (§8-6)。

```bash
tools/ci/build_cross.sh --prefix $HOME/opt/cross --src-dir $HOME/opt/src --jobs $(nproc)
```

事前に必要な apt パッケージ:
```bash
sudo apt install build-essential nasm libgmp-dev libmpfr-dev libmpc-dev \
  texinfo bison flex python3-lz4 python3-yaml genisoimage
```

binutils 2.41 / GCC 13.2.0 / newlib 4.4.0.20231231 を以下の構成でビルドする
(インストール先の例: `$HOME/opt/cross`):

```bash
# 1. binutils
../binutils-2.41/configure --target=i386-elf --prefix=$HOME/opt/cross \
    --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && make install

# 2. GCC ステージ1 (libgcc まで)
../gcc-13.2.0/configure --target=i386-elf --prefix=$HOME/opt/cross \
    --disable-nls --enable-languages=c --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc

# 3. newlib — ★nano構成必須★
../newlib-4.4.0.20231231/configure --target=i386-elf --prefix=$HOME/opt/cross \
    --disable-multilib \
    --disable-newlib-supplied-syscalls \
    --enable-newlib-nano-malloc \
    --enable-newlib-nano-formatted-io
make -j$(nproc) && make install

# 4. GCC 完全ビルド
../gcc-13.2.0/configure --target=i386-elf --prefix=$HOME/opt/cross \
    --disable-nls --enable-languages=c --with-newlib
make -j$(nproc) && make install
```

> ⚠️ **newlib の nano オプションを省略しないこと**: 通常構成の newlib では printf
> 系がフル実装になり、各コマンド .bin が約3倍 (14KB→40KB) に肥大化して
> 1.2MB ブートFDが容量不足でビルド失敗する (2026-08 環境再構築時に実証済み)。

#### nano allocator の入力台帳 (T2f f1a)

SDK用nano/adapterの入力は [`sdk/allocator/nano_inputs.json`](../sdk/allocator/nano_inputs.json)
に置く。SDK実装入力を `sdk/` に集める既存の構成に合わせ、将来のadapterと隣接して
由来・版・ソースhash・configure・patch列を追跡する。`toolchain` 節は構築入力 (patch禁止)、`sdk_build` 節はf1bのソース・patch・表示用。
構築検査/receiptは前者だけを見るため、SDK入力の追記はtoolchain再構築を要求しない。
f1bの単体archive生成では `sdk_build.sources` のSDKファイルhash、`upstream` の
元ソースhashとtoolchain台帳の一致、member名を別に照合する。
`local.members` の名前集合を増やした後、既存receiptで `members SHA256 differs` が出る場合は、
保存した構築入力で `--record-built --source --tarball --config` を再実行するか、
`tools/ci/build_cross.sh` で再構築する (receipt形式は変更していない)。
ARM Ltd (2012, 2013) のnanoファイルのBSD 3-clause表示を
[`nano.LICENSE`](../sdk/allocator/nano.LICENSE) に全文保持し、配布全体の
`COPYING.NEWLIB` もhashで記録した。f1bで追加のソース/patchを使う際はその入力と表示も追加する。

手元の実toolchainはnewlib 4.4.0.20231231 / GCC 13.2.0、`CFLAGS=-g -O2`。
上の4つのnewlib設定と一致し、`libc_nano.a` / `libg_nano.a` は存在しない。
nanoは通常名の `libc.a` / `libg.a` に入り、両者のSHA256は
`d7a35f3fe99d533d16dca01694a4effd7a149dd7367e610de03e8ae5441e83d8`。
`build/config.mk` の検索順で `-lc` が `$(CROSS_DIR)/i386-elf/lib/libc.a` を選ぶ。
malloc/freeは `libc_a-malloc.o`、calloc/reallocは各非reentrant wrapper、
`_malloc_r` / `_free_r` / `_calloc_r` / `_realloc_r` は
`libc_a-mallocr.o` / `libc_a-freer.o` / `libc_a-callocr.o` / `libc_a-reallocr.o`。
台帳は全12 reentrant入口、整列/統計wrapper、sbrkr/lockの計22メンバーを記録する。
当初9メンバーは手元の `build-newlib-nano` の実objectともbyte一致した。
`_mallocr.c` が `<newlib.h>` の `_NANO_MALLOC` を見て `nano-mallocr.c` を取り込む。

f7 から USER ELF は `build/out/lib/libos32nano.a` (検証済み libc の6 memberを
私有化したもの + adapter) を `-lc` より前にリンクする。`make sdk` は archive、
`libos32nano.json` (adapter/CRT/私有member の hash)、リンク検査器とライセンスを配る。
`link_guard.py` は選択された全入力 (多重定義の負け側を含む) を hash で検査する。
未結線の memalign/valloc/mallinfo/usable-size/mallopt/cfree 等はリンクエラー。
allocator を一切含まない freestanding ELF は archive 不要だが、同じ入力検査を通る。
resident の shell/gshell は libc の nano を維持し、別ビルドの adapter が固定 primary
morecore だけを持つ。USER の `_sbrk` と malloc は adapter の primary を共有する。
不正な初期引渡し (`sbrk_heap_limit < aligned _end`) は以後の全 sbrk を ENOMEM に固定する。
副 arena の先頭1 pageに管理情報を置き、nano の呼出しが失敗して戻った後に状態を切り替える。
各 arena の free list は独立し、pointer 所属で free/realloc を振り分ける。空副 arena は
unmap 成功時だけ list から確定除去し、失敗時は復元する。primary/BSS は返さない。
65536 bytes 以上も f7 では nano 経路で、大塊の直接 map は f8 で切り替える。

`CROSS_DIR=... make check-nano-inputs-host` は実アーカイブ/memberのhash、提供symbol、
実 `-lc` リンクmapを検査する。i386出力はrelocatableとして検査し、実行はしない。
receiptの無いtoolchainは **f1a参照ホスト (PMの開発ホストで採取した構築)** の固定台帳と照合する。
他ホストや手作り/旧build_cross.sh構築では、同じ入力でもDWARFのbuild path差により
`archives SHA256 differs` になり得る。失敗時の診断も参照ホストと以下の復旧路を示す。
元のsource/tarball/config.logと同階層のMakefileが残っていれば、実入力を検証して
その構築固有のreceiptを作る (台帳/hashを手修正して通さない):

```bash
python3 tools/check_nano_inputs.py --cross-dir "$CROSS_DIR" --record-built \
  --source "$NEWLIB_SOURCE" --tarball "$NEWLIB_TARBALL" \
  --config "$NEWLIB_BUILD/i386-elf/newlib/config.log"
```

各変数はそのtoolchainを作った未改変の実入力へ設定する。元の入力が残っていない場合は
`tools/ci/build_cross.sh --prefix "$CROSS_DIR" --work "$CROSS_WORK" < /dev/null`
で作り直す (§8-5冒頭の手順)。構築スクリプトが同じ検証とreceipt生成を行う。
CIの `build.yml` は `make check-fast` の同じ検査を使う。
CI再構築ではDWARF内のbuild path等が異なり得るため、手元のアーカイブhashとのbyte一致は要求しない。
`tools/ci/build_cross.sh` はtarball/関連ソースの固定hash・実configure・CFLAGS・nano提供元を
検査した後、構築固有のhashをprefix直下の `os32-nano-build.json` に記録する。
同じ検査器がreceiptの由来・設定・builder hashと実アーカイブ/memberを照合する。
手作りtoolchainのreceiptにあるbuilderは、記録時の `build_cross.sh` を示す名目上の値で、
そのスクリプトで構築した証明ではない。`build_cross.sh` を変えると記録し直しが要る。
`receipt builder differs` の場合も、保存した実入力を使って上記コマンドでreceiptを再記録し、
入力が無ければスクリプトで再構築する。builderの比較はcache_keyと同じファイル全体の
SHA256 (正規化なし) なので、コメントだけの変更も両方を無効化する。
これは署名や再現可能ビルドの証明ではなく、信頼するCI構築における入力と出力の記録。
構築後の改変は失敗する。台帳をCIの出力で上書きしない。
CIキャッシュキーは `check_nano_inputs.py --cache-key` が、toolchainの
upstream/configure/GCC/target/CFLAGS/patchesとbuilder内容を正規化JSONからSHA256化する。
さらに `sorted(local.members)` / `sorted(local.archives)` の名前だけを含める。
名前を追加すると古いreceiptを持つキャッシュを作り直し、hash値だけの変更ではキーは変わらない。
SDK節・local hash値・symbolsは除外する。OS/GCC/newlib版をキーのprefixにも付ける。
旧キーのキャッシュは再構築する。検査器だけの変更は再構築せず、現在の検査器でreceiptを再検査する。
クロスtoolchain無しの `check.yml` の静的集合には追加しない。
GitHub Actionsでの新規構築はf1a作業中は未実施、PMがCI runで確認する。
システムの `libc.a` の手編集・allocator公開切替は行わない。

#### Rust ツールチェーン

`userland/rust/` の Rust プログラム (hello_gfx, alloc_demo, math_test_rs) のビルドには
rustup が必要。バージョンは `rust-toolchain.toml` (nightly + rust-src) が自動解決する。

```bash
curl https://sh.rustup.rs -sSf | sh -s -- -y
```
カスタムターゲット `i686-os32-none.json` と build-std
(`userland/rust/.cargo/config.toml`) により `core`/`alloc`/`compiler_builtins` を
ソースからビルドする。

#### Makefile へのパス設定 (.env)
環境が構築できたら、OS32のソースツリー最上位の `.env` ファイルに以下を設定してください
(`Makefile` が `-include .env` で自動読み込み):

```env
# クロスコンパイラのインストール先
CROSS_DIR=/home/user/opt/cross
# NP21/W 本体ディレクトリ (np21x64w.exe, os32.nhd の場所)
NP21W_DIR=/mnt/c/Users/<user>/Documents/np21w
# HostDrv デプロイ先 (NP21/W の HOSTDRV0 設定と一致させる)
HOSTDRV_DIR=/mnt/c/os32
```
OS32の `Makefile` は、ここで指定された `$CROSS_DIR/i386-elf/include` や `$CROSS_DIR/i386-elf/lib` を参照してビルドを行います。
なお `tools/np21w_ctl.py` (NP21/W の停止・起動、`docs/POLICY_DEBUG.md` §5) は `NP21W_DIR` を
Windows 表記へ変換して使う。変換が合わない環境では環境変数 `WIN_NP21W_DIR` (Windowsパス形式,
例 `C:\Users\<user>\Documents\np21w`) で上書きする。

> [!NOTE]
> **コンパイラのバージョンについて**
> PC-98ターゲットでは新しいコンパイラの最適化やABI変更による非互換リスク（およびバグ）のほうが大きいため、一度安定動作したGCCバージョンで**完全に固定化**して開発を継続するのがセオリーです。OS32では当面GCC 13.x系の利用を推奨しています。

### §8-6 GitHub Actions での本体ビルド

`.github/workflows/build.yml` (workflow 名 `build`、os32-v3 で 2026-09-30 に最小から作り直し —
[FORK_PLAN §3 d](tasks/v3/FORK_PLAN.md)) が、素の clone から core を完全ビルドし、`make check-fast`
を回して、成果物を artifact に置く。静的ゲートの `check.yml` とは別 (変異込みの `make check` は回さない)。

**狙い**: 開発ホストの回線が従量課金のことがあるので、ホストからはソースを push する
(小さい) だけにし、イメージの受け渡しは GitHub → 実機に繋がったホスト (Ubuntu ノート)
で済ませる。

| 項目 | 内容 |
|---|---|
| 起動 | `main` / `wt/**` への push、tag `v*` の push、手動 (workflow_dispatch)。同じ ref の古い run は打ち切る |
| ビルド | `make -j$(nproc) all` (core。`all` は 2 種の起動 FD と ISO を含む) → `make check-fast`。`NP21W_DIR` は存在しない場所で、コピー失敗は Warning で続行する |
| submodule | **`apps/` と `game/` (private submodule) は v3 の CI では組まない** (ユーザー決定 2026-09-30: 今回の開発に要らないので `SUBMODULE_TOKEN` は登録しない)。必要になったら secret `SUBMODULE_TOKEN` (os32-apps / os32-game の Contents: read を持つ fine-grained PAT) で submodule 取得 + `make external` の step を足す ([FORK_PLAN §3 d ③](tasks/v3/FORK_PLAN.md)) |
| フォント | IPAex の TTF はリポジトリに無く `tools/fetch_fonts.py` が IPA の公式配布から取る (§8-2)。CI は `OS32_ACCEPT_IPA_LICENSE=1` で非対話に同意し (リポジトリ所有者が同意済みの前提)、取れた ttf を key `ipaex-<zip の SHA-256>` (`fetch_fonts.py --print-zip-sha256`) で `actions/cache` に置く |
| 文書だけの push | `docs/**`・`*.md`・`.claude/**` だけの push では回さない (`paths-ignore`。文書の検査は `check.yml`)。同じ ref の run は 1 つ (`cancel-in-progress`) なので、**run の途中でその ref へ push すると打ち切られる** |
| ツールチェーン | `tools/ci/build_cross.sh` (§8-5) で `~/opt/cross` に作り、`actions/cache` で保存。キーは `cross-i386-elf-<OS>-gcc<版>-newlib<版>-<構築入力/builder/inventory名のSHA256>` (`check_nano_inputs.py --cache-key`)。**構築入力・builder・inventory名を変えたときに作り直す**。SDK用ソース/patch・local hash値の変更はキーを変えない。os32 での実測 (2026-09-23): 初回はツールチェーン約 25 分、キャッシュが効けば run 全体で約 2 分 (ubuntu-latest 4 vCPU、キャッシュ 335MB)。os32-v3 での実走はまだ (最初の push で見る) |
| Rust | `rust-toolchain.toml` (nightly + rust-src) を `rustup toolchain install` (引数なし) で解決。`target/` のキャッシュは無し (最小構成。遅ければ `Swatinem/rust-cache` を足す) |
| apt | `build-essential nasm genisoimage e2fsprogs` + ツールチェーン構築の `libgmp-dev libmpfr-dev libmpc-dev texinfo bison flex`。Python は `requirements.txt`。FD イメージは `tools/mkfat12.py` (純 Python) なので mtools は要らない |
| 上限 | `timeout-minutes: 150` |

**成果物** (artifact 名 `os32-<ブランチ名の / を - に>-<sha7>`、保持 30 日):

| ファイル | 中身 |
|---|---|
| `os32_boot.d88` / `os32_boot.img` | 2HD 1232KB の起動 FD (D88 / 生イメージ) |
| `os32_boot144.img` | 1.44MB の起動 FD (生イメージ、[POLICY_DEBUG.md §4-47](POLICY_DEBUG.md)) |
| `os32_install.iso` | インストール ISO |
| `packages/*.PKG` | パッケージ |
| `vmkernel.lz4` / `kernel.map` | カーネルとシンボル (kselftest の番地はこの map で引く) |
| `BUILD_INFO.txt` / `SHA256SUMS` | コミット・ref・日時・run の URL・gcc / rustc の版・各ファイルのサイズと sha256 (`tools/ci_fetch.sh` が読む) |

os32 の `build.yml` にあった配備ツリー (`hostdrv.tar.gz`)、埋め込み commit id の照合、tag → Release の job は
最小構成に含めていない (要るときに足す)。

**実機ホスト側の取り方** — `tools/ci_fetch.sh` (要 `gh`、`gh auth login` を 1 回。
public repo でも artifact の API ダウンロードには認証が要る):

```bash
tools/ci_fetch.sh                       # main の最新の成功 run (既定 --repo ske-studio/os32-v3)
tools/ci_fetch.sh --branch wt/ci
tools/ci_fetch.sh --sha 0a5247f         # そのコミットの run (短縮 SHA 可)
tools/ci_fetch.sh --dry-run             # 選ばれる run と artifact を表示するだけ
```

保存先は `./os32-ci/<artifact 名>/` (`--dir` で変更)。取得後に `BUILD_INFO.txt` を表示し、
`SHA256SUMS` で全ファイルを照合する (不一致は終了 1、`gh` が無い / 未認証は終了 2)。

> ⚠️ artifact は「ビルドが通った」ことしか保証しない。NP21/W でも実機でも起動していない
> ([V4])。実機へ入れたら kselftest の値を**その artifact の `kernel.map`** の番地で読む。
