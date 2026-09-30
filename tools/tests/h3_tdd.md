# 票 H3 — mtime の取得と保存、日時の前置判定 (ホスト TDD の記録)

- 票: [`docs/archive/shell/TASK_H3.md`](../../docs/archive/shell/TASK_H3.md) (とくに §8 = ユーザー決裁 2026-09-15)
- 設計: [`HSYNC_IMPROVEMENT_PLAN.md`](../../docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md) §5 / §7.2 / §9
- 基点: `feat/gui` の `d734f54`
- 実行: `python3 -B tools/tests/test_hsync_h3.py --target --mutate`
  (`make check-hsync-h3-host` が同じものを回す)
- 日付: 2026-09-15

## 0. 正直に書く ([V4])

**試験は実装のあとに書いた。** `hsync.c` / `fs/hostdrv_stat_rules.inc` /
`fs/vfs.c` / `fs/ext2_vfs.c` / `kapi/kapi_sys.c` を先に直し、そのあとで
`tools/tests/hsync_h3_host.c` と `tools/tests/vfs_set_mtime_host.c` を書いた。
だから「先に RED を見た」とは書けない。

代わりに **変異試験 (`--mutate`) で赤を取った**。規則を 1 つずつ壊した版に
差し替えて試験を回し、**7 件すべてが落ちる**ことを確かめてある (§4)。
落ちなければ「試験が規則を見ていない」ので、そこは正直に数えて報告する。

途中で実際に RED を踏んで直したものが 2 件ある (§3)。こちらは書いた直後に
落ちたものなので、そのまま記録する。

## 1. 何を確かめる試験か

| ファイル | 中身 |
|---|---|
| `tools/tests/hsync_h3_host.c` | 実物の `userland/system/hsync.c` を 1 行も写さず `#include` し、KernelAPI だけを贋物に差し替える。贋 FS は**ノードごとに mtime を持ち**、`sys_set_mtime` の成功 / `NOSYS` / I/O 失敗を注入できる。`sys_read` / `sys_write` / `sys_set_mtime` の**呼び出し回数**を数える |
| ↑ の A16 | `fs/hostdrv_stat_rules.inc` の `hdrv_filetime_to_unix` / `hdrv_stat_mtime` を同じ翻訳単位で直接叩く (純関数) |
| `tools/tests/vfs_set_mtime_host.c` | 実物の `fs/vfs.c` を `#include` し、合成 `VfsOps` で `vfs_set_mtime()` の振り分け (`NOSYS` / `INVAL` / `NOMOUNT` / エラーの素通し) を見る |
| `tools/tests/test_hsync_h3.py` | 2 本のビルドと実行、`--target` のクロスコンパイル確認、`--mutate` の否定側、`[ABI1]`/`[ABI2]` の静的検査 (`sys_set_mtime` が **slot 213 から動いていないか** / その前の並びが変わっていないか / 版が揃うか / 生成ヘッダの `KAPI_FUNC_COUNT` が 一致するか / `build/app.conf` の要求版) |

贋 FS の `sys_write` は本物と同じく**書き込みのたびに mtime を現在時刻
(555555) で上書きする**。だから「データを書き終えてから mtime を設定する」を
守っていないと A04 が落ちる。

## 2. GREEN (現状)

```
KAPI SLOT PASS (sys_set_mtime = slot 213, v52, app.conf hsync>=52)
HOST GNU89 -Werror COMPILE PASS (tools/tests/hsync_h3_host.c)
103 checks, 0 failures
EXIT hsync_h3_host=0
HOST GNU89 -Werror COMPILE PASS (real fs/vfs.c)
17 checks, 0 failures
EXIT vfs_set_mtime_host=0
TARGET i386-elf -Werror COMPILE PASS (userland/system/hsync.c)
TARGET i386-elf -Werror COMPILE PASS (fs/hostdrvfs.c)
TARGET i386-elf -Werror COMPILE PASS (fs/ext2_vfs.c)
TARGET i386-elf -Werror COMPILE PASS (fs/fatfs_vfs.c)
TARGET i386-elf -Werror COMPILE PASS (fs/iso9660.c)
TARGET i386-elf -Werror COMPILE PASS (fs/vfs.c)
TARGET i386-elf -Werror COMPILE PASS (kapi/kapi_sys.c)
```

### A16 の基準値 (独立に出したもの)

