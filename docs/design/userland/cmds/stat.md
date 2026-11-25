# userland/cmds/stat.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S3 (§1a・§1b の `st_dev`・`st_ino` の使い方) の契約を今のソースと照らして要約
ソース: `userland/cmds/stat.c`

`stat PATH...` は `sys_stat` が返す `OS32_Stat` を 1 パス 1 行で出す。設定の復旧 (`install --recover-settings`) が root の `st_dev` で FDD ブートを判定し、`st_ino` で同一性を見るので、その 2 つをゲストの上で観測するための道具。

## 出力

`<path>: <種別> size=<n> dev=<生値>(<復号>) ino=<n> mode=0<8 進> nlink=<n> uid=<n> gid=<n> atime=<n> mtime=<n> ctime=<n>`

- 種別は `st_mode & OS_S_IFMT`: `FILE`・`DIR`・`DEV`・`FIFO`・それ以外は `UNKNOWN`。
- `st_dev` は VFS が入れる `(dev_type << 8 | unit) + 1` を復号する (`STAT_DEV_TYPE`・`STAT_DEV_UNIT`。種別の値は `sys/fs/vfs.h` の `VFS_DEV_*` の写し)。hd0 = 1、hd1 = 2、fd0 = 257。種別ごとの接頭辞は `hd`・`fd`・`ser`・`cd`・`host`。未知の種別は `type<t> unit<u>` と数字のまま。0 は `unknown` (FS が埋めず VFS もマウントを引けなかった)。
- 32bit の値は `%lu` で出す (ホストの LP64 でも同じ書式)。1 行は 80 桁を超えうる。

## 失敗と終了コード

- 引数が無ければ `Usage: stat PATH...` で終了 1。
- stat の失敗は `stat: <path>: <理由>`。理由は番号ではなく言葉 (`stat_errstr`、`userland/shell/cmd_fs_shared.c` の `fs_strerror` と同じ文言): NOTFOUND は `No such file or directory`、IO は `I/O error`、NOMOUNT は `Not mounted` など。
- 1 つ失敗しても残りのパスを続け、1 つでも失敗があれば終了 1、全部成功なら 0。

## 検査

`tools-host` の中の `tools/tests/test_stat_cmd.py` (本体 `tools/tests/stat_cmd_host.c`)。実物の `stat.c` を `#define main stat_main` で取り込み、`sys_stat` だけを表に差し替えて `printf` の出力を突き合わせる。ケースは `dev`・`fields`・`notfound`・`multi`・`usage`。`--target` は i386-elf で単体コンパイルする。実 VFS が何を埋めるか (FAT・hostdrv は `st_ino` を埋めない等) はゲストでしか分からない。
