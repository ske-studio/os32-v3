# PPC_KAPI_FILL 段 6c の確定設計 (LOOP_EXT2)

> 状態: **確定 (2026-11-20)** — 本線 PM。Fable 起草、sol レビュー 2 回、確定の節を PM が反映 (6c はユーザー決定 U1・U3 も)。出所は本線の機械の ~/os32-tmp/run/kfill-6c/。段の状態は票 TASK_PPC_KAPI_FILL が持つ。行番号つきの参照は往復の版のまま (取り込みの時に直す)。

# KAPI の埋め 6c 詳細設計 r2 — loop の転送 (6f 残り) → loop の書込の口 → formatter の自立 → 媒体の口

対象: origin/main (c8e78d850)。計画 `docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md` §2 の platform 行 (loop 3 本)・ext2 行 (2 本)、§3 の 6f・6c。
前提: シングルタスク・協調切替、整数演算、[C1]〜[D4]、共通は機種・装置のヘッダを読まない、1 機能 1 ファイル、x86 の振る舞いを変えない。
ビルド・試験は回していない。`.env`・`docs/archive/` は読んでいない。AC-KAPI-1 の本文は r2 で読み直していない (番号の参照だけ)。

## r1→r2 の変更 (REVIEW.r1 の指摘ごと)

| 指摘 | 直し |
|---|---|
| P2 F1–F8 | F5・F6 を書き直し、ほかは据え置き |
| P1 F5 権限 | `exec_disk_write_allowed` は CPL3 では `disk_write_authorized` (ロード元の経路、`exec/exec.c:329-330`・`exec/exec_launch.inc:269`) も要る。受入は常駐シェルか許可済みの経路から回し、権限の拒否 (-1) と読取専用の媒体の拒否を別の項目で見る (§2-3、A4a/A4b) |
| P1 作業域 | formatter は `ext2_g_blk` を使わず自分の `fmt_blk` を持つ (段 N5)。下の ext2 が作業域を潰す場合を host で固定 (A5) |
| P1 drv の互換 | 型・unit の切り出しをやめる。HD の提供者には**元の drv をそのまま**渡す。loop は `0x0600..0x0603` の完全一致だけ (決定後の段)。両入口の検査の順は今のまま (§2-2) |
| P2 status・D88 | `loop_dev_status` は attach 済みで 1・それ以外 0 (`loop_dev.c:441-451`)。LBA の書込可否の口 `loop_dev_lba_writable` を足し 512B の D88 を書込前に断る (段 N3)。`Device` の関数は `int lba, int count` |
| P1 M5–M9 の順 | IDE の提供者の追加・ビルドの選択・起動時の登録・formatter の切替を 1 つの動作単位 (段 N7) にした。loop の提供者はリンクと登録を同じ段 (D1) で有効にする |
| P1 M6 | 持ち主を確定: 作業域・未マウントの I/O = formatter、時刻 = 新 `ext2_time.c` (純移動)、読みのキャッシュの破棄と `ext2_fs_error` = mount 側 (HD の提供者が後始末の口で呼ぶ)。nm は検証 (§2-4、段 N6・N8・N9) |
| P2 像ごとの対応 | §2-5 に像 × (EXEC・loop・formatter・公開) の表。RAW の標本は `.img` 163840B (320 sector × 512B) |
| P2 受入 | A3 の変異を A5 の成功系に結んだ。A2・A8 は map・`nm -l`・層の検査 (変異なし)。`ext2_part_host`・`b8_open_host` の追従、PPC の不在の表・link の検査の更新、build_compare の関門を段ごとに書いた |
| P2 §1-2 | AC-KAPI-1、像ごとの能力の差、両機種で同じ期待値、計画 §2「VFS と同じ仮想装置を選択」との整合を足した |
| P1 U1 | ramfs は 1 ファイル 32KiB・全体 64KiB (`fs/ramfs/ramfs.h:8-9`、r1 は古い木の値)。推奨を「loop 3 本を先に、ext2 2 本は当面 E」に替えた |
| P2 U2・U3 | U2 は PM 決定 (b) で未決から外した。U3 は (a) を推奨のまま、決定後の段へ |
| PM の指示 | 段表を「ユーザー判断に依らない段 (N・P)」と「U1・U3 の決定の後の段 (D)」に分け、前者だけで受入できる形にした |

