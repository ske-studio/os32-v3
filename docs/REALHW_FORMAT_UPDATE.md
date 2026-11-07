# 実行形式が変わる版を実機に入れる (手で写してから hsync)

> 状態: **現行 (2026-11-08)** — OS32X ヘッダの形式 (`sdk/kapi.json` の `os32x_format`) が変わる版を、HDD 起動のまま実機へ入れる手順の正典。普段の更新 (boot → 再起動 → 全体) はスキル os32-realhw-update、FD からの復旧は hsync の FD 復旧の経路 (`userland/system/hsync.c` の案内)。

## 1. いつ使うか

`os32x_format` が変わる版 (例: 2026-11-08 の v4 → v5、アプリの .text を読み取り専用にした版) は、普段の手順で入らない。

- 古い hsync は、配備元の `/host/sys/shell.bin` のヘッダが自分と同じ形でないと `reason=shell_header_unconfirmed … 0 files written` で断る (`userland/system/hsync.c` の `running_generation_gate`)。`--root` で同じディスクを指しても同じ。
- 新しいカーネルは古い形式のアプリ (シェルを含む) を `rebuild required` で断る (`exec/os32x_hdr.c`)。
- 新しい hsync は新しい形式なので、古いカーネルでは動かない。

KAPI の版が上がるだけの版、配置の世代 (`memory_layout`) が変わる版は、ここではなくスキル os32-realhw-update の世代の表に従う。

## 2. 手順

前提: 配備元 (実機では LAN 版 SerialFS の `/host`、NP21/W では HostDrv) に、入れる版の一式を置いてある。実機の操作はノートのセッションが行い、`/boot` の上書きはそのセッションでユーザーの許可を受ける。

1. 古い OS のまま、古い `cp` で起動に要る物を写す (どれも rc=0 を確かめる)。

   | 写す物 | 理由 |
   |---|---|
   | `/boot` の全部 (`vmkernel.lz4`・`kdebug.bin`・部品の `.mod` 全部) | 部品はカーネルの像の CRC と対で照合される (`kernel/module/module_image.c` の pair)。sqlite と fep は起動に要る |
   | `/sys/shell.bin` | カーネルが起動で読む (`init/kernel.c` の `exec_run(SYS_SHELL_BIN)`) |
   | `/sys/lib/libos32gui.shlib` | GUI に要る (古いままだと CUI に落ちる)。シェルまでなら無くてもよい |

   ```
   cp /host/boot/<各ファイル> /boot/<同じ名前>
   cp /host/sys/shell.bin /sys/shell.bin
   cp /host/sys/lib/libos32gui.shlib /sys/lib/libos32gui.shlib
   sync
   reboot
   ```
   `unicode.bin`・`fep.dic`・`splash.osri` は形式に依らないので写さなくてよい (変わっていれば次の hsync が写す)。
2. 再起動の後の `ver` が新しい Commit・SERVICES_READY であることを確かめる。古いアプリは `rebuild required` で断られる (正しい動き)。
3. 新しい hsync を配備元から直接動かして全体を同期する (`/sbin/hsync.bin` はまだ古い)。実機は `sfs run /host/sbin/hsync.bin --fep-off`、続けて `sfs run /host/sbin/hsync.bin sys`。errors=0 を確かめる。
4. もう一度、今度はディスク上の hsync で `hsync --fep-off` を回し、copied=0・errors=0 を確かめて再起動する。
5. `ver`・`mem` (selftest_fail=0・leftover=0・audit_fail=0) と、その版で変わった所の試験 (v5 なら `time_test ro` と `text_ro_test text` が kill される) を確かめる。

## 3. 止まったとき

- 手順 1 の途中や再起動の後に起動しなくなったら、FD 復旧の経路 (FD で起動 → `sfs lan on` → `sfs run hsync --root /hd0 --verify --no-backup`) で戻す。FD の差し替えは物理の操作なのでユーザーに頼む。
- 戻す先の版は、版上げの前に打ったタグ (例 `pre-text-ro-20261107`) の成果物。

## 4. 予行

NP21/W で 2 回通した (2026-11-08、v4 `c7585a3f` → v5 `d4a4cc96`、2 回目は各段で `e2fsck -fn` がきれい)。証拠は `~/os32-tmp/evidence/2026-11-08/manual-boot/`。NP21/W には 82557 が無いので、手順 3 の `sfs run` は HostDrv の `/host/sbin/hsync.bin` の直接起動で代えた。