FILETIME は 1601-01-01 UTC 起点・100ns 単位。1601→1970 の差は
369 年 = 134774 日 = **11644473600 秒 = 116444736000000000 (100ns)**。

```python
import datetime
def ft(s):
    dt = datetime.datetime.fromisoformat(s).replace(tzinfo=datetime.timezone.utc)
    base = datetime.datetime(1601, 1, 1, tzinfo=datetime.timezone.utc)
    return int((dt - base).total_seconds()) * 10000000
ft('2026-09-15 01:02:03')  # 0x01DD44ADD1BF7780 -> unix 1789434123
ft('2000-01-01 00:00:00')  # 0x01BF53EB256D4000 -> unix  946684800
ft('1970-01-01 00:00:00')  # 0x019DB1DED53E8000 -> unix          0 (= 起点差)
```

押さえた境界:

| 入力 | 期待 |
|---|---|
| 既知日時 2 つ | 秒が一致する。**JST の 9 時間を足していない** |
| `+0.9999999 秒` | 秒は動かない (切り捨て) |
| `+1.0000000 秒` | 秒が 1 進む |
| `0` (未提供) | 判定できない、`*out = 0` |
| 起点差 − 1 (100ns 前) | 判定できない (**wrap しない**) |
| `1` (1601 直後) | 判定できない |
| ちょうど起点 (1970-01-01) | 変換はできるが値は `0` = 現行 ABI では不明と区別が付かない |
| `u32` 上限ちょうど | `0xFFFFFFFF` |
| `u32` 上限 + 秒未満 | `0xFFFFFFFF` のまま |
| `u32` を 1 秒超える | 判定できない (**wrap しない**) |
| `0xFFFFFFFFFFFFFFFF` | 判定できない (**wrap しない**) |
| `out == NULL` | `0` |
| `basic_rc != 0` | `hdrv_stat_mtime` が `0` (取れなかった値を信用しない) |

H1 で入れた `hdrv_stat_fill` の規則 (Basic / Standard の失敗は `IO`、
`EndOfFile > 0xFFFFFFFF` は拒否) が**変わっていない**ことも同じ節で見ている。

## 3. 途中で踏んだ RED (実際に落ちたもの)

### R1. `hdrv_stat_mtime` / `hdrv_filetime_to_unix` が未使用で `-Werror`

H1 のホスト試験 (`hsync_h1_host.c`) は同じ `.inc` を取り込むが時刻の規則を
呼ばないので、`-Wunused-function` で**ビルドが落ちた**。

```
fs/hostdrv_stat_rules.inc:123:12: error: 'hdrv_stat_mtime' defined but not used
```

直し方: `.inc` は「純規則の置き場」で取り込む側が全部を使うとは限らないので、
`HDRV_STAT_MAYBE_UNUSED` (`__attribute__((unused))`、GNU89 なので使える [C1])
を付けた。

### R2. コピー元の mtime が不明なとき、省略を表示していなかった

内容が同じで `src.mtime == 0` の場合、最初の実装は `meta_only` を
`(ss.st_mtime != 0 && ss.st_mtime != ds.st_mtime)` にしていたので
`apply_mtime()` に入らず、**「時刻の保存を省略した」と誰も言わなかった**。

```
  FAIL 省略したことを表示する
  FAIL 固定の理由コードを出す
```

直し方: 条件を「宛先の mtime がコピー元と**一致していると言えない**」
= `!(ss.st_mtime != 0 && ss.st_mtime == ds.st_mtime)` にした。
0 を書きに行かないのは `apply_mtime()` 側の仕事で、そこが数えて表示する。
**省略を黙らせない**が票の要求そのものなので、試験が正しかった。

### R3. `VfsOps` にフィールドを足したら既存ドライバが `-Werror` で落ちた

```
fs/hostdrvfs.c:890:1: error: missing initializer for field 'set_mtime' of 'VfsOps'
```

`-Wextra` の `-Wmissing-field-initializers`。C89 の規則では書かなかった
メンバはゼロになるので**動作は正しい**が、「書き忘れ」と区別が付かない。
`fs/hostdrvfs.c` / `fs/fatfs_vfs.c` / `fs/iso9660.c` に**明示的な `0`** と
理由のコメントを置いた。ゼロになること自体は
`tools/tests/vfs_set_mtime_host.c` が位置指定の初期化子で固定している。

