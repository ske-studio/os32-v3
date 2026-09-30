# F2a FD lease — HOST TDD 証跡

対象: 実 `fs/vfs_fd.c` を include する `vfs_fd_sqlite_host.c`。
backend／resolve／route／path_kind／TTY／res_owner 境界のみ mock。
SQLite、exec、FEP、実媒体は使用していない。公開 KAPI/SDK は変更なし。

## RED → GREEN の実行記録

各行の RED は `python3 tools/tests/test_vfs_fd_sqlite.py <case>`。
いずれも `COMPILE GNU89 -Werror PASS` の後、下記 assertion で exit 1。
当該最小実装の後に、その時点の既存全 case を実行してすべて exit 0 を確認した。
行番号は当時の出力（後続追加で移動）。

| case | 実観測した最初の RED | GREEN 変更 |
|---|---|---|
| explicit_owner | `explicit_owner:47: open_files[lease.fd].owner == 1` | 共通 open に owner を明示引数として渡す |
| direct_close | `generic_barrier:62: open_files[lease.fd].in_use == 1` | SQLITE class の直接 close 拒否 |
| owned_close | `generic_barrier:62: open_files[lease.fd].in_use == 1` | SQLITE class の owner cleanup 除外 |
| protect | `generic_barrier:59: vfs_fd_set_protect(lease.fd, 1) == VFS_ERR_INVAL` | SQLITE は protect 設定／解除を拒否 |
| verified_close | `verified_close:76: vfs_close_sqlite(&lease) == VFS_OK` | cookie 両成分／FD generation／class／in_use 検証後の専用 close |
| stale_reuse | `stale_reuse:92: vfs_close_sqlite(&old) == VFS_ERR_INVAL` | 同じ整数 FD・同じ cookie の再openでも FD generation を進める |
| validation | `validation:112: vfs_validate_sqlite(&lease) == VFS_OK` | 副作用ゼロの検証入口を close と共有 |
| count_members | `count_members:140: vfs_count_sqlite(&a) == 2` | 生存 SQLITE FD の cookie ごとの数え上げ |
| quarantine | `quarantine:164: vfs_validate_sqlite(&y) == VFS_ERR_INVAL` | 残存集合だけ sticky quarantine、専用 close／validate 拒否 |
| capacity_preflight | `capacity_preflight:195: probes == 0` | FD 容量確認を resolve/route/path_kind より前へ |
| generation_exhaustion | `generation_exhaustion:211: vfs_open_sqlite("/exhausted", O_CREAT | O_RDWR, 1, &cookie, 7, &lease) == VFS_ERR_NOSPC` | MAX 世代の空き FD は退役、wrap せず他スロットへ |
| invalid_open | `invalid_open:235: vfs_open_sqlite("/invalid", O_RDWR, 1, &cookie, 7, &lease) == VFS_ERR_INVAL` | 不正 owner/cookie／NULL 引数は probe 前に拒否 |

新 API がない最初の RED を compile failure で代用しないため、host harness にのみ一時的な
bootstrap を置いた。最初の open bootstrap は既存 `vfs_open` を呼び、既存 FD 表の owner を
検査した。close／validate は INVAL、count は 0、quarantine は無操作 OK の仮実装だった。
本番に仮実装を入れず、実装を接続して GREEN 後に bootstrap と feature macro は全撤去した。
最終 harness は FD API を一切 mock しない。

`direct_close/owned_close/protect` は同じ class 障壁の三入口として一緒に RED を採取、
同じ最小変更で GREEN にした。他の新挙動は一つずつ RED→GREEN。

追加の `generic_regression`、`failed_open`、`quarantine_capacity` は既存挙動／組合せの
回帰・特性確認で、初回から PASS。これらを新規 RED の証拠とは数えない。

## 最終実行

```text
$ python3 tools/tests/test_vfs_fd_sqlite.py --target
COMPILE GNU89 -Werror PASS
TARGET i386-elf GNU89 -Werror PASS
PASS explicit_owner
PASS direct_close
PASS owned_close
PASS protect
PASS verified_close
PASS stale_reuse
PASS validation
PASS count_members
PASS quarantine
PASS capacity_preflight
PASS generation_exhaustion
PASS invalid_open
PASS generic_regression
PASS failed_open
PASS quarantine_capacity
SUMMARY 15/15 PASS
exit 0
```

上記は各 `EXIT <case>=0` 行だけ省略した実出力。
`git diff --check -- fs/vfs_fd.c fs/vfs.h docs/tasks/settings/F2_OWNERSHIP.md tools/tests/vfs_fd_sqlite_host.c tools/tests/test_vfs_fd_sqlite.py`
も exit 0、出力なし。未追跡ファイルは git diff では検査対象にならないため別途末尾空白を検査する。

Host flags: `-std=gnu89 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare
-Wdeclaration-after-statement -D__cdecl=`。
既存 FD の未使用 attr、fstat の signed/sizeof 比較、mock 境界の未使用引数を抑制する。
ホストの `u32` は既存 `unsigned long` のため LP64 上では64bit。
世代上限は明示32bit値で検査し、型サイズ／ターゲット compile は別の i386-elf で確認する。

Target flags は `build/config.mk` の共通／kernel 素性に基づき、
`-std=gnu89 -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector
-nostdlib -mno-red-zone -fcommon -O2 -Wall -Werror -Wdeclaration-after-statement
-D__KERNEL_BUILD__`。`fs/vfs_fd.c` 一個を一時 directory に `-c`。
Make、環境自動読込、既存 build 出力の上書きは行わない。target の警告抑制は追加しない。

## 確認範囲と限界

- current owner=2、managed owner=1、境界で current owner が変わらず setter 呼出しゼロ。
- stale close は同じ cookie の新 SQLITE FD と新 GENERIC FD の両方を変更しない。
- validation/count の前後で FD 表全体と probe 数を比較。不一致 close の対象も byte 比較。
- 部分 close 後の整数 FD 再利用先は quarantine に巻き込まれない。同 owner・別 cookie の
  次資源は正常 close 可能。隔離 FD は generic／専用 close／protect／容量枯渇で解放されない。
- 満杯／全世代上限時は backend だけでなく resolve/route/path_kind もゼロ。
  最後の有効世代は一度使え、その後 wrap／再利用しない。上限注入は harness から静的表へ直接行う。
  本番に generation setter／reset／故障注入 API は追加しない。
- GENERIC owner cleanup、protected cleanup 除外、直接 close、protect 解除、seek/read/write/EOF、
  managed→GENERIC 再利用の metadata 初期化を確認。
- missing file／create backend IO error／get_file_size 未対応は FD/out を変更せず失敗。
  既存 O_TRUNC の backend error 未伝播など F3 の I/O 契約は直していない。

F2a は内部 FD 寿命の基礎のみ。group の実在／状態／cookie generation 非再利用と
隔離後の将来の open 拒否は F2b の責任で未実装。公開 read/write の認可は変更していない。
既存 VFS の非再入前提を維持し、並行 open の同期は検証していない。
独立レビューは親 PM 側の残ゲート。実 SQLite、exec 終了、FEP、full build／kernel link、
make check、emulator／配備／実機は依頼境界により未実施。F2 全体完了とは報告しない。
