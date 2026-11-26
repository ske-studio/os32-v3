# userland/system/install.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S3I2 (S3I2-I) の契約を今のソースと照らして要約
ソース: `userland/system/install.c`

FDD (FAT12) から起動して hd0 にシステムを入れる `install` (無印) の、FDD の中身を HDD へ写す部分の約束。
区画・IPL・ローダの扱いは CD の cdinst と共通の `inst_hdd.c`・`inst_disk.c` にある。`--recover-settings`・`--revert-settings` は `install_recover.inc` (別の設計書 `docs/design/userland/system/install_recover.md`)。

## 媒体と写し先

| FDD (媒体の綴り) | HDD |
|---|---|
| `/VMKRNL.LZ4` | `/hd0/boot/vmkernel.lz4` (ローダ v3 が ext2 から読む) |
| `/sys/boot_hdd.bin`・`/sys/loader_h.bin` | ブート領域 (IPL・ローダ) |
| `/boot`・`/sys`・`/bin`・`/sbin`・`/etc` の中身 (`copy_dirs`) | `/hd0` の下の同じ場所 |

- 生のカーネル (`/kernel.bin` を LBA 6 へ) は無い。`/kernel.bin` を開かない・stat しない。生の書き込みは区画表・ローダ・IPL だけで、ローダより後ろの LBA へ生で書かない。
- 宛先の各成分は ASCII 小文字にする (FAT は 8.3 の大文字を返し、ext2 は大小文字を区別する)。ソースは列挙で得た綴りのまま開く。
- FD だけの物 (`fd_only`: `/loader.bin`・`/etc/profile`・`/etc/netcd.cfg`) は写さない。`/etc/profile` は FDD 用の PATH (`/usr/bin` が無い) なので写すと HDD の PATH が壊れる。8.3 に合わせて改名した物 (`fd_renames`) は正規名へ戻して写す。この 2 つの表は `build/packages.yaml` の `fd.only`・`fd.rename` と対で直す。
- `/etc/settings.db` は写す (新規インストールの seed。設定レジストリの DB を作ってよい明示の操作の 1 つ)。

## 承認の前の検査 (`precheck_media`)

`/VMKRNL.LZ4`・`/sys/boot_hdd.bin`・`/sys/loader_h.bin`・`/sys/shell.bin` が在り、通常のファイルで、空でないことを `sys_stat` で見る。1 つでも欠ければ **1 バイトも書かず** (ERASE の確認も出さず) `Error: Missing <path>` などで終了 1。

## 失敗の伝播

- `copy_file` は read の負を EOF と区別して失敗に、short write も失敗にする。`/boot/vmkernel.lz4` は書いた後に stat の長さと突き合わせる (読みが正常に EOF で終わっても、媒体の申告より短ければ失敗)。
- 最初に作る 12 個のディレクトリ (`init_dirs`、親が先) のどれか 1 つでも作れなければ `Error: Failed to create <path>` で終了 1。
- `copy_directory` は `sys_ls` の負 (列挙の途中で負が返った場合を含む)・mkdir・再帰の失敗を件数で返す。1 ディレクトリの項目は `MAX_FILES` (64) まで、65 件目で取りこぼすなら失敗。深さは 4 (`/etc/d1/d2/d3/d4`) まで、5 段目は黙って写し漏らさず失敗。
- ブート領域・format・mount・`/boot` のコピー・ディレクトリのコピー・`vfs_sync` のどれか 1 つでも失敗したら `[FAIL]` を出し、`Installation complete` を出さずに終了 1。
- 承認しない (`y` 以外) なら何も書かず `Installation aborted` で終了 0 (利用者が断っただけなので失敗にしない。回復モードと同じ作法)。

## `IdeInfo` の型

`ide_identify` はカーネルの `sys/dev/ata/ide.h` の `IdeInfo` (i386 で 96 byte、末尾 `phys_sector_size` が offset 92) で書く。外部プログラムからカーネルヘッダは見えないので `install.c` に同じ並びの写しを置く。小さい型で受けると呼び手の領域を踏む。`ide.h` を変えたら写し (`userland/shell/cmd_sys.c` にもある) を一緒に直す。

## 検査

`install-fresh-host` (`test_install_fresh.py (旧版 5ea36b4bc0ef)`、本体 `install_fresh_host.c (旧版 59cb48eaa6d5)`)。実物の `install.c` を `#define main install_main` で取り込み、KAPI だけを贋物にする (`sys_ls` は FAT のように大文字を返す、`ide_identify` は 96 byte の並び `IdeIdentifyWire` で書く)。
`nokernel`・`vmkernel`・`lower`・`precheck`・`decline`・`boot_fail`・`copy_fail`・`mkdir_init`・`bounds`・`srcname`・`sync_fail`・`idetype` がこの節の約束を見る。ホストは LP64 なので型は実型との大きさ・全 offset の一致で見て、`sizeof(IdeInfo) == 96` と offset 92 は `--target` の i386-elf のコンパイル時の表明で固定する。