## 4. 変異試験 (`--mutate`) — 7 件すべて RED

規則を 1 つずつ壊した版に差し替えて `hsync_h3_host` を回し、**落ちること**を
確かめた。落ちなければ試験がその規則を見ていない。

| 変異 | 壊した規則 | 結果 |
|---|---|---|
| `mtime_unknown_is_same` | `mtime_known` を常に真に = **日時が不明 (0) でも「同じ」として省略する** | RED |
| `copy_on_mtime_diff` | 内容が同じでも日時が違えば本体をコピーする (コピーの可否を日時で決める) | RED |
| `verify_ignored` | `--verify` を無視して日時でスキップする | RED |
| `swallow_set_failure` | 時刻の保存失敗を握り潰して成功と言う (`metadata_failed` を出さない) | RED |
| `no_set_mtime` | `sys_set_mtime` を呼ばず成功を返す (保存しない) | RED |
| `filetime_no_epoch_shift` | FILETIME の起点差 (1601→1970) を引かない | RED |
| `filetime_wrap` | `os_time_t` の範囲検査を外す (切り詰めて wrap する) | RED |

```
MUTATE mtime_unknown_is_same      RED (期待どおり落ちた)
MUTATE copy_on_mtime_diff         RED (期待どおり落ちた)
MUTATE verify_ignored             RED (期待どおり落ちた)
MUTATE swallow_set_failure        RED (期待どおり落ちた)
MUTATE no_set_mtime               RED (期待どおり落ちた)
MUTATE filetime_no_epoch_shift    RED (期待どおり落ちた)
MUTATE filetime_wrap              RED (期待どおり落ちた)
```

**1 件目がこの票の中心。**「証拠が無いことを同一の根拠にしない」を壊すと
確かに赤くなる、というのが票 §8 に対する一番大事な保証。

変異は `git` を使わずにソースを書き換えて `finally` で必ず戻す。
実行後に `git status` が汚れていないことを確認済み。

## 5. 票 §6 の受入表との対応

| ID | 反例・操作 | どこで見ているか |
|---|---|---|
| A03 | 内容同一で mtime だけ相違 | `case_a03` — `MTIME ... reason=mtime_only` / `metadata_updated=1` / `copied=0` / **`write` 0 回** / 渡した mtime が元の値 / 2 回目は `read` 0 回 |
| A04 | 元の mtime が古いが内容が違う | `case_a04` — `content_changed` でコピーし、**古い方の mtime** を保存。書き込みが入れた現在時刻 (555555) を上書きしていることまで見る |
| A16 | FILETIME の既知日時・秒未満・1970 前・未提供・範囲外 | `case_a16` (上の表) |
| 追加 | 非対応 FS への設定 | `case_nosys_and_failure` — `NOSYS` は**エラーにしない**。内容同期は継続、`errors=0`、省略を表示 |
| 追加 | 設定失敗 | 同上 — コピー経路と mtime だけ経路の**両方**で `metadata_failed` + 非ゼロ終了 |
| 追加 | `mtime = 0` の元ファイル | `case_unknown_mtime` — 両側 0 / src だけ 0 / dst だけ 0 の 3 通りで**必ず内容を比較する** |
| §8 | サイズも日時も同じ | `case_skip_by_mtime` — **`sys_read` を 1 度も呼ばない** |
| §8 | 失うもの (サイズ・日時が同じで中身が違う) | `case_skip_by_mtime` — 既定では見逃すことを**そのまま試験にした**。仕様が黙って変わったら気付ける |
| §8 | `--verify` | 同上 — 日時が同じでも内容を比較して捕まえる |
| §8 | dry-run は mtime も書かない | `case_dry_run` — `sys_set_mtime` 0 回、宛先の mtime が動かない |
| §7.2 | 集計 6 区分 | `case_tally` — 並び順まで見る |

## 6. H1 の既存試験 (全部そのまま通っている)

| 試験 | 件数 | 結果 |
|---|---|---|
| `test_hsync_h1` (通常) | 219 checks | 0 failures |
| `test_hsync_h1` (CRC 贋物 / A08) | 3 checks | 0 failures |
| `test_hsync_protect` | 134 checks | 0 failures |
| `test_hostdrv_list` | 23 checks | 0 failures |
| `test_fs_kind` | 21 checks | 0 failures |
| `test_vfs_kind` | 35 checks | 0 failures |

