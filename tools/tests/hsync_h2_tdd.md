# hsync H2 TDD 記録 — 一時ファイル + 検証 + 置換

対象票: [`docs/archive/shell/TASK_H2.md`](../../docs/archive/shell/TASK_H2.md) §2-3 / §2-4 / §4-1
試験: `tools/tests/hsync_h2_host.c` (実物の `userland/system/hsync.c` を `#include`)
実行: `python3 -B tools/tests/test_hsync_h2.py [--target] [--mutate]` / `make check-hsync-h2-host`

基点: `feat/gui` = `15c5edf`。

## 何を見ているか

H1 / H3 と同じく **`userland/system/hsync.c` を 1 行も写さずそのまま `#include`**
し、`KernelAPI` だけをオンメモリの贋ファイルシステムへ差し替える。贋 FS は票 H2 が
要求する故障を注入できる:

- `sys_open` の **`O_EXCL`** (既存は `EXIST` / 非対応 FS は `NOSYS`)
- n 回目の `sys_write` を任意の番号で落とす (空き不足 = `NOSPC` / `FULL` も)
- `sys_set_mtime` の失敗と、それに伴う **`ROFS` への転落** (ext2 と同じ)
- `sys_rename` の模様: **公開の前**に失敗 / **公開の後**に失敗 (一時名が消えた形と
  残った形) / 公開したか判定できない / 宛先が第三の inode になった / 成功
- `st_nlink` をノードごとに持つ (hardlink 判定と予約名の掃除)
- 指定パスの `sys_ls` を n 回だけ落とす (掃除の列挙だけを止め、手順 2 の
  `EXIST` 経路を必ず通す)
- `api->version` を 52 / 53 に切り替える (古いカーネルの門)

受入表との対応 (§4-1):

| ID | 試験の節 |
|---|---|
| A14a | `case_a14a` (write 失敗 / FULL / NOSPC / 読戻し / 公開前の rename) |
| A14a2 | `case_a14a2` (`RN_FAIL_AFTER`) |
| A14a3 | `case_a14a3` (`set_mtime` 失敗 → ROFS → `STALE` → 次の実行が片づける) |
| A14a4 | `case_a14a2` (`RN_UNKNOWN` / `RN_THIRD_INO`) |
| A14a5 | ext2 側は `tools/tests/b8_open_host.c` の段 C 掃引。hsync 側は A15 で収束を見る |
| A14a6 | `case_a14a2` 末尾 (`-f` で新旧同内容 + 公開前の失敗) |
| A14b / A14b2 / A14c | `case_a14b` |
| A15 / A15b / A15c | `case_a15` |
| A17a / A17b / A17c | `case_a17` |
| R1 / R1b | `case_r1` |
| R3 | `case_r3` |
| R4 | `case_r4` |
| R5 | `case_r5` |
| §2-3 手順 2 の `replace_unsupported` | `case_unsupported` |
| R2 (回帰) | `case_regression` + `test_hsync_h1.py` / `test_hsync_h3.py` / `test_hsync_protect.py` |

## RED (実装前 = `15c5edf` の `userland/system/hsync.c`)

KAPI と VFS / ext2 は新しいまま、**hsync だけを `15c5edf` に戻して**回した結果。

```
150 checks, 80 failures
```

主な内訳 (抜粋):

```
== A14a: 公開の前の失敗 -> 旧宛先の内容・サイズ・mtime が不変 ==
  FAIL write 失敗: 旧宛先の内容・サイズ・mtime・ino が不変
  FAIL 公開の前と明示する
  FAIL reason=no_space
  FAIL 空き不足: 旧宛先のサイズ不変
  FAIL 読戻し失敗: 旧宛先が不変
  FAIL 公開前の rename 失敗で非ゼロ終了
  FAIL reason=replace_failed
== A14a2: 公開の後の失敗 -> 新内容を認め replace_partial ==
  FAIL reason=replace_partial
  FAIL **成功には数えない**
== A14a4: 公開したか判定できない -> ino 照合で決まる ==
  FAIL 宛先の stat が落ちたら replace_unknown
== A14a3: 一時ファイルへの set_mtime 失敗 -> ROFS -> STALE ==
  FAIL **copied は 0** (公開の前の失敗)
  FAIL 宛先は旧内容のまま
  FAIL STALE と表示する
== A14b2: 利用者が .hs~notes を置いている -> 消える (予約名) ==
  FAIL **利用者のファイルでも消える**
  FAIL 集計 cleaned=1
== A17a/b/c ==
  FAIL reason=source_changed / dest_changed / hardlink
== R1: KAPI v52 で --unsafe-overwrite 無し -> kernel_too_old ==
  FAIL reason=kernel_too_old
  FAIL **1 件も書かない**
== R3 / R4 / R5 / replace_unsupported ==
  FAIL 本名は不存在のまま / name_too_long / reason=protected / replace_unsupported
```

