# PPC_SHELL_DESIGN — PowerPC でシェルが動くまで (計画と中間の到達点 S-A の詳細設計)

> 状態: **確定 (2026-11-11)** — 中間 S-A (開発用の小さな CLI が LAN の端末でプロンプト・内蔵コマンド・/tmp への書き込みを行う) の詳細設計と、最終 S-B (外部の USER アプリ) から製品のシェル sh.bin までの計画の輪郭。S-B の USER ABI・syscall・exec は別の往復で詳細設計する。段の状態は票 TASK_PPC_SHELL が持つ。

> 出所: ~/os32-tmp/run/ppc-shell/DESIGN.final.md (棚卸し gpt-6.1-sol の SURVEY.md、gpt-6-astra 起草 r1→r2、Opus 5.5 の第 1 回 Request changes・第 2 回 条件付き Approve、PM が残りを反映)。中身は変えていない。


S-A の詳細設計と S-B 以後の計画。ユーザー判断の未決は0件。
読み取りのみ。ビルド・試験・容量計測は未実施。GPL・QEMU ソース・docs/archive/ は未参照。

### 1. 実物の確認

|区分|確認結果・設計への反映|根拠|
|---|---|---|
|食い違い①|常駐 shell の待ちは `ime_getkey()`。丸ごとのリンクでは IME が入る|`userland/shell/ui.c:22–25,658`|
|食い違い②|ISO の CD imports は `OS32_CD_MODULE` 時だけ。静的リンクでは imports 分離不要|`fs/isofs/iso9660.c:25–30`|
|食い違い③|ISO mount は `VFS_DEV_CD` と `cdN` を使用。RAM Device 登録だけでは不足|`fs/isofs/iso9660.c:371–382`|
|名前登録|名前表は差替え式。platform が cd・ram をまとめて登録する|`fs/vfs.c:373–399,427`|
|既存方針との衝突|「S-A は③の USER 実装前、S-B は③」へ更新する|`docs/tasks/ppc/TASK_PPC_LAN.md:12–14`|
|既存起動との衝突|x86 は root mount 後に外部常駐 shell を `exec_run()`。S-A は C 関数呼出し|`init/kernel.c:185,430`|
|既存 UI との衝突|座標 console とは別に stream の口を追加する|`userland/shell/ui.c:120,133`、`kernel/console/console_backend.h:22–27`|
|SDK の現状|PPC は SDK/layout 無効。既存 shell は i386 CRT・libc・PC98 区画処理をリンク|`sdk/target_profile.py:18–20`、`build/programs.mk:99–108`|
|入力の現状|InputOps は19関数で NULL 検査なし。S-A の専用 getkey では登録不要|`include/input_service.h:10–31`、`kernel/input_service.c:52–70`|
|実行形式|OS32X は LE 出力・native field 検証、ELF 読取りも LE 固定|`sdk/os32x_hdr.py:82–89,285`、`exec/os32x_hdr.c:9`|
|時間の現状|ISO の tick 差は idle cache 破棄。固定 RAM の読取りの正しさに影響しない|`fs/isofs/iso9660.c:80–90`、`drivers/base/dev.c:353–358`|

以下の新規関数・構造体は提案契約。既存宣言との一致を未確認の箇所まで主張しない。

### 2. 到達点と波

|到達点|受入の意味|
|---|---|
|S-A：常駐 CLI|LAN 端末で echo→次prompt、RO root の読取り、`echo text > /tmp/f`→cat による読み戻し|
|S-B：外部 USER|root 内の PPC USER アプリを起動し、終了値を受けて呼び手へ戻る。S-B の受入は開発用の小さな CLI (KBOOT_SHELL=1) から行ってよい|
|後続|既存 shell の USER 化、永続保存、FB・鍵盤、実ディスク、動的 module、GUI|

**不変条件：起動の後は起動媒体を読まない。FD・CD を抜いても動く。**
kernel に入るまでに kernel・root の全量を RAM に写す。後から明示的に mount する保存媒体は別の利用経路とする。