### H1 のハーネスに 1 か所だけ手を入れた

`tools/tests/hsync_h1_host.c` の贋 `sys_stat` は **両側とも `st_mtime = 1000`
を決め打ち**で返していた。H3 の判定では「サイズも日時も同じ」= 読まずに
省略なので、そのままだと A01 / A02 が「読み比べない」側に落ちる。

この贋 FS は**そもそも時刻を持っていない**ので、返すべき値は
**0 = 不明**である (1000 は H1 当時の「mtime を根拠にしないことを示すための
決め打ち」だった)。`FNode` に `mtime` を足し、既定 0 のまま返すようにした。

結果として A02 (「同 mtime でも内容差を検出する」) は H3 の下でも意味を保つ:
**0 同士でも必ず内容を比較する**からで、むしろ票 §8 の中心規則を
H1 側からも押さえる形になった。時刻を使う試験は H3 側が持つ。

`hsync.c` の出力書式も H1 の表明を壊さないように保った:

- `SAME <path> size=N` の並びは変えず、理由は**後置**にした
  (`SAME /bin/a.bin size=4096 reason=content_same`)
- `(dry-run: copied は予定件数)` の文字列はそのまま残し、
  `metadata_updated` の但し書きは**別行**にした

## 7. 実機では確かめていない ([V4])

ホスト試験だけ。以下は**未実施**で、PM / テスターの領分:

- `make clean` → `make all` → `make external` → `make check` (コーダーは回さない)
- NHD / HostDrv への配備、NP21/W 上での実行
- **実機での `hsync sys` の所要時間** (票 §8 の見込みは 26 秒 → 1 秒程度、
  全体同期 135 秒 → 数秒程度。**測っていない**)
- HostDrv が実際に `LastWriteTime` を返すかどうか。NP21/W の
  `FileBasicInformation` 応答が 0 を返す実装なら、hsync は全件を内容比較する
  (遅いが**正しい**) 側に落ちる。**速くなることの確認は実機が要る**
- ext2 の `set_mtime` が媒体まで届いているか (再起動後の `ls -l`)
- 初回の同期は宛先の mtime が全件ずれているので、**1 回だけ全件を内容比較
  する** (その後 `metadata_updated` で揃う)。2 回目から速くなる

## 8. 触ったファイル

実装:

- `fs/hostdrv_stat_rules.inc` — FILETIME → Unix 秒の純関数 (`hdrv_filetime_to_unix` / `hdrv_stat_mtime`)
- `fs/hostdrvfs.c` — `hdrv_stat()` が `LastWriteTime` を値として退避し `st_mtime` に返す
- `fs/vfs.h` / `fs/vfs.c` — `VfsOps.set_mtime` (任意実装) と `vfs_set_mtime()`、`VFS_ERR_NOSYS`
- `fs/ext2_vfs.c` — `ext2_vfs_set_mtime()` (ext2 のみ実装)
- `fs/fatfs_vfs.c` / `fs/iso9660.c` / `fs/hostdrvfs.c` — `VfsOps` に明示的な `0`
- `kapi/kapi_sys.c` — `kapi_sys_set_mtime()` (CPL=3 のポインタ検証と写し)
- `sdk/kapi.json` (v51 → v52、`api` 末尾に `sys_set_mtime`) と生成物
- `sdk/include/os32/os32_kapi_shared.h` — `KAPI_VERSION` 52
- `userland/system/hsync.c` — 日時の前置判定、`--verify`、`metadata_updated`
- `build/app.conf` — `userland/system/hsync 52 0`
- `docs/KAPI_SPEC.md` / `docs/INDEX.md` / `README.md` — 版と関数表
- `docs/manpages/hsync.1` — 判定順の表、新オプション、日時方式で見逃すもの

試験:

- `tools/tests/hsync_h3_host.c` (新規)
- `tools/tests/vfs_set_mtime_host.c` (新規)
- `tools/tests/test_hsync_h3.py` (新規)
- `tools/tests/hsync_h1_host.c` (贋 stat の mtime を 0 = 不明に)
- `build/sdk.mk` — `check-hsync-h3-host`
- `tools/tests/h3_tdd.md` (これ)

---

## 9. 着地後に `make check` が落ちた 2 件 (2026-09-15、追記)