**旧コードが何をしていたか**: `copy_verify` が宛先を `O_WRONLY|O_CREAT|O_TRUNC` で
**直接**開いていた。書き込み・検証・mtime のどこで落ちても旧内容は既に切り詰められて
いて戻らない (失敗行に「直接上書きなので旧内容は残らない」と出していた)。
一時ファイル・予約名・置換・古いカーネルの門は存在しない。

## GREEN (実装後)

```
MANIFEST PASS (app.conf hsync=52, HS_MIN_KAPI_H2=53, 予約名 .hs~ は man ページに明記)
HOST GNU89 -Werror COMPILE PASS (tools/tests/hsync_h2_host.c)
=== 票 H2: hsync の置換安全化 (一時ファイル + 検証 + 置換) ===
  (150 件すべて ok)

150 checks, 0 failures
EXIT hsync_h2_host=0
TARGET i386-elf -Werror COMPILE PASS (userland/system/hsync.c)
```

## 追補 — 独立レビューの非 blocker 指摘 1 / 2 / 4 (2026-09-16、基点 `d99d9e1`)

実装 (`528c5cb`) は独立レビューで Approve (blocker 0)。その非 blocker 指摘のうち
3 件を反例つきで直した。試験は `case_review_nb`、対象は同じ `hsync_h2_host.c`。

| 指摘 | 反例 |
|---|---|
| 1 | 手順 8 の `rename` が通った後に手順 9 の `vfs_sync` が落ちると、呼び手が `note_target` を呼ばず「/sys を更新した -> シェル再起動が必要」が消えていた。**置換は媒体に載っているのに案内が消えるのは誤報**。贋 FS に「n 回目の `vfs_sync` だけ落とす」注入を足し、手順 4 を通して手順 9 だけ落とす |
| 2 | コピー元の `.hs~*` を黙って落としていた。`excluded` にも `-v` の行にも出ないので、ホストの配備元に紛れても気づけない |
| 4 | mtime の失敗 1 件が、続く `drop_temp` の `STALE` でもう 1 件数えられ `errors=2` になっていた。**1 つの失敗は 1 と数える** |

### RED (直す前 = `d99d9e1` の `userland/system/hsync.c`)

試験だけ新しくして hsync を `d99d9e1` に戻した結果。

```
176 checks, 7 failures
```

落ちた 7 件 (既存の 150 件は全部 ok のまま):

```
== 非 blocker 1: rename 成功 + sync 失敗 -> 再起動の案内は出す ==
  FAIL **シェル再起動の案内が出る** (置換済みなので消してはいけない)
  FAIL **再起動の案内が出る**                       ← /boot 側
== 非 blocker 2: コピー元の .hs~ を黙って落とさない ==
  FAIL **excluded が 1 増える**
  FAIL reason=reserved_name
  FAIL -v で**コピー元**のパスを見せる (直すのはそちら)
  FAIL -v 無しでも excluded に数える
== 非 blocker 4: 1 つの失敗を 2 と数えない ==
  FAIL **1 つの失敗は 1 件** (STALE で二重に数えない)
```

### GREEN (直した後)

```
176 checks, 0 failures
EXIT hsync_h2_host=0
TARGET i386-elf -Werror COMPILE PASS (userland/system/hsync.c)
```

指摘 1 は `note_target` だけでは足りなかった。案内の出力そのものが
`g_copied > 0` で門を張っていたので、**`g_touched_sys` / `g_touched_boot` も
条件に入れた** (`note_target` は媒体の上で内容が入れ替わったときだけ立つ)。