## 1. 実物の確認

### 1-1 計画の前提と実コードの食い違い (8 件、c8e78d850)

| # | 前提 | 実物 (file:line) | 効き |
|---|---|---|---|
| F1 | 6f の loop の転送は main にある | 無い。c41d5992c は「loop は保留」。`platform/pc98/device_loop.c` と `build/sources/order.mk:5` に残る | 先頭に N1・N2 |
| F2 | 書込可能な block の契約から format (計画 :77) | lo* の `Device` は `blk_write` が常に 0 (`drivers/block/loop_dev.c:182,338`)。書込は `loop_dev_write_lba` (:386) の直呼びだけ | 接着の `Device` (D1) |
| F3 | ext2_fmt.c は共通 | IDE 専用: `platform_ide_drive_present` (`fs/ext2/ext2_fmt.c:168,221,248`)、`platform_ide_range_ok` (:176,261)、`platform_disk_geometry_get` (:249)、`ext2_dev_for` = `"hd%d"` (`fs/ext2/ext2_super.c:217`)。選択は `C_COMMON_PC98_PENDING` (`build/sources/common.mk:10`) | 媒体の口 (N7) |
| F4 | 受入「再 mount」(計画 :264) | mount は HD 専用 (`fs/ext2/ext2_vfs.c:419`) で PC-98 の区画表を読む | PM 決定: 6c は読み戻しまで |
| F5 | 「attach と書込権限」 | `loop_attach` に呼び手の検査は無い (`sdk/kapi.json:1231-1235`)。媒体の書込可否は `vfs_open(O_RDWR)` の成否 (`loop_dev.c:295-303`)。`ext2_format*` の body は `exec_disk_write_allowed` (前面の CUI、CPL3 は加えて `disk_write_authorized`) | 権限は 3 層 (§2-3) |
| F6 | PowerMac に書ける媒体がある | `/` = iso9660 on rd0 (読取専用、`init/boot_root.c:11`)。`/tmp` = ramfs、1 ファイル 32KiB・全体 64KiB。ext2 の下限 64 ブロック = 64KiB (`fs/ext2/ext2_layout.h:34`) に届かない。`/host` (serialfs、USERSHELL) の上の成功は未確認 | U1 |
| F7 | loop は選べば入る | `loop_dev.c` は `build/features/pc98-devices.mk:6`、`loop_dev_init` の呼び手は `platform/pc98/boot.c:124` だけ | N4 |
| F8 | loop の誤り符号は一意 | FORMAT と UNSUPPORTED が -2、BUSY と WRITE_PROTECTED が -3 (`drivers/block/loop_dev.h`) | 値は変えず、可否は専用の口で見る (N3) |

formatter の外への依存 (r2 で確かめた): `ext2_g_blk` (`ext2_super.c:28`)、`ext2_current_time` (`ext2_super.c:160`)、`ext2_write_block`/`ext2_read_block` (`ext2_super_block_io.inc`) → その中の `ext2_read_cache_reset` (`ext2_inode.c:8`、.inc :27,35,82) と `ext2_fs_error` (`ext2_super.c:48`、.inc :48,55)。`ide_range_ok` は `drive < 0 || drive >= IDE_MAX_DRIVES` を断る (`drivers/ata/ide.c:354-359`)。

### 1-2 統合の判断 (POLICY_DEV §1)

| 点 | 判断 |
|---|---|
| 既存で足りるか | loop の本体は置き場も既存の関数も変えない (足すのは可否の口 1 関数)。ブロック I/O は `dev_blk_read_lba`/`dev_blk_write_lba` を再利用。接着の `Device` は登録しない |
| 口は 1 つか | formatter から見た媒体は `struct ext2_medium_ops` の 1 つ。weak の代役は使わず登録。計画 §2「VFS と同じ仮想装置を選択」: 選ぶ slot は VFS が使う lo0..lo3 と同じ物で、formatter 用の別の仮想装置は作らない |
| 持ち主は 1 人か | slot・fd・writable = `loop_dev.c`。媒体の表・作業域・未マウントの I/O = `ext2_fmt.c`。時刻 = `ext2_time.c`。IDE の範囲の検査と mount 側の後始末 = `ext2_medium_ide.c`。loop と ext2 は互いのヘッダを読まない |
| AC-KAPI-1・像の差 | 公開する slot は両機種で同じ共通の定義に落ちること (map・`nm -l`)。像ごとの能力の差は §2-5 の表で持ち、無い像は型別不在。試験の期待値 (標本の sector 数 320・bps 512、superblock の値) は両機種で同じ物を使う |