|波|内容|前提・並列|大きさ|
|---|---|---|---|
|W0|契約固定、②と S-A の build 分離|先行、実装分担の基準|小|
|W1|端末 codec、RAM Device、image、CLI、ramfs、PPC compile の先行確認|契約確定後は並列可|各小〜中|
|W2|LAN service、専用入力、stream、root・/tmp mount、像同梱|W1 と先行 compile 完了|各小〜中|
|W3|PPC 起動結線、閉包リンク、S-A 通し受入|統合は直列|中|
|W4|USER ABI・SDK・exec 詳細設計|S-A 後、別の往復で詳細設計|大|
|W5|USER 最小実装→外部 hello→終了→呼び手 (開発用 CLI) へ復帰|W4 後、複数波へ再分解|大|
|W6|PC-98 FD loader から共通 RAM root へ接続|全量読込み・容量・boot 情報を別設計|中〜大|
|W7|既存 OS32 shell を PPC USER の sh.bin として組み、製品の起動は root を mount した後 kernel が sh.bin を直接起動 (x86 の `init/kernel.c:430` と同じ形、小さな CLI を挟まない)|S-B 後。`SHELL_AS_APP` と USER 入出力を接続。前提: InputOps の登録 (19 本すべて) と USER の stdout を stream へ|大、再分解|
|W8|書ける媒体を後から mount して保存|PPC は Host Services の LAN 版 `/host`、PC-98 は既存 FD/HDD/Host Services|輪郭のみ|
|W9|docs/tasks/agents/PORT_FLOW.md に新しいアーキテクチャの立ち上げの順 (起動 → RAM root → 開発用の小さな CLI (resident_cli_ops をつなぐ) → USER アプリ → 本物のシェル) を短く足す|W7 後|小 (文書)|

- S-A は小さな CLI を kernel に静的リンクする。PPC resident OS32X/CRT/KAPI を先行実装しない。
- 一段は主ファイル一つ・一機能・一実装者。小＝約50〜150行、中＝150〜300行。工数確約ではない。
- S-B は整数 USER ABI、argc/argv/api/aux、syscall、PR 遷移、親への復帰を定義する。
- AS 切替、共有 kernel mapping、USER stack、fault 時の子だけの終了、資源回収も S-B に含む。
- USER と必要なライブラリを soft-float/no-AltiVec で揃え、命令公開時の cache 同期を定義する。
- OS32X の LE wire と CPU の BE を分離。ELF は EI_DATA に従い、CPU/ABI 識別を確定する。
- `docs/09_exec.md:20,39–54,64` の親への復帰を維持。暫定保存域を ABI 承認としない。
- ③の負債は `PPC_KBOOT_DESIGN.md:165,244–246`。S-A 完了を USER 基盤の完成とは数えない。
- 製品の起動は小さな CLI を通らない。小さな CLI は KBOOT_SHELL=1 のときだけリンクされる開発用の経路 (新しいアーキテクチャの立ち上げ用)。`SHELL_AS_APP` は `kbd_getkey()` 経路を使う。
- `/tmp` は揮発性。残す物は W8 の媒体へ書く。S-A に保存プロトコルを足さない。

### 3. LAN 端末：線上形式と所有

- PLN1 の80B header・BE整数・最大256B payloadを維持。診断 REQ/RSP と cache は変更しない。
- 追加型は端末専用。`pm_lan_monitor()` を任意コマンド実行へ拡張しない。
- 出力は「届く分だけ＋欠落数」。入力も自動再送せず、欠落した未完行を実行しない。
- 単一ホスト・単一前景入力。認証、複数端末、再接続履歴、再送窓は追加しない。