PM が `make clean` → `make all` → `make external` → `make check` を回したところ、
**`make all` / `make external` / `check-kapi-version` は通ったが `make check` が
落ちた**。私 (コーダー) が **H1/H3 関連の試験しか回していなかった**のが原因で、
`tools/tests/test_*.py` を全部回していれば着地前に見つかっていた。以後は全部回す。

### (1) `test_vfs_mount_dev` — リンクエラー

```
vfs_mount_dev_host.c:(.text+0x31e7): undefined reference to `ext2_current_time'
vfs_mount_dev_host.c:(.text+0x320e): undefined reference to `ext2_write_inode'
```

`tools/tests/vfs_mount_dev_host.c` は実物の `fs/ext2_vfs.c` を取り込んで
ext2 本体を贋物で埋めているが、H3 で足した `ext2_vfs_set_mtime()` が
`ext2_read_inode` → **`ext2_write_inode`** → `ext2_sync` と進み、時刻を
**`ext2_current_time()`** から取るので、埋まっていない 2 本が未定義になった。

**直し方**: ハーネスに贋物を 2 本足しただけ。本体の実装は変えていない。
この試験の対象は**デバイス番号のエンコード**なので、他の境界と同じく
成功を返すだけにした (`ext2_current_time` は本体と同じ定数 `0x67E8E800`)。
`set_mtime` そのものの規則は `tools/tests/vfs_set_mtime_host.c` が見る。

→ `SUMMARY 6/6 PASS`

### (2) `test_kapi_db_v50` — 22/23。`db_v50_selftest:426`

`kapi/kapi_db.c` の

```c
if (KAPI_SLOT_COUNT != KAPI_SLOT_HOST_CLOSE + 1) bad |= 1u << 0;
```

`KAPI_SLOT_HOST_CLOSE` は 212 なので表の要素数が **213 ちょうど**であることを
要求する。`sys_set_mtime` (slot 213) を足して 214 になったので落ちた。

**これは決め打ちが古いだけでなく、設計と矛盾している。** [ABI2] は末尾への
追記を正当な操作と定めているのに、この行は「`host_close` の後ろに何も足されて
いないこと」を要求するので、**KAPI を 1 本足すたびに必ず落ちる**。
`kernel/kselftest.c` の `test_db_v50` の註が言う意図は
「追記した 7 本が 201..207 に居て、既存の `db_*` が動いていない」であって、
表がそこで終わっていることではない。

**直し方**: 判定を純関数 `db_slot_layout_ok(int slot_count)` に切り出し
(`kapi/kapi_db.c` / `kapi/kapi_db.h`)、

- 長さは **下限だけ** 見る (`slot_count <= KAPI_SLOT_HOST_CLOSE` なら偽)。
  末尾に何本足されても真。
- **意図は変えていない** — 既存スロットが動いていないことの検査
  (`db_open_existing == 201` / `db_error_code == 207` / 差が 6 /
  `db_open == 140`) はそのまま残した。
- 以前は上の「件数 == `host_close` + 1」が v51 の host 帯を**間接的に**
  押さえていただけだったので、**位置を明示して**足した
  (`host_open == 208` / `host_close == 212` / 差が 4)。
- 数値の直書きはしない ([C4])。位置は全部 `os32_kapi_slots.h` の定数から導く。

`slot_count` を引数で受けるのは、**「KAPI をもう 1 本足したら」を試験から
直に試せるようにするため**。

#### 同じ罠を次の追加で踏まないための試験

`tools/tests/kapi_db_v50_host.c` に **`slot_layout_append`** を新設し、
`tools/tests/test_kapi_db_v50.py` の `CASES` に登録した (23 → **24 cases**)。

```c
CHECK(db_slot_layout_ok(KAPI_SLOT_COUNT));         /* いまの表 */
CHECK(db_slot_layout_ok(KAPI_SLOT_COUNT + 1));     /* もう 1 本足した未来 */
CHECK(db_slot_layout_ok(KAPI_SLOT_COUNT + 10));
CHECK(db_slot_layout_ok(KAPI_SLOT_COUNT + 100));
CHECK(db_slot_layout_ok(KAPI_SLOT_HOST_CLOSE + 1));
CHECK(!db_slot_layout_ok(KAPI_SLOT_HOST_CLOSE));   /* 下限は見る */
CHECK(!db_slot_layout_ok(0));
CHECK(!db_slot_layout_ok(-1));
CHECK(KAPI_SLOT_COUNT > KAPI_SLOT_HOST_CLOSE);
```