## 2. 通り道と境目

### 2-1 loop の口 (新設 `include/loop_contract.h`)

```c
/* 値・名前・引数は loop_dev.h の今の物と同じ (F8 の重なりも保つ)。宣言の移動 + 末尾の 1 本だけ新しい */
#define LOOP_ERR_FORMAT (-2)
#define LOOP_ERR_BUSY (-3)
#define LOOP_ERR_IO (-4)
#define LOOP_ERR_UNSUPPORTED (-2)
#define LOOP_ERR_WRITE_PROTECTED (-3)
void loop_dev_init(void);
int  loop_dev_attach(const char *vfs_path, int slot);
void loop_dev_detach(int slot);
int  loop_dev_status(int slot, u32 *out_total, int *out_bps);   /* 1=attach 済み、0=未使用・範囲外 */
int  loop_dev_get_geometry(int slot, u16 *cyls, u8 *heads, u8 *spt, u16 *bps, u32 *total_lba);
int  loop_dev_read_chs(int slot, u16 cyl, u8 head, u8 sect, void *buf);
int  loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf);
int  loop_dev_write_lba(int slot, u32 lba, u32 count, const void *buf);
int  loop_dev_is_writable(int slot);
int  loop_dev_lba_writable(int slot);  /* 新: 1 = attach 済み・O_RDWR で開けた・LBA 書込ができる形式 (D88 以外)。ほかは 0 */
```
`loop_dev.h` はこれを include し、残り (attach_fd・D88 の口・get_fd ほか) だけを持つ。転送 6 本は `kernel/dev/device_loop.c` へ純移動し include を口に替える。

### 2-2 ext2 の媒体の口 (新設 `include/ext2_medium_contract.h`)

```c
struct _Device;
struct ext2_medium_ops {                 /* 引数 drv は KAPI から来た値そのまま (切り出さない) */
    int  (*present)(int drv);                           /* 1/0。今の platform_ide_drive_present の位置で呼ぶ */
    int  (*total_sectors)(int drv, u32 *out);           /* 0=成功。format_at だけが、今の geometry 取得の位置で呼ぶ */
    int  (*find_part)(int drv, u32 *start, u32 *len);   /* EXT2_OK / EXT2_ERR_*。format だけが呼ぶ。BIOS の幾何の検査と kprintf を含む */
    int  (*range_ok)(int drv, u32 lba, u32 count);      /* 1/0。今の 2 か所 (:176, :261) で呼ぶ */
    struct _Device *(*dev)(int drv);                    /* ext2 の I/O が使う装置 */
    void (*after_write)(void);                          /* 0 可。書込 1 回ごとの mount 側の後始末 (HD: 読みのキャッシュの破棄) */
    u32  min_lba;                                       /* format_at の開始の下限 (HD = EXT2_FMT_MIN_LBA) */
};
#define EXT2_MEDIUM_HD   0
#define EXT2_MEDIUM_LOOP 1               /* 決定後の段 D1 まで未登録 */
int ext2_medium_register(int kind, const struct ext2_medium_ops *ops);   /* 0 / -1 (範囲外・二重) */
extern const struct ext2_medium_ops ext2_medium_ide_ops;                 /* fs/ext2/ext2_medium_ide.c */
```
- N 段での選び方: `ext2_fmt.c` は**どの drv も HD の ops に渡す** (未登録なら EXT2_ERR_IO)。`0x10000`・`0x0600` などは今と同じく IDE の検査が断る。HD の経路は呼ぶ関数・順・戻り値が今と 1 対 1。
- D1 (U3 の後) での選び方: `drv` が `0x0600..0x0603` に完全一致し LOOP が登録済みなら loop の ops、それ以外は HD の ops。

### 2-3 書込の権限の通り道 (どこかで断れば書込 0 回)