指摘 3 (掃除の列挙で「名前が長い」を「掃除の枠を越えた」に畳んでいた) と
指摘 5 (`HS_TEMP_PREFIX_LEN` の重複定義 [C4]) は表示の語と定数の導出だけで、
**振る舞いが変わらないので反例は足していない** (票の指示どおり)。

### 追補 2 — 手順 8 の `replace_partial` へも広げる (PM 決裁 2026-09-16)

最初の実装では `*published` を「手順 8 の `rename` が 0 を返した後」にだけ立てていたので、
`rename` が非ゼロを返しつつ宛先の `st_ino` が一時ファイルのものと一致する回
(`replace_partial`) では案内が出なかった。**媒体の上では入れ替わっているので手順 9 と
同じ理屈が当たる**、という PM 判断で広げた。

- `replace_partial` … `*published = 1` (公開済み。案内を出す)
- `replace_failed` … 0 のまま (宛先は旧内容。入れ替わっていない)
- `replace_unknown` … 0 のまま (公開したか分からない = 案内を出す根拠がない)

反例は `case_review_nb` の「非 blocker 1b」。`RN_FAIL_AFTER` / `RN_FAIL_BEFORE` /
`RN_UNKNOWN` の 3 通りを `/sys` の対象に当てて、**出る回と出ない回の両方**を固定した。

#### RED (広げる前)

```
189 checks, 1 failures
```

```
== 非 blocker 1b: replace_partial でも再起動の案内は出す ==
  ok   reason=replace_partial
  ok   **置換は媒体に載っている** (宛先は検証済みの新しい内容)
  ok   copied には数えない
  ok   errors に数える
  FAIL **シェル再起動の案内が出る** (公開済みなので消してはいけない)
```

`replace_failed` / `replace_unknown` 側の「案内を出さない」6 件は RED でも ok
(元から出ていなかった) — **広げすぎていないこと**を GREEN 側で押さえるための表明。

#### GREEN (広げた後)

```
189 checks, 0 failures
EXIT hsync_h2_host=0
TARGET i386-elf -Werror COMPILE PASS (userland/system/hsync.c)
```

## 変異試験 (`--mutate`、否定側)

```
MUTATE publish_by_size            RED (期待どおり落ちた)
MUTATE fallback_direct_on_nosys   RED (期待どおり落ちた)
MUTATE mtime_failure_ignored      RED (期待どおり落ちた)
MUTATE temp_owned_by_nlink        RED (期待どおり落ちた)
MUTATE clean_owned_by_nlink       RED (期待どおり落ちた)
MUTATE no_kernel_gate             RED (期待どおり落ちた)
MUTATE clean_ignores_protection   RED (期待どおり落ちた)
MUTATE temp_names_synced          RED (期待どおり落ちた)
```

- `publish_by_size` … 公開の判定を **サイズ**に戻した版 (往復 2 所見 3 の否定側)。
  `-f` で新旧が同じ内容だと未公開を「公開された」と誤る。
- `fallback_direct_on_nosys` … `NOSYS` で**直接上書きへ黙って落ちる**版。
- `mtime_failure_ignored` … mtime の失敗を握り潰して置換へ進む版
  (往復 1 所見 4 の否定側)。
- `temp_owned_by_nlink` / `clean_owned_by_nlink` … 予約名の所有を **`st_nlink`**
  で判断する版 (往復 1 所見 2 の否定側)。手順 2 の `EXIST` 経路と §2-4 の掃除の
  両方に当てる。
- `no_kernel_gate` … 古いカーネルの門を外した版 (R1 の否定側)。
- `clean_ignores_protection` … 掃除が保護対象を無視する版 (R5 の否定側)。
- `temp_names_synced` … コピー元の `.hs~` を同期対象にしてしまう版 (R4 の否定側)。
- `temp_names_dropped_silently` … コピー元の `.hs~` を**黙って**落とす版
  (非 blocker 2 の否定側。弾くこと自体は変えず、数えるのと `-v` の行だけを消す)。
- `published_note_dropped` … 手順 9 の `sync_failed` で再起動の案内を消す版
  (非 blocker 1 の否定側)。
- `partial_note_dropped` … 手順 8 の `replace_partial` で `*published` を立てない版
  (PM 決裁 2026-09-16 の否定側。公開済みなのに案内が消える)。