```c
/* platform/powermac/lan_boot.h：数値は提案 */
enum { PLN1_TERM_OPEN=5, PLN1_TERM_IN=6,
       PLN1_TERM_OUT=7, PLN1_TERM_STATE=8 };
/* OPEN: payload=0、host_nonce=新世代、seq=0
 * IN: payload=入力bytes、seq=入力frame番号（0開始）
 * OUT: payload=出力bytes、seq=出力chunk番号（0開始）
 * STATE: seq=次の出力番号、payload=BE64 in_next/in_drop_frames/in_drop_bytes
 * OUT/STATE の drop_records/drop_bytes は端末出力の累計。
 * 全型 boot_id/host_nonce 使用。request_id/opcode/status/flags=0。 */
void pm_term_receive(const struct lan_message *, const u8 src[6]);
void pm_term_write(const u8 *, u32 bytes);  /* 有界enqueue */
int  pm_term_tx_peek(struct lan_message *);
void pm_term_tx_complete(void);           /* gem_tx受理後だけ除去 */
```

|所有者|状態・責務|
|---|---|
|`lan_service.c`|GEM 状態機械、Ethernet envelope、boot/peer 照合、送信予算|
|`lan_terminal.c`|端末 MAC/nonce、入力 next_seq、出力 FIFO・seq/drop、STATE|
|`terminal_input.c`|入力リング、欠落後の行破棄、選択した入力源、CLI 専用 getkey|
|CLI|編集中の1行、実行、prompt。NIC 状態を持たない|
|ホスト|boot/nonce、入力 seq、出力 seq の穴、raw 保存、tty 状態|

- OPEN は端末世代・入力を初期化し、CLI へ取消イベントを渡す。STATE は新 nonce を反射する。
- ホストは一致する STATE 後に入力開始。OPEN は自動再送せず、未接続なら明示的に再接続する。
- 異なる nonce の遅延 IN を捨てる。ログの peer 宛先を端末接続で変えない。
- `seq < in_next` は破棄。`seq > in_next` は欠落を記録して行取消。
- 欠落・入力リング不足時は次の LF まで捨てる。破片を連結して実行しない。
- 欠落 frame の byte 数は不明。実廃棄 byte 数と seq の穴を別表示する。
- 出力 FIFO は16×256B。満杯では新着を捨て、仮想chunk分の seq と実廃棄数を進める。
- OUT は `gem_tx` 受理後に除去。線上喪失は再送せず、host の穴と guest drop を加算しない。
- STATE は接続時と以後1秒ごと。最後の OUT 喪失も next_seq で分かるが、完全配送は保証しない。
- **LOG と OUT は交互、空なら他方で計4試行まで。STATE は BEACON と同じく枠の外。TX AGAIN ならその回の送信列を終える。**
- RX→RSP→LOG→BEACON に OUT/STATE を加える。入力は診断応答枠に依存させない。
- `lan_service.c:86–89` の REQ 限定分岐を型別 dispatch に変更。`lan_monitor.c` は無変更。

### 4. ホスト・入力待ち・stream・ESCC

```python
# tools/ppc_lan_host.py：既存 UDP / AF_PACKET を共用、--terminal を追加
class Terminal:
    def open(self): ...                    # 新nonceでOPEN
    def send_bytes(self, data: bytes): ...  # 最大256B、CR→LF
    def receive(self, message): ...         # OUT表示・保存、STATE欠落表示
# select(socket, stdin)。tty raw/no-echo、finallyで復元。
# Ctrl-] はhost終了、Ctrl-Cはguestへ。診断はstderr。診断モードは従来どおり。
```
```c
void console_stream_write(const void *bytes, u32 len);
void pm_terminal_poll(void);  /* pm_lan_poll(TB)、選択時だけESCC RX */
void pm_terminal_input_init(enum terminal_source source);
void pm_terminal_feed(const u8 *bytes, u32 len);
void pm_terminal_line_lost(void);
static int terminal_getkey(void) {
    int c;
    while ((c = terminal_trygetkey()) < 0) pm_terminal_poll();
    return c;
}
/* byte=0..255、行取消は別の非負内部値。負値は未着。 */
```