| 層 | 見る物 | 持ち主 | 断る値 |
|---|---|---|---|
| 1 呼び手 | 前面の CUI、常駐シェル、または CPL3 でロード元の経路が許可済み | KAPI body `exec_disk_write_allowed()` (変えない) | -1 |
| 2 媒体 | HD: present。loop (D1): `loop_dev_status()==1` でなければ EXT2_ERR_IO、bps≠512 か `loop_dev_lba_writable()==0` なら EXT2_ERR_INVAL | 提供者 (状態は loop_dev.c の slot) | 左 |
| 3 範囲 | 長さ 0、min_lba、全長、桁あふれ、range_ok (今の順) | `ext2_fmt.c` | EXT2_ERR_INVAL |
| 4 書込 | loop: 接着の `Device.blk_write(int lba,int count)` → `loop_dev_write_lba` (ここでも writable を見る) | 接着 | EXT2_ERR_IO |

loop 用の `Device` (D1): `ext2_medium_loop.c` の `static Device` 1 個、`priv` に slot、dev_register しない (lo* に `blk_write` を足すと x86 で KAPI `dev_blk_write` の結果が変わるため)。

### 2-4 formatter の自立 (持ち主の確定)

| 物 | 今 | r2 の持ち主 | 段 |
|---|---|---|---|
| 作業域 1 ブロック | `ext2_g_blk` (mount 側と共有、下が ext2 だと潰れる) | `ext2_fmt.c` の `static u8 fmt_blk[EXT2_BLOCK_SIZE]` (.inc 3 本も) | N5 |
| 時刻 | `ext2_current_time` @ ext2_super.c | `fs/ext2/ext2_time.c` へ純移動 | N6 |
| 未マウントの I/O | `ext2_write_block`/`ext2_read_block` (キャッシュ破棄・fs_error を引く) | `ext2_fmt.c` の静的 `fmt_wr`/`fmt_rd`: 区画の外を断る → `dev_blk_*_lba` → 書込の後に `ops->after_write` | N8 |
| 読みのキャッシュの破棄 | I/O の中 | mount 側。HD の ops の `after_write` = `ext2_read_cache_reset` | N7・N8 |
| `ext2_fs_error(&fmt_ctx)` | I/O 失敗で呼ばれる | formatter は呼ばない (fmt_ctx は使い捨て)。**N8 の着手で中身を読み、ログ・大域の状態に効くなら HD の ops に失敗の口を足して同じ呼び出しを残す** | N8 |

### 2-5 像ごとの対応

| 像 | EXEC | FEATURE_LOOP | formatter | loop 3 本 | ext2 2 本 |
|---|---|---|---|---|---|
| PC-98 (全部) | 1 | 1 (既定) | 入る + HD の提供者 | 公開 (今のまま) | 公開 (今のまま) |
| PowerMac EXEC・USERSHELL | 1 | 1 (P2 で像の既定に) | N9 では compile-only | P4 で公開 | 型別不在 (U1 の決定まで) |
| PowerMac CLI ほか EXEC 無し | 0 | 0 | 無し | KAPI 無し | KAPI 無し |

FEATURE_LOOP=0 の像で起動の呼び出しを結ばないため、PowerMac の `loop_dev_init` は FEATURE_LOOP の時だけ選ぶ `platform/powermac/loop_boot.c` に置き、無効の像は `build/sources/hooks.mk` の不在の型に従う (型の細部は P2 の着手で確認)。PowerMac の媒体: `/` の標本 `.img` (読取専用で attach、writable=0)。`/tmp` は 32KiB まで、`/host` は未確認。

## 3. 大きさと関数差

| 項目 | 見込み (概算、実測は各段の報告) |
|---|---|
| x86 の増分 (N 段) | `loop_dev_lba_writable` 約 40B、ops の表と register 約 80B、`ext2_medium_ide.c` は移動で差し引き +100B 前後、`fmt_wr`/`fmt_rd` 約 150B、bss +1KB (fmt_blk、像の大きさには入らない)。計 **.text +0.4KB 前後**。D1 でさらに約 300B |
| 起動 FD・MINIMAL | 上限の実値は読んでいない。PM が `docs/08_build.md` §8-2 の上限の表と `make all` の実測で照合 |
| build_compare | **N1・N2・N4a・N4b・N6 = 関数差 0 が関門**。N3 = 新関数 1 つだけ。N5 = `fmt_*` と `ext2_format_range` が変わる。N7 = `ext2_format`・`ext2_format_at`・`ext2_format_range` が変わり、ide の提供者の関数が増え、`platform/pc98/boot.c` の起動関数 1 つが変わる。N8 = `fmt_*` が変わる。N5・N7・N8 は差分を実測して報告に載せる (見込みと違えば止まる) |