**変異で確かめた** — 判定を「いまの件数ちょうどしか許さない」形
(`slot_count != KAPI_SLOT_COUNT`) に差し替えると、

```
EXIT v50_selftest=0                                  <- 既存の項は素通り
EXIT slot_layout_append=1
FAIL slot_layout_append:448: db_slot_layout_ok(KAPI_SLOT_COUNT + 1)
```

つまり **既存の `v50_selftest` では捕まらない「次の追記で壊れる形」を、
新しい項だけが捕まえる**。元の決め打ち (`!= KAPI_SLOT_HOST_CLOSE + 1`) に
戻すと両方 RED になることも確認した (`SUMMARY 0/2 PASS`)。

### 同じ形の決め打ちの調査

`KAPI_SLOT_COUNT` / `KAPI_FUNC_COUNT` を等号で比べている箇所を全探索した
(`--include=*.c --include=*.h --include=*.inc --include=*.py --include=*.rs
--include=*.asm`、生成物と `sdk/gen_kapi.py` を除く)。

**該当は `kapi/kapi_db.c:1151` の 1 か所だけ**だった。他の利用は全部
「表の長さから導く」形で、末尾追記に対して正しい:

| 箇所 | 使い方 | 追記で壊れるか |
|---|---|---|
| `exec/exec.c:102` | `for (i = 0; i < KAPI_FUNC_COUNT; i++)` (トランポリンのスタブ生成) | 壊れない |
| `exec/exec.c:120,801,1698` | `tbl[2 + KAPI_FUNC_COUNT + 0/1]` (データフィールドの位置) | 壊れない |
| `exec/exec.c:314` | スタブ帯の大きさ `KAPI_FUNC_COUNT * 8` | 壊れない |
| `exec/exec.c:1063` | `if (slot >= (u32)KAPI_FUNC_COUNT)` (ディスパッチャの範囲検査) | 壊れない |
| `exec/ring3_str.h:40` | `RING3_USTR_OFF` の式 | 壊れない |
| `kapi/kapi_generated.c` (生成物) | `kapi_argsize[KAPI_FUNC_COUNT]` / `kapi_argptr[...]` | 壊れない |
| `tools/tests/ring3_str_host.c:45,129` | `KAPI_FUNC_COUNT * 8` の式 | 壊れない |
| `tools/tests/test_hsync_h3.py:87` | 生成ヘッダと `kapi.json` の件数一致 (再生成忘れの検出) | **そういう検査なので正しい** |

`KAPI_SLOT_<NAME>` を固定値と比べる行 (`kapi/kapi_db.c:1152-1155` ほか) は
**既存スロットが動いていないこと**の検査で、末尾追記では変化しない。残した。

#### 追記 (2026-09-16) — この調査は**同じ誤りの別の綴り**を取りこぼしていた

上の全探索は `KAPI_SLOT_COUNT` / `KAPI_FUNC_COUNT` の**等号**だけを見ており、
**「特定の関数が `api[-1]` であること」** という同じ意味の決め打ちを拾えていなかった。
`kbd_peekkey` (v54、継承バグ「`source` が ESC 以外も食う」) を末尾に追記したときに
`check-hsync-h3-host` が落ちて見つかった。該当は 2 か所で、どちらも直した:

| 箇所 | 元の主張 | 直した後 |
|---|---|---|
| `tools/tests/test_hsync_h3.py` | `api[-1]["name"] != "sys_set_mtime"` → 「末尾であること」 | `sys_set_mtime` が**存在**し、slot が **213 から動いていない**こと + slot 0..213 の名前列の SHA-256 が変わらないこと + 表の長さは**下限だけ** |
| `tools/tests/test_vfs_excl.py` | 同上 (「H2 でスロットを足していない」を末尾で代用) | `sys_set_mtime` が slot 213 のままであること (後ろに v54 以降が何本あってもよい) |

digest は「slot 213 までの名前」なので**末尾追記では動かない** — KAPI を足すたびに
更新する必要は無い。更新が要るなら、それ自体が [ABI2] 違反の印。

次に同じ探索をするときは、等号だけでなく **`[-1]` / `[len-1]` / 「最後の要素」**
の形も併せて見ること。

### 全試験の結果 (`tools/tests/test_*.py` を全部実行)

**53 本すべて PASS。**