- S-A は `resident_cli_ops.getkey` に直結し、InputOps を登録しない。`kbd_inject` も経由しない。
- 取消値は CLI 内部だけで使い、KAPI のキー値へ混ぜない。InputOps 登録・キー対応は S-B で定義する。
- stream は LAN enqueue と既存 ESCC TX へ接続。ログ sink へ二重投入しない。
- 座標型 `con_backend_*` を空実装で満たさず、CLI はその API を呼ばない。
- 入力待ちと行実行の前後で poll。enqueue 中・LAN callback 内・IRQ から NIC を回さない。
- 起動設定で LAN/ESCC の入力源を一つ選ぶ。出力は双方へ出せる。入力を同じ行へ混ぜない。
- ESCC は QEMU の補助端末。RX は既存設定を使う非待機1byte受信。ESCC 待機中も LAN poll を続ける。
- RX 実装前にメーカー資料と BSD 実装を照合する。実機配線・動作は QEMU 合格から外挿しない。

### 5. 共通 RO RAM root：ISO9660

|比較|ISO9660|FAT|
|---|---|---|
|小ささ|約数十KiBの固定費、既存 parser、自然な RO|小ファイルの像は小さいが FatFs＋adapter|
|BE|既存 `le16_rd/le32_rd`|FatFs・diskio・区画処理の確認が必要|
|mount|`cdN` を扱えば Device read へ届く|FDC・PC98 区画・drive 選択が残る|
|FD 再利用|FD は運搬媒体、RAM 内 ISO を読む|同様。運搬媒体と root の FS は別でよい|
|判断|S-A の実装量が小さいため採用|FD 全体容量が制約になった時点で再比較|

根拠：`iso9660_media.c:69–79,109–117`、`fatfs_vfs.c:20,595`、`diskio.c:24–25`。

```c
/* include/boot_root.h：呼び手がVFS初期化・名前登録済み */
struct boot_root_image { const u8 *base; u32 bytes; };
/* baseはplatformが予約・写像済み。起動中不変、解放しない。 */
int boot_root_mount(const struct boot_root_image *image);
/* drivers/block/ram_ro.h */
struct ram_ro { Device dev; const u8 *base; u32 bytes; };
int ram_ro_register(struct ram_ro *, const struct boot_root_image *);
int ram_ro_read(struct ram_ro *, u32 lba, u32 count, void *dst);
/* DEV_BLOCK、DEV_BUS_NONE、sect_size=2048、total_sects=bytes/2048。
 * dev_register_cd(&r->dev, 0) でcd0登録。media_check=NULL、世代0。
 * blk_readは既存署名へadapter。範囲内をkstring系copy、writeはROエラー。 */
```

- `boot_root_mount()` が静的 RAM Device 一個を所有し、登録→ISO 登録→`vfs_mount("/", "cd0", "iso9660")`。
- `cd0` は VFS の互換論理名。ATAPI・媒体交換を偽装しない。任意名 mount 改修は含めない。
- **PPC は `dev_init()` を呼ばない**。同関数は fd0/fd1 を登録する。Device の初期表は BSS を使う。
- 名前表は共通 boot_root.c に置かず、platform/powermac/root_boot.c が mount 前に登録する。
- image 長は2048B整列。block read の LBA 範囲を境界で検査する。dev.h は変更しない。
- cat は一括 VFS read を使う。`loop_dev.c:295,380` は file-backed なので root の下回りに使わない。

### 6. image 作成・予約・受渡し

```python
# tools/mkroot_image.py：既存ISO作成ツールを呼ぶ薄いwrapper
# ISO Level 1、Rock Ridge/Jolietなし、boot catalogなし、末尾paddingなし。
# S-A：/etc/motd（ASCII、1KiB以下）と空の/tmp。S-B：/bin/hello.bin追加。
```
```c
/* platform/powermac/root_image.c */
extern const u8 __root_image_start[], __root_image_end[];
void pm_root_image(struct boot_root_image *out);
/* root.isoを.rodata.boot_rootへ収容、KEEPし__kernel_resident_endより前。
 * linkerのRAM写像契約でbaseを設定。裸のPA castを散在させない。 */
```