## 4. 段

### 4-1 ユーザー判断に依らない段 (これだけで受入できる。x86 の振る舞いは不変)

| 段 | 担当 | ファイル | 中身 | 関門 | 順・並列 |
|---|---|---|---|---|---|
| N1 | 本線 | `include/loop_contract.h` 新設 (+ `loop_dev.h` の include) | §2-1 の宣言の移動 (新しい 1 本の宣言は N3) | 関数差 0、check_layers 0 | 先頭。N6 と可 |
| N2 | 本線 | `platform/pc98/device_loop.c` → `kernel/dev/device_loop.c` | 6f-loop の純移動 (SRC_MOVE §3)、order.mk:5 の経路、`dep_dir_a_l04_host` の covers | 関数差 0 | N1 の後 |
| N3 | 本線 | `drivers/block/loop_dev.c` | `loop_dev_lba_writable` を足す (既存の関数は触らない) | host `loop_dev_host` に 3 例 | N1 の後 |
| N4a | 本線 | `build/select.mk` | FEATURE_LOOP (PC-98 既定 1、PowerMac は像ごと §2-5)。持ち主から受け取る | 有効・無効の構成が組める (PM) | 直列 |
| N4b | 本線 | `build/sources/common.mk` (+ pc98-devices.mk:6 から抜く) | `C_COMMON_LOOP = kernel/dev/device_loop.c drivers/block/loop_dev.c`、x86 のリンク順は order.mk で保つ | 関数差 0、map の順が同じ | N2・N4a の後 |
| N5 | 本線 | `fs/ext2/ext2_fmt.c` (+ `ext2_fmt_*.inc` 3 本) | 作業域を `fmt_blk` に | host A5、**NP21/W** (hdprep → install → 起動) | N7 と直列 (同じファイル) |
| N6 | 本線 | `fs/ext2/ext2_super.c` → `fs/ext2/ext2_time.c` | `ext2_current_time` の純移動、common.mk は同じ変数のまま | 関数差 0 | N1 と可 |
| N7 | 本線 | 1 つの動作単位: `include/ext2_medium_contract.h`・`fs/ext2/ext2_medium_ide.c` (新設)・`build/sources/common.mk` (PC98_PENDING に追加)・`platform/pc98/boot.c` (登録 1 行)・`fs/ext2/ext2_fmt.c` (切替) | §2-2。分けると間の版で HD の format が全部落ちるので 1 段 1 ファイルの例外にする。`ext2_part_host`・`b8_open_host` は提供者を組み込み登録を呼ぶ | host A6・A7、**NP21/W** (同上) | N5 の後 |
| N8 | 本線 | `fs/ext2/ext2_fmt.c` | 未マウントの I/O を自前に (§2-4) | host A6・A8、**NP21/W** (同上、N7 とまとめて 1 回でよい) | N7 の後 |
| N9 | 本線 | `build/sources/common.mk` | `ext2_fmt.c`・`ext2_layout.c`・`ext2_time.c` を PowerPC の compile-only に足す。`nm` で formatter 3 個の未定義が ext2_super・ext2_inode・platform_ide_* を引かないことを確かめる | PPC のコンパイル、nm、PPC の link の検査の更新 | N8 の後 |
| P1 | PowerPC | `build/ppc-ram-root.json` | 標本 `.img` 163840B (320 sector × 512B、中身は位置で決まる固定の模様) | 像の検査 | N4b の後 |
| P2 | PowerPC | `platform/powermac/loop_boot.c` 新設 (+ 像の既定で FEATURE_LOOP=1) | `loop_dev_init` の呼び出し | QEMU の起動、無効の像も組める | P1 と可 |
| P3 | PowerPC | `sdk/kapi_target_ppc.py` (loop 3 本) | 公開。PPC の不在の表から 3 本を外す | A1・A2 | P2 の後 |

NP21/W の受入は直列 (本線の取り決め 3): N5 → N7+N8 の 2 回。

### 4-2 U1・U3 の決定の後の段