| 試験 | 件数 / 結果 |
|---|---|
| `test_app_band_pde` | HOST ILP32 + TARGET GNU89 PASS |
| `test_boot_splash_native` | 2 |
| `test_cfg` | 53/53 |
| `test_con_sink` | EXIT 0 |
| `test_deploy_protect` | 164 |
| `test_device_reservation` | 8 |
| `test_emu_playbook` | **41 tests OK** (`PYTHONPATH=. ` が要る。`make check` は `PYTHONPATH=.` 付きで呼ぶので通る。単体実行だけ `ModuleNotFoundError: No module named 'tools'` — **既存の呼び出し方の問題で H3 とは無関係**。触っていない) |
| `test_ext2_read_bound` | 4/4 |
| `test_ext2_write_io` | 10/10 |
| `test_fatfs_stat` | 11/11 |
| `test_filer_copy_abort` | 4 |
| `test_filer_normalize` | 3 |
| `test_fs_kind` | 21 checks, 0 failures |
| `test_gui_button_dispatch` | SUMMARY PASS |
| `test_highram_stage` | 9 |
| `test_host_agent` | 81/81 |
| `test_host_lib` | 5/5 |
| `test_hostdrv_list` | 23 checks, 0 failures |
| `test_hsync_h1` | 219 + 3 checks, 0 failures |
| `test_hsync_h3` | 103 + 17 checks, 0 failures |
| `test_hsync_protect` | 134 checks, EXIT 0 |
| `test_install_fresh` | 12/12 |
| `test_install_recover` | 14/14 |
| `test_kapi_db_owned` | 1 |
| **`test_kapi_db_v50`** | **24/24** (23 → 24、`slot_layout_append` を追加) |
| `test_kbd_inject` | EXIT 0 |
| `test_launch` | EXIT 0 |
| `test_memory_boot` | 9 |
| `test_mk_blank_nhd` | 13 |
| `test_mk_settings_db` | 45 |
| `test_multiapp_impl` | TARGET COMPILE PASS |
| `test_multiapp_model` | TARGET COMPILE PASS |
| `test_net_link` | 35/35 |
| `test_nhd_deploy_failure` | 3 |
| `test_np21w_ini` | 49 |
| `test_np21w_ini_live` | 47 |
| `test_np21w_transport` | 8 |
| `test_np21w_trial` | 31 |
| `test_owner_reclaim` | 1 |
| `test_paging_bounds` | HOST ILP32 + TARGET GNU89 PASS |
| `test_pgalloc_model` | 12 |
| `test_pgalloc_range` | TARGET COMPILE PASS |
| `test_physmem` | 9 |
| `test_ring3_str` | EXIT 0 |
| `test_sbrk_tier` | TARGET COMPILE PASS |
| `test_sh_launch` | EXIT 0 |
| `test_sh_shell` | EXIT 0 |
| `test_sqlite_groups` | 32/32 |
| `test_stat_cmd` | 5/5 |
| `test_tar_cmd` | 8/8 |
| `test_vfs_fd_sqlite` | 15/15 |
| `test_vfs_kind` | 35 checks, 0 failures |
| **`test_vfs_mount_dev`** | **6/6** (贋物 2 本を足して復旧) |

`make check` の非 `test_*.py` 項目のうち、ビルド成果物が要らないものも回した:

- `tools/check_kapi_version.py` … v52 一致 (4 箇所) / 関数表一致
- `tools/check_constraints.py` … OK (規則 16 件)
- `tools/check_gui_proto.py` … 一致 (定数 105 / 構造体 31)
- `userland/gshell/host/integration.py` … 100 passed

**未実施** ([V4]): `tools/check_privileged.py` と `tools/check_manifests.py` は
`make programs` / `make all` の成果物を読むので回せない (コーダーは make を
実行しない)。

### 追加で触ったファイル (この節の分)

- `tools/tests/vfs_mount_dev_host.c` — `ext2_write_inode` / `ext2_current_time` の贋物
- `kapi/kapi_db.c` — `db_slot_layout_ok()` の切り出しと下限判定化、host 帯の位置を明示
- `kapi/kapi_db.h` — `db_slot_layout_ok()` の宣言
- `tools/tests/kapi_db_v50_host.c` — `slot_layout_append` ケース
- `tools/tests/test_kapi_db_v50.py` — `CASES` に `slot_layout_append`