- 最初は起動 ELF に同梱。OF が detach 前に全 ELF を読むため、起動後に媒体を読まない条件を満たす。
- `kboot_reserve.c:45–55,69–70` の image 全体予約に blob を含める。複製・展開・別予約表は不要。
- map で root の予約内収容、固定窓・arena との非衝突を確認。容量超過は PM が配置を再設計する。
- kboot_reserve.c は原則無変更。memmap.h・config.h は変更しない。
- 将来 OF 別読みを使う場合も、全量を予約済み RAM に読み、detach 前に受渡しを終えて kernel に入る。
- W6 の PC-98 FD loader も kernel＋root を全量 RAM に写してから kernel に入る。
- PC-98 platform が allocator から除外・写像し、同じ boot_root_image と boot_root_mount に渡す。
- boot 情報の版と受渡しは W6 で定義する。S-A で x86 boot を変えない。

### 7. 常駐 CLI

```c
/* kernel/cli/resident_cli.h：kernel内部口 */
struct resident_cli_ops {
    int  (*getkey)(void);
    void (*write)(const void *, u32);
    void (*poll)(void);
    int  (*cat)(const char *path);
    int  (*put)(const char *path, const void *data, u32 size);
};
void resident_cli_run(const struct resident_cli_ops *ops);
/* resident_cli.c：char line[256]、現在長、行破棄状態。
 * static constコマンド表はhelp/echo/cat。未知コマンドは一行エラー。 */
/* cli_boot.c：catはvfs_read(path, buf, 1024)、返った長さだけstreamへ。
 * putはvfs_write(path, data, size)。FDを開かず、chunk間pollなし。 */
```

- PPC の既存 entry/stack/BSS 初期化を使い、root・/tmp mount 後に通常の C 呼出しで開始する。
- SDK profile、新 CRT、resident linker script、KAPI table、newlib は S-A に不要。
- `userland/shell/main.c/ui.c/sh_exec.inc` は無変更。既存 shell の全コマンド登録は呼ばない。
- prompt は `os32> `。印字可能 ASCII、Enter、BS/DEL、Ctrl-C。履歴・補完・IME は後段。
- BS は `"\b \b"`、Enter は改行、Ctrl-C は行取消。座標・画面寸法を参照しない。
- 行長超過は LF まで破棄して一行エラー。切り詰めたコマンドを実行しない。
- 引数は空白区切り。echo は残りの文字列を表示する。引用・展開・pipe は未対応。
- 書込み構文は `echo text > /tmp/f` の一形だけ。末尾の `> ファイル` を解釈し、text＋LF を一括上書きする。
- 出力先は `/tmp/` 直下の一ファイル。`>>`、`<`、FD 指定、複数 redirect、他コマンドの redirect は未対応。
- `cat /etc/motd` と `cat /tmp/f` を使う。S-A の cat は最大1024B。profile/history 保存は行わない。
- S-B で外部起動口を追加する。W7 の後の製品の起動は kernel が sh.bin を直接起動し、この CLI は開発用の経路としてだけ残る。
- `kernel/cli/` は CPU・機種に依らず、resident_cli_ops 以外 (platform・arch のヘッダ) を include しない。KBOOT_SHELL を付けないときはリンクに入らない (A23 で選ぶ)。新しいアーキテクチャは resident_cli_ops に自分の端末と poll をつなぐだけで同じ立ち上げの順を取れる。

### 8. 小さな ramfs と mount 境界