| 段 | 待つ物 | 担当 | 中身 |
|---|---|---|---|
| D1 | U3 | 本線 | 1 つの動作単位: `fs/ext2/ext2_medium_loop.c` 新設・common.mk の選択 (FEATURE_LOOP かつ formatter)・`platform/pc98/boot.c` の登録・`ext2_fmt.c` の `0x0600..0x0603` の振り分け。受入 A3・A4b・A9 (host + NP21/W: ext2 の上の使い捨ての像を lo0 に attach) |
| D2 | U3 (a) | 本線 | 公開ヘッダの定数と KAPI_SPEC の記載 (os32-kapi-add、版は PM)。(b) ならこの段は無く、試験は数を直書き |
| D3 | U1 が「公開する」になった時 | 本線 → PowerPC | common.mk で formatter を PowerMac の EXEC 像に選ぶ → `platform/powermac/loop_boot.c` に loop の提供者の登録 → 公開表 (ext2 2 本) → QEMU で書ける媒体の上の両 format の成功。「当面 E」ならこの段は無く、不在の試験 (計画 §5 の E) だけ残す |

### 4-3 受入 → 試験と変異

| # | 受入 | 場 | 変異 (1 行) |
|---|---|---|---|
| A1 | 標本を attach、status が 1・320 sector・512B、detach の後は 0。両機種で同じ期待値 | host `loop_dev_host`、NP21/W (N4b の後)、QEMU (P3) | status の total を +1 |
| A2 | `platform_loop_dev_*` の定義元が両像で `kernel/dev/device_loop.c` | map・`nm -l`・check_layers (実行の変異なし) | — |
| A3 (D1) | `0x0600..0x0603` 以外 (`0x0604`・`0x10600`・`0x10000`) は loop に落ちず書込 0 回。**成功系 A9 と同じ試験の中で見る** | host 新 `ext2_medium_host` | 振り分けを常に HD にする → A9 が落ちる / 上位を見ない → A3 が落ちる |
| A4a | 権限の無い呼び手は -1 で、提供者が呼ばれない | host (KAPI body の既存の型の試験)、NP21/W (未許可の経路のアプリ) | body の検査を外す |
| A4b (D1) | 読取で開いた loop・512B の D88・未 attach は順に INVAL・INVAL・IO、書込 0 回 | host `ext2_medium_host`、NP21/W (常駐シェルから) | `loop_dev_lba_writable` の検査を外す |
| A5 | 下の装置が書込の途中で `ext2_g_blk` を潰しても、書かれた superblock と前後の番兵 sector が正しい | host (書込の関数が `ext2_g_blk` を塗る偽の Device) | `fmt_blk` を `ext2_g_blk` に戻す |
| A6 | HD の経路が不変: drv 0..3 で書く sector の列と戻り値が N5 の前と同じ。`0x10000`・`0x0600`・-1 は今と同じ値で断り書込 0 回 | host `ext2_part_host`・`b8_open_host` (期待は無変更)、NP21/W | drv を `& 0xFF` してから渡す |
| A7 | 未登録 (HD の提供者なし) は EXT2_ERR_IO、二重の登録は -1 | host | 表の空きの検査を外す |
| A8 | HD の書込のたびに読みのキャッシュが破棄される (mount 中の区画の外を format_at した後の読みが古くない) | host (破棄の回数を数える) | `after_write` を呼ばない |
| A9 (D1) | format_at(0x0600\|slot): 範囲の外の前後 1 sector が不変、superblock が ext2_layout_plan の値。format: 全体・頭打ち | host、NP21/W | start_lba を無視する (計画 :264) |
| A10 | loop だけの像・formatter だけの compile-only で相手の記号を引かない | PM のビルド、nm、層の検査 (変異なし) | — |
| A11 | 関数差が §3 のとおり | build_compare | — |

再 mount は 6c の外 (PM 決定、計画に明記)。

## 5. 未決 (ユーザー判断、朝まで保留)

| # | 争点 | 推奨 | 両案の段の違い |
|---|---|---|---|
| U1 | PowerMac の ext2 2 本。書ける 64KiB 以上の媒体が今は無い (F6) | **loop 3 本を先に公開、ext2 2 本は当面 E (型別不在)**。書ける媒体で両 format の成功を確かめられた時に見直す (恒久の E にはしない)。拒否側だけ通して公開する案は採らない | 当面 E: D3 は無く、N9 の compile-only と不在の試験で止まる。公開する: 媒体を決める段 (`/host` の確認か ramfs の上限の拡大) の後に D3 の 4 手が足される |
| U3 | drv = `0x0600\|slot` で loop を選ぶ意味の拡張と SDK の定数 | **(a) 公開ヘッダに定数を足し KAPI_SPEC に書く**。HD の経路の結果は A6 で固定 | (a): D1 + D2。(b) カーネルの中の約束だけ: D1 だけで D2 が無い。認めない: D1・D2・D3 が無く、N 段までで終わる (loop の上の format は公開されない) |

