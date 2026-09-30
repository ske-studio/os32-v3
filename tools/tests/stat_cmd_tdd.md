# `stat` コマンドのホスト TDD 記録

対象: `userland/cmds/stat.c` / 試験 `tools/tests/stat_cmd_host.c` + `tools/tests/test_stat_cmd.py`
実行: `python3 -B tools/tests/test_stat_cmd.py [--target] [case ...]`
土台: 実物の `stat.c` を `#define main stat_main` で丸ごと取り込み、KAPI の
`sys_stat` だけを表に差し替える。`printf` は捕まえて文字列で突き合わせる。
ホストのファイルシステム・配備・エミュレータには触らない。

背景: S3 (設定のリカバリ) は root の `st_dev` を `(dev_type << 8 | unit) + 1` で
復号して FDD ブートを判定し、`st_ino` で同一性を見る
(`docs/archive/settings/TASK_S3.md` §1a / §1b)。ゲスト上でその 2 つを観測する
手段が無かったので、実機観測の根拠を作るために足した道具。

## 事実関係 (RED の出し方)

実装と試験は同じ往復で書いた。したがって「試験だけが存在する RED」は無い。
代わりに **実装を壊して各試験が本当にその壊れ方を捕まえることを確認** し、
戻して GREEN を取った (以下の RED 1 / RED 2 はその実測)。

## RED 1 — `st_dev` の `+1` を落とす

`STAT_DEV_TYPE` / `STAT_DEV_UNIT` から `- 1u` を外す (素朴な `>> 8` / `& 0xFF`)。

```
FAIL dev: hd0 raw+decode
  output: /: DIR size=1024 dev=1(hd1) ino=2 ...
FAIL dev: fd0 raw+decode
  output: /: DIR size=1024 dev=257(fd1) ino=2 ...
FAIL dev: hd1 raw+decode     ... dev=2(hd2)
FAIL dev: serial             ... dev=513(ser1)
FAIL dev: cd                 ... dev=769(cd1)
FAIL dev: hostdrv            ... dev=1025(host1)
FAIL dev: unknown type raw+decode ... dev=1794(type7 unit2)
EXIT dev=1
FAIL multi: last entry decoded
EXIT multi=1
```

`dev=0(unknown)` だけは通る (0 は復号の手前で弾いている) — `+1` の目的
そのものなので、これは期待どおり。

## RED 2 — 1 つ失敗したら止める / 終了コードを 0 にする

`main` のループを `if (stat_one(argv[i])) return 0;` に替える。

```
FAIL notfound: exit 1
FAIL notfound: IO error
FAIL notfound: NOMOUNT
EXIT notfound=1
FAIL multi: exit 1 when one fails
FAIL multi: 3 lines (2 stats + 1 error)
FAIL multi: continued after the failure
EXIT multi=1
```

## GREEN

実装を戻して全件通る。

```
$ python3 -B tools/tests/test_stat_cmd.py --target
HOST GNU89 -Werror compile PASS (real userland/cmds/stat.c)
TARGET i386-elf GNU89 -Werror compile PASS (stat.c)
PASS dev       EXIT dev=0
PASS fields    EXIT fields=0
PASS notfound  EXIT notfound=0
PASS multi     EXIT multi=0
PASS usage     EXIT usage=0
SUMMARY 5/5 PASS
```

`--target` は `build/config.mk` の `PROGRAM_FLAGS` と同じ形 (`-O2 -Wall -Wextra
-Werror -Wdeclaration-after-statement`、i386-elf) で `stat.c` を単体コンパイル
する。`check-tools-host` からは `--target` 無しで呼ぶ (クロスコンパイラの
有無に `make check` を依存させない)。

## 固定している事

| case | 内容 |
|---|---|
| `dev` | `st_dev` の生値と復号: hd0 = 1、fd0 = 257 (TASK_S3 §1a の値)、hd1 = 2、ser0 / cd0 / host0、未知の種別は `type7 unit1` と数字のまま、0 は `unknown` |
| `fields` | 種別 (FILE / DIR / DEV / UNKNOWN = `st_mode & OS_S_IFMT`)、`size=` / `ino=` / `mode=0100644` (8 進) / `nlink=` / `atime=` `mtime=` `ctime=`、1 パス = 1 行 |
| `notfound` | `stat: <path>: <理由>` と終了コード 1。`OS32_ERR_*` は番号ではなく言葉 (NOTFOUND / IO / NOMOUNT) |
| `multi` | 途中の 1 つが失敗しても残りを続け、行数は 3 (2 件 + エラー 1)、終了コードは 1。全部成功なら 0 |
| `usage` | 引数無しで `Usage: stat PATH...` と終了コード 1 |

## 限界 (この試験で見ていない事)

- 実 VFS / 実 ext2 / 実 FAT は通っていない。`sys_stat` が実際に何を埋めるか
  (FAT / hostdrv は `st_ino` を埋めない等) はゲストでしか確かめられない。
- ホストは LP64 なので `u32` = `unsigned long` が 64bit で回っている。書式は
  `%lu` に揃えてあり、`--target` の i386-elf `-Werror` が 32bit 側を見る。
- 出力行は 80 桁を超える (ゲストの画面では折り返す)。意図どおり。