```c
/* fs/vfs.h：既存番号を維持して追加 */
#define VFS_DEV_RAM 5
/* platform/powermac/root_boot.c：静的寿命。mount前に一度登録 (常に組む、製品の経路でも root は要る) */
static const struct vfs_device_name pm_names[] = {
    {"cd",  VFS_NAME_DIGIT_PREFIX, VFS_DEV_CD,  0, 1},
    {"ram", VFS_NAME_DIGIT_PREFIX, VFS_DEV_RAM, 0, 1},
};
/* vfs_device_names_register(pm_names, 2);
 * boot_root_mount(&image);
 * vfs_register_fs(&ramfs_ops); vfs_mount("/tmp", "ram0", "ramfs");
 * 各戻り値を確認し、失敗時はCLI開始へ進まない。 */

/* fs/ramfs/ramfs.h：上限と公開口 */
enum { RAMFS_FILES=16, RAMFS_FILE_MAX=1024,
       RAMFS_DATA_MAX=16*1024, RAMFS_META_MAX=4*1024 };
extern VfsOps ramfs_ops;
/* fs/ramfs/ramfs.c：内部関数の境界 */
static void *ramfs_mount(int dev_id);
static void ramfs_umount(void *ctx);
static int ramfs_is_mounted(void *ctx);
static int ramfs_read(void *ctx, const char *path, void *buf, u32 max_size);
static int ramfs_write(void *ctx, const char *path, const void *buf, u32 size);
static int ramfs_list(void *ctx, const char *path, vfs_dir_cb cb, void *user);
static int ramfs_stat(void *ctx, const char *path, OS32_Stat *out);
VfsOps ramfs_ops = {
    .name="ramfs", .mount=ramfs_mount, .umount=ramfs_umount,
    .is_mounted=ramfs_is_mounted, .read_file=ramfs_read,
    .write_file=ramfs_write, .list_dir=ramfs_list, .stat=ramfs_stat
};
```

- mount は encode 済み dev_id の型 `VFS_DEV_RAM`・番号0を受け、kmalloc した context を返す。
- `/tmp` 直下の通常ファイルだけ。名前は最大31B、項目数16固定。下位 directory・rename・削除・stream は S-A に足さない。
- 項目表を含む context は4KiB以内。各項目は初回書込みで1024Bを kmalloc し、上書き時は再利用する。
- データ確保総量は16KiB以内、context と合わせ20KiB以内。umount で全て kfree する。
- write は作成または全置換。サイズ・項目・割当上限超過は `VFS_ERR_NOSPC`、失敗時は旧内容を保つ。
- read は先頭から `min(size, max_size)` B、戻り値は読んだ長さ。write の成功値は書いた長さ、失敗は VFS エラー。
- heap 192KiB の予算は ISO 先読み32KiB、ramfs 最大20KiB、残り140KiBを他用途・allocator 管理へ分ける。
- root blob は予約済み像内にあり heap を使わない。実測で不足すれば予算を見直し、heap 定義を拡張しない。
- FatFs の FDC/IDE 固定 diskio・format、ext2 の規模・hdN/pc98pt 依存を持ち込まない。
- ISO の tick 単位修正は行わない。固定 RAM は media_check=NULL・世代0。時間の負債は既存 PPC 設計に残す。

### 9. 大きさの設計予算

|部品|PPC ファイル増分|追加常駐 RAM|
|---|---:|---:|
|端末 protocol・入力・stream（host除外）|8〜20KiB|5〜8KiB|
|CLI・VFS 接続|4〜12KiB|2〜8KiB＋既存stack|
|Device/VFS/ISO 閉包|30〜100KiB|16〜64KiB程度、ISO先読みを含む|
|ramfs|2〜6KiB|最大20KiBのheap要求＋allocator管理分|
|root.iso|48〜80KiB|同量、像内を直接参照|
|合計|約92〜218KiB|像＋概ね43〜100KiBと管理分、重複を除いて実測|

- 未計測の予算。既存 kboot、BSS、link alignment は別に加える。
- PC-98 FD は1.2MB形式で1,261,568B、1.44MB形式で1,474,560B。kernel＋loader＋root の一枚収容は未確認。
- W6 条件は `loader領域＋kernel格納量＋root格納量＋外側FS管理量 <= FD容量`。
- root 80KiB は1.2MB側の約6.5%以下。S-B のアプリは再計測。圧縮は全体超過時に費用を比較する。

### 10. ファイル別の実装段

各行＝一段＝一実装者。`†` は**共通の場所の部品を出す前に本線に知らせる**段。A09・A13・A14 は削除。