- `stale_counted_twice` … 手順 7 の後始末の `STALE` を独立した 1 件として
  数え直す版 (非 blocker 4 の否定側 = 1 つの失敗を 2 と数える)。

## 設計との違い (PM の確認が要る点)

**`build/app.conf` の `userland/system/hsync` の要求 API 版は 52 のまま**にした。

票 §1 の 7 と §2-3 末尾が「v52 のカーネルの上で新しい hsync を動かす場面は現に
起こる」「既定は `kernel_too_old` で断り、`--unsafe-overwrite` のときだけ直接上書き」
としているが、`exec/exec.c:1276` は `hdr->min_api_ver > KAPI_VERSION` のバイナリを
「invalid OS32X binary」として**起動そのものを拒否する**。53 を宣言すると
`kernel_too_old` も `--unsafe-overwrite` も届かない。

そのため `test_hsync_h3.py` の `check_kapi_slot()` が持っていた
「`app.conf` の hsync の要求版 >= `kapi.json` の version」という検査を、
**「`sys_set_mtime` が入った v52 以上、かつ `kapi.json` の version 以下」**に
緩めた (H3 の挙動試験そのものは 1 件も変えていない)。
`test_hsync_h2.py` 側では逆に「52 ちょうどであること」を固定している。

## 確かめていないこと ([V4])

- **ゲスト (NP21/W) では 1 度も動かしていない。** 実 NHD / HostDrv 上の所要時間
  (票 §4-2 の 3)、大きい shlib の差し替え (同 4)、空き不足 (同 5)、強制終了からの
  復旧 (同 6) はすべて未実施。
- `make all` / `make check` / `make external` / 配備は**実行していない** (禁止範囲)。
  `make clean` → 全体ビルドは PM が着地時に回す ([ABI3])。
- 贋 FS は「名前 = ノード」の模型なので、**ext2 の媒体の状態** (links_count の
  過大計上、漏れ、公開の 3 値判定) は見ていない。そこは
  `tools/tests/b8_open_host.c` の段 C / 段 H の掃引と `e2fsck -fn` が受け持つ。

## 同期先がフロッピー (Codex 2026-09-25 P2)

MINIMAL (= 起動 FD) に hsync を入れたので、FD 起動で引数なしの `hsync` を打つと宛先 `/`
が FD になる。ファイル本体は FAT に O_EXCL が無く `replace_unsupported` で落ちるが、その前の
`sys_mkdir` は通るので FD を空ディレクトリで埋め得た。`main` が掃除・名札・mkdir より前に
`dst_on_floppy` で断る (KAPI は足さない: `vfs_devname` で宛先のパスを親へ遡り、最初のマウント
点のデバイス名が `fd` で始まるか)。

- `case_dst_fd`: FD 起動の全体 / `bin` / `-n` は非ゼロ終了で `dest_on_fd`、mkdir・write・rename
  が 0 回、FD の中身は不変。HDD 起動で `/fd0` にフロッピーを載せた場合、`hsync fd0/x` は断り、
  `hsync bin` は通る
- 変異 `fd_dest_not_refused` (判定を常に 0) と `fd_dest_root_only` (遡らずルートだけ見る) は RED
- 他の hsync ハーネス (h1 / h3 / h4) の贋 KAPI には、ルート hd0 を返す `vfs_devname` を足した

### マウントをまたがない (Codex 2026-09-25 2 回目 P2)

開始点の判定だけでは、/ = hd0・/fd0 = fd0 で同期元に `/host/fd0/x` があると、全体同期が
`/fd0/x` を mkdir して FD に書けた。`sync_directory` の各項目で、宛先の子が別のマウントの根
(`vfs_devname` が名前を返す) なら入らずに `other_mount` で除外する (-v 無しでも 1 行、
`excluded` に数える、失敗にしない)。

- `case_dst_fd` の続き: 上の配置で全体同期は rc=0、`other_mount` の行が出て、/fd0 の下に
  ノードができず /fd0 以下への mkdir は 0 回。hd0 側 (/bin/a.bin の更新、/newdir の作成) は
  通常どおり
- 変異 `cross_mount` (判定を無効にする) は RED

### /sys が別マウントのときも other_mount を出す (Codex 3 回目 P3)