## 反論

なし (指摘は全部反映。N7 を 1 段 1 ファイルの例外にした点はレビューの修正案どおり)。

## 確定 (2026-11-20、sol の第 2 回「N1〜N9・P1〜P3 は条件付きで確定可」、PM が反映。D1〜D3 はユーザーの U1・U3 の決定の後)
- C1 (N6/N7 の host の追従): ext2_read_cache_host・ext2_write_io_host・vfs_fd_path_host なども実物の formatter で試験の媒体を作る。N6 で ext2_time.c の取り込み、N7 で IDE の提供者の取り込みと登録を、該当する全 fixture と検査の入力の宣言 (covers) に追従させる (段の完了条件)。
- C2 (N7/N9 のビルドの選択): common-compile は C_COMMON_PC98_PENDING もコンパイルするので、IDE の提供者は common.mk の PC98_PENDING でなく build/platform/pc98.mk 側で選ぶ。共通側に残る 3 オブジェクトは N9 で nm を確かめる。N7 の 1 段 1 ファイルの例外 (5 ファイルを 1 つの動作単位) は承認。
- C3 (N8 の x86 の互換): 今は書き込みの前と読み込みの失敗の時に読みのキャッシュを破棄し、ext2_fs_error も一時の ctx の判定より先に破棄する。after_write だけにせず、今の位置で破棄する。A8 は成功の回数に加えて I/O との順序と読み込みの失敗を観測する。
- C4 (P2/P3 の像と初期化): kboot-shell は FEATURE_CLI=1 かつ FEATURE_EXEC=1。像の名前ごとに loop の有効・不在を明記し、P2 に既存の起動の経路からの呼び出しとソースの選択、P3 に loop が無効の EXEC の像の型ごとの不在を含める。§2-5 の公開の段は P4 → P3。
- C5 (A1/AC-KAPI-1): PPC のマニフェストに標本の生成元とビルドの依存。NP21/W にも同じ標本を置く手順、host の loop_dev_status とゲストの公開 loop_status の期待値を区別。P3 の受入に無効の像からの公開スロットの呼び出し。
- C6 (決定の後の段): D3 の前提に D1 の完了を加える。A3 の成功系の参照は変更表でも A9 にそろえる。

## ユーザー決定 (2026-11-20 朝)
- U1: **当面 E** — PowerMac の ext2 の 2 本 (format 系) は型ごとの不在のまま。loop の 3 本を先に公開し、書き込める媒体で両 format の成功を確かめられた時に再判定。
- U3: **広げる** — KAPI の drv に 0x0600|slot (loop) を足し、同じ口で loop を選ぶ。drv の意味と SDK の定数を KAPI 仕様に書き、loop は 0x0600..0x0603 の完全一致だけ、既存の HD の値の結果は試験で固定。
これで D1〜D3 (U1 の公開は E のまま) に進める。D3 の前提は D1 の完了 (確定の C6)。

## ユーザー決定 (2026-11-20 夕) — U1 の見直し
- U1: **公開する (実装対象に戻す)** — G4 の実機に 6.4 GB の ATA の HDD をつないだので、書ける媒体の条件が来た (ユーザーが PowerPC のセッションで決め、本線の PM がユーザーに確かめた)。
- 前提の順: (1) 本線の ATA の block の HAL の口 (計画 PPC_KAPI_FILL の段 8「ATA の block 契約」、未設計) → (2) PowerPC の ATA の装置 (KeyLargo の Ultra ATA/66、docs/ppc/ata.md) をその口の実装として → (3) ext2 の formatter の提供者 (段 6c の N7 の ext2_medium_contract の口) に PowerMac の ATA を登録し、段 D3 (common.mk の formatter の選択・提供者の登録・公開表の ext2 2 本) を出す。受入は QEMU (mac99 の IDE) が先、G4 の実機は後 (ユーザー指示 2026-11-18)。G4 の HDD への書き込みは中身を確かめてユーザーの承認の後 ([D2])。