|段/波|新規・変更ファイル|一機能・前提|概算行|
|---|---|---|---:|
|A01/W0|変更 platform/powermac/lan_boot.h|端末型・口|35|
|A02/W0 †|新規 include/boot_root.h|呼び手の名前登録を含む契約|20|
|A03/W0 †|新規 drivers/block/ram_ro.h|RAM Device 契約、A02|25|
|A04/W0 †|新規 kernel/console/console_stream.h|stream 契約|15|
|A05/W0 †|新規 kernel/cli/resident_cli.h|CLI・cat・put 契約|30|
|A06/W1|変更 platform/powermac/lan_wire.c|追加型 codec、A01|25〜50|
|A07/W1|新規 platform/powermac/lan_terminal.c|端末世代・FIFO・統計|220〜280|
|A08/W1|変更 tools/ppc_lan_host.py|端末モード・codec|140〜220|
|A10/W1 †|新規 drivers/block/ram_ro.c|RO block、dev_register_cd|100〜160|
|A11/W1|新規 tools/mkroot_image.py|image生成、空の/tmp、サイズ報告|60〜100|
|A12/W1 †|新規 kernel/cli/resident_cli.c|行編集、3コマンド、echoの一形|200〜280|
|C1/W1|既存 dev.c・iso9660.c の compile 確認|W2 前に PPC flags で試す。共通修正は出す前に本線通知|確認段|
|A15/W2|新規 platform/powermac/terminal_input.c|専用入力・欠落行破棄|140〜200|
|A16/W2|新規 platform/powermac/console_stream.c|LAN/ESCC 出力|40〜70|
|A17/W2|変更 platform/powermac/escc.c|資料照合後の非待機RX|25〜60|
|A18/W2|変更 platform/powermac/lan_service.c|dispatch・送信予算|50〜90|
|A19/W2 †|新規 init/boot_root.c|共通root mount、A10|60〜100|
|A20/W2|新規 platform/powermac/root_image.c|像内blob受渡し|25〜40|
|A21/W2|変更 arch/ppc/kernel.ld|section保持・symbol|10〜20|
|A22a/W2|新規 platform/powermac/root_boot.c|名前の表の登録と mount の順 (root → /tmp)。常に組む|40〜70|
|A22b/W2|新規 platform/powermac/cli_boot.c|resident_cli_ops の adapter (getkey・write・poll・cat・put)。KBOOT_SHELL=1 のときだけ組む|70〜110|
|A23/W3|変更 build/ppc-kboot.mk|選択構成・blob・閉包リンク|50〜90|
|A24/W3|変更 init/ppc_kernel.c|S-A 選択時の開始|10〜25|
|A25/W0 †|変更 fs/vfs.h|VFS_DEV_RAM 追加|1〜3|
|A26/W0 †|新規 fs/ramfs/ramfs.h|上限・ops 宣言|20|
|A27/W1 †|新規 fs/ramfs/ramfs.c|一階層ramfs、A25/A26|200〜300|
|H1/W2|新規 tools/tests/test_ppc_terminal.py|端末host試験|150〜230|
|H2/W2|新規 tools/tests/test_ram_root.py|RAM/ISO＋ramfs製品コード試験|150〜220|
|H3/W2|新規 tools/tests/test_resident_cli.py|CLI製品コード試験|90〜140|
|Q1/W3|新規 tools/ppc/shell_run.py|S-A通し受入|120〜180|

- A23 の既存 TU は `fs/vfs.c`、`fs/vfs_fd.c`、`drivers/base/dev.c`、`fs/isofs/iso9660.c`、`fs/isofs/iso9660_media.c`。新規 TU は上表の S-A 実体を加える。
- vfs.c の FD 参照により vfs_fd.c も候補閉包へ入る。gc-sections による消去と残る推移依存は実リンクで確認し、万能stubで埋めない。
- 追加修正が必要なら一ファイル単位で追補し、本線通知と PM の規模更新を行う。表をリンク成功の証拠にしない。
- `KBOOT_SHELL=1 KBOOT_TERM=lan|escc` を make 変数とし、②・S-A LAN・S-A ESCC の出力先を分ける。S-A の既定入力は lan。
- ②の既存構成・9印を保持。x86 生成物不変を目標に比較する。生成 KAPI・SDK profile・既存 shell build は触らない。