マウント判定を既定の sys 除外より前へ移した。以前は /sys が別マウントだと
default_sys_exclusion (-v でしか出ない) で黙って飛ばされていた (書き込みは防げていた)。
`case_dst_fd` に「/sys = hd1 + 全体同期 → `/sys reason=other_mount` の行、/sys/k.bin 不変」
を足し、変異 `sys_exclusion_hides_mount` (sys 除外を先に見る) が RED。


### `--root <根>` — 同期先の根を替える (2026-09-26、ユーザー提案)

FD の新しいカーネルで起動し、SerialFS 越しの /host から /hd0 (HDD) を更新するための口
(docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md §5)。`case_root` (FD 起動 = ルート fd0、/hd0 = hd0
で根の inode 2) で見るもの:

- `--root /hd0` (全体)・`--root /hd0 bin`: /hd0 の下だけが変わり、FD の /bin/a.bin と / は無変更
  (newdir も etc も作らない)。mtime も元の値
- 保護: /hd0/etc/settings.db (名前規則 + 実体規則)、`-f --root /hd0 etc`、/hd0/etc に無い
  settings.db-wal を作らない (名前規則を根からの相対で見る)、/hd0/etc/settings.db の hardlink
  の別名 /hd0/bin/sdb (実体規則を <根>/etc の走査で集める)
- マウントをまたがない: /hd0/mnt が別マウントなら `EXCLUDE /hd0/mnt reason=other_mount`
- 名札: bin/a.bin と newdir/n.bin を載せた名札で `manifest_extra=0` (根を除いて照合)、名札の写しは
  /hd0/.deploy へ
- 断る門 (rc≠0・mkdir / write / rename 0 回・FD も /hd0 も不変): /fd1 (`dest_on_fd`)、
  /hd0/../fd1、/host と /host/bin (`root_is_source`)、/hd0/bin (`root_not_mount`)、/hd1 = 根の
  inode が 2 でない (`root_not_ext2`)、-n + /cd0 = ISO の LBA 20 (`root_not_ext2`)、相対パス・値
  無し・2 回指定、HDD 起動の /hd0 (マウントされない、`root_not_mount`)、KAPI v52 のカーネル
  (`kernel_too_old` — 走っているカーネルの門は --root でも同じ)
- vmkernel.old: 起動記録と一致すれば /hd0/boot/vmkernel.old に作る (/boot には作らない)・
  `/boot を更新した` の案内。FD の起動記録 (一致しない) では `not_booted_image` + `--root` の案内で
  置き換えない、`--no-backup` で .old 無しに置き換える

変異 11 本 (`root_ext2_unchecked` `root_mount_unchecked` `root_fd_unchecked`
`root_source_accepted` `root_protect_by_full_path` `root_protect_scan_slash_etc`
`root_manifest_full_path` `root_old_on_slash` `root_kernel_by_full_path`
`root_note_by_full_path` `root_subdir_not_prefixed`) がすべて RED。既存 20 本も RED のまま。
`check_manifest` に「HS_EXT2_ROOT_INO が fs/ext2.h の EXT2_ROOT_INO と同じ」と man ページの
`--root` / `root_*` の記載を足した。

着地後の P3 (2026-09-26、代行レビュー): 根の stat が落ちたら `root_stat_failed err=` (I/O 失敗を
`root_not_ext2` と言わない)、`--root` で /sys・/boot を更新したときの案内は「HDD (`<根>`) から起動し直す」、
`FAIL <根>/boot/vmkernel.old` (name_too_long の表示を根付きに)。case_root に `/hd0/` と `//hd0/./`
(正規化して /hd0 と同じ)、`--root /hd0 ../x` の拒否、`--root /hd0 sys` の案内、HDD 起動の `--root /` =
既定と同じ結果、FD 起動の `--root /` = `dest_on_fd`、`-n --root /hd0 boot` + FD の起動記録 = `not_booted_image`
を足した。変異 5 本 (`root_stat_fail_as_ext2` `root_sys_note_shell` `root_boot_note_reset`
`root_not_normalized` `old_gate_skipped_on_dry_run`) が RED、`root_ext2_unchecked` は新しい形に合わせて
直した (全 35 本 RED)。
