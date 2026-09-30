# B5 — シェルの種別判定と `cp -r` の宛先階層 (ホスト試験の記録)

- 票: [`docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md`](../../docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md)
  (票 H1) / Codex 実装レビュー 往復 3 の **B5**。退行が入ったのは `d574704`
- 上位の記録: [`h1_tdd.md`](h1_tdd.md) — H1 全体の RED→GREEN はそちら
- 実行: `python3 -B tools/tests/test_fs_kind.py [--target]`
  (`make check-fs-kind-host` が同じものを `--target` 付きで回す)
- 対象: `userland/shell/cmd_fs_shared.c` / `userland/shell/cmd_file.c` (実物を `#include`)
- 2026-09-16: 贋 FS を [`fs_kind_fake.h`](fs_kind_fake.h) へ切り出して
  [`fs_kind_callers_tdd.md`](fs_kind_callers_tdd.md) の試験と共有した。検査の内容は変えていない
  (切り出しの前後とも 21 checks, 0 failures)

## 0. 正直に書く ([V4])

**この記録は試験と実装の後に書いた (2026-09-15)。** 試験そのものは B5 を直した
往復 3 の中で書かれていて、そのときの RED→GREEN は
[`h1_tdd.md`](h1_tdd.md) の往復 3 の節に入っている。この文書はそれを
`docs/TESTS.md` から名前で引けるようにするために起こしたもので、**新しく
RED を踏み直してはいない**。根拠がこれまで `build/sdk.mk` のコメント 1 行しか
無かったので、何を検査しているかをここに落とす。

自動の変異 (`--mutate`) は**無い**。代わりに §3 に「壊すとどの検査が落ちるか」を
書く — これは試験を読んで書いたものであり、実際に壊して走らせた記録ではない。

## 1. 何を確かめる試験か

`tools/tests/fs_kind_host.c` が実物の `cmd_fs_shared.c` と `cmd_file.c` を
1 行も写さず `#include` する (模型ではない)。差し替えるのは KernelAPI と
`shell_print_help` / `shell_register_cmds` の 2 本だけ。

贋ファイルシステムの肝は **`sys_stat` は正しく答えたまま、`sys_ls` だけを
失敗させられる**こと:

- `OS32_ERR_FULL` — 1000 件を超えるディレクトリ (列挙の上限で打ち切られた)
- `OS32_ERR_IO` — 途中で切れた列挙

B5 の退行は「ディレクトリかどうか」を**列挙が成功したかどうか**で代用して
いたことだった。上の 2 つはどちらも「ディレクトリだが列挙は失敗する」なので、
代用した判定はここで「ディレクトリでない」と答えてしまう。

## 2. GREEN (現状)

```
HOST GNU89 -Werror COMPILE PASS (real cmd_fs_shared.c + cmd_file.c)
21 checks, 0 failures
EXIT fs_kind_host=0
TARGET i386-elf -Werror COMPILE PASS (userland/shell/cmd_fs_shared.c)
TARGET i386-elf -Werror COMPILE PASS (userland/shell/cmd_file.c)
```

| 節 | 見ているもの |
|---|---|
| 種別判定 | `stat` が答えるなら `fs_is_dir` / `fs_path_kind` は DIR / FILE / `NOTFOUND` をそのまま返す |
| 種別判定 (`stat` 非対応) | 列挙が通ればディレクトリ。判定できないときは `fs_path_kind` が `OS32_ERR_NOSYS`、`fs_is_dir` は**従来どおり 0** (真偽値には新しい値を作らない) |
| `cp -r` の宛先階層 | `cp -r /src /big` → `/big/src/a.txt`。**`/big/a.txt` を上書きしない**。列挙が `FULL` でも `IO` でもここは動かない |
| `cp -r` の宛先が無いとき | `/newdir` 直下へ写す (basename を足さない) |
| 単一ファイルの `cp` | 列挙が `FULL` のディレクトリ宛でも `/big/f.txt` へ写し、`/big` をファイルで潰さない |
| 複数入力の `cp` | 列挙が `FULL` でも受け付ける。「ディレクトリでない」と誤って断らない |

`--target` は同じソースが実機と同じ `i386-elf-gcc -Werror` でも通ること
([C1] C89/GNU89) を別に見る。`make`・エミュレータ・実配備には触らない。

## 3. 壊すとどれが落ちるか

| 壊し方 | 落ちる検査 |
|---|---|
| 種別判定を `sys_ls` の戻り値で代用する (B5 の退行そのもの) | 「列挙 FULL でも `/big/src/a.txt` へ入る」「`/big/a.txt` を上書きしない」— 宛先がディレクトリと認識されず、階層が 1 段潰れる |
| `cp -r` で宛先の basename を足し忘れる | 「`/big/a.txt` を上書きしない」が失敗 (**既存ファイルを壊す**ので、この表で最も重い) |
| 宛先が無いときにも basename を足す | 「宛先が無いときは `/newdir` 直下へ」が失敗 |
| `fs_is_dir` が判定不能で 1 を返す / 負値を返す | 「判定できないときの真偽は 0 (従来の形)」が失敗 |
| `fs_path_kind` が `NOSYS` を `NOTFOUND` に畳む | 「`stat` 非対応なら `OS32_ERR_NOSYS`」が失敗 |
| 複数入力の宛先検査を列挙の成否で行う | 「『ディレクトリでない』と誤って断らない」が失敗 |

## 4. この試験が言わないこと

- **実機の `cp` の振る舞いは見ていない。** 贋 FS はオンメモリで、実物の VFS も
  ext2 も通らない。VFS 側の種別判定は [`vfs_kind_tdd.md`](vfs_kind_tdd.md) が別に見る。
- `sys_ls` のコールバックから FS を触る危険 (POLICY_DEBUG §4-26) は対象外。
- 日本語ファイル名の桁幅・切り詰め (POLICY_DEBUG §4-27) は対象外。