### 11. 最小の受入・試験・変異

|受入項目|試験と一行変異|
|---|---|
|端末双方向・欠落行取消|H1：穴・重複・満杯。変異：入力欠落時の取消を削除|
|出力seq/drop・診断共存|H1：OUT廃棄・STATE・RSP。変異：廃棄時seq加算を削除|
|RO root・ISO読取り|H2：生成imageを製品RAM Device/ISOで読み、write拒否。変異：コピー元LBAを1ずらす|
|ramfs作成・上書き・上限|H2：読戻しと容量境界。ramfsの変異は1つ：サイズ上限判定を無効化|
|CLI一行実行・次prompt|H3：echo・BS・取消・長過ぎる行・`echo text > /tmp/f`。変異：実行後の行長初期化を削除|
|PPC S-A・BE・待機中LAN|Q1：7450/7455各一回、echo→待機中CLOCK→cat motd→書込み→cat /tmp/f→次prompt。変異：入力待ちpollを削除|
|ESCC補助経路|Q1：7450一回、echoとLAN診断継続。変異：RXを常に未着へ変更|
|起動媒体非依存|同じQ1で起動後に媒体を利用不能にして読書き列を実行。起動後の媒体readなし。OF別読み・W6も同条件を各受入へ継承|

- packet・FIFO 故障は host 試験に集約。QEMU では重ねず、既存9印・5診断を再利用する。
- 媒体なしの ELF 直接起動だけを「抜去試験済み」と数えない。実機 FD/CD 抜去は各実機段で確認する。
- 予約収容・サイズ・未解決0・FP/AltiVec不使用は製品ELF/mapで確認。専用ゲスト試験を増やさない。
- 実装者は対象build、触った試験の `--mutate`、`python3 -B tools/checks/run.py --lint`。CHECK に入力を宣言する。
- PM の統合build・CIへ進める。ローカル全変異・check-changed は実行しない。
- 今回は設計のみで preflight・全試験未実施。QuickSilver 実機、PC-98 FD一枚起動、S-B は S-A の実施済み受入に含めない。

### 12. 未決と PM の確定事項

- ユーザー判断が必要な未決は0件。書込みの一形と S-B 後の sh.bin、小さな CLI は製品の経路を通らない開発用の経路 (他のアーキテクチャの立ち上げにも使う) はユーザー決定済み。
- ISO・像同梱・cd0・専用静的CLI・256B行・4KiB出力FIFO、小さな `/tmp` ramfs を本案とする。
- PM が確定するもの：端末型番号、ISO作成ツール、試験ファイル名、実測後の容量予算。
- 実サイズ、ESCC RX 手順、OF load-base と拡大image末尾の関係は実装段で確認する。
- 実機作業の個別承認は実機段で扱う。今回の設計を止める確認事項にはしない。

## 確定 (2026-11-11、Opus 5.5 の第 2 回の条件付き Approve を PM が反映)
- ユーザー決定「小さなCLIの経路は最終的に通らないように。ただし、他のアーキテクチャ開発時に同じ開発経路を取れるようにはしておきたい。」を反映: 製品の起動は W7 の後に kernel が sh.bin を直接起動、小さな CLI は KBOOT_SHELL=1 のときだけ組む開発用の経路。
- P2-a: cli_boot.c から root_boot.c (名前の表と mount の順、常に組む) を分けた (A22a・A22b)。
- 文言 5 か所 (§2 S-B の行、W5、W7、§2 の保守の窓口の行、§7 の常駐 CLI を残す行、§12) を書き換え、kernel/cli の独立と KBOOT_SHELL の選択を 1 行足した。
- W9 (PORT_FLOW の立ち上げの順) を足し、W7 に InputOps (19 本) と USER の stdout の前提を書いた。
