# H1 ホスト TDD の記録 (hsync の同サイズ更新の検出)

票 [docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md](../../docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md)
§8 の H1 行 / §9 の A01〜A13。実行:

```
python3 -B tools/tests/test_hsync_h1.py [--target]
make check-hsync-h1-host        # 同じもの (--target 付き)
```

- `tools/tests/hsync_h1_host.c` … 実物の `userland/system/hsync.c` を 1 行も写さず
  `#include` する (`main` を `hsync_main` へ改名するだけ)。差し替えるのは KernelAPI
  だけで、オンメモリの贋ファイルシステムへ向ける。ノードごとに
  **short read の分割幅 / 読み取り途中の I/O エラー / 早期 EOF / 1 回の write 上限 /
  0 進捗 write / 書き込み時のビット反転 (破損) / stat のエラー・種別ゼロ・サイズ偽装**
  を注入できる。`vfs_sync` の失敗は大域スイッチ。`kprintf` は全文をログへ溜め、
  検査は**固定文字列の理由コード**で行う (文言の雰囲気で判定しない)。
- `fs/hostdrv_stat_rules.inc` (HostDrv の stat 失敗の是正) は純関数なので同じ翻訳単位で
  直接叩く。`fs/hostdrvfs.c` の `hdrv_stat()` はこの `.inc` を `#include` して呼ぶだけで、
  判定の写しをどこにも作っていない。
- CRC は `lib/crc32.c` (既存の一括版 `crc32_calc`) を同じ実行ファイルへリンクし、
  ストリーム核 (`lib/crc32_core.inc`) との一致を既知ベクトルと実データで見る。
- ビルドは 2 本。`-DHSYNC_CRC_STUB` を付けると CRC 核を「常に同じ値を返す贋物」へ
  差し替える (A08 の否定側)。

エミュレータ・実配備・`make` には一切触れない。

## 受入項目との対応

| ID | 内容 | 対応する検査 |
|---|---|---|
| A01 | 同サイズで 1 バイト変更 | `case_a01_a02` — `UPDATE /bin/a.bin reason=content_changed size=100` |
| A02 | 同サイズ・同 mtime で内容だけ変更 | `case_a01_a02` — 贋 stat は両側 `st_mtime=1000` を返す。それでも `content_changed` |
| A05 | 空 / 新規 / 大小変化 / バッファ境界 / 複数チャンク | `case_a05` — 0,1,2,32767,32768,32769,65535,65536,65537,131072,131085 の 11 サイズ x (同一 / 末尾 1B 差 / 先頭 1B 差 / 新規 / サイズ差) |
| A06 | 両側で異なる short read 分割・read エラー・早期 EOF・0 進捗 write・短い write | `case_a06` |
| A07 | CRC 既知ベクトル (1B 刻み / 全量 / 不規則チャンク) | `case_a07` — 空列 `00000000`、`123456789` → `CBF43926`、70000B をビット版の基準値と照合。チャンクごとの完成 CRC を XOR したものが**一致しない**ことも見る |
| A08 | CRC を差し替えて同値を返してもバイト比較が不一致を検出 | `case_a08` (通常ビルドと `-DHSYNC_CRC_STUB` の両方で走らせる) |
| A09 | copy 後の破損・`vfs_sync` 失敗 | `case_a09` — どちらも `reason=verify_failed`・`copied=0`・非ゼロ終了 |
| A10 | 強制モードでも settings.db の大小文字名・祖先保護・実体 (hardlink) 保護 | `case_a10` |
| A11 | stat 失敗・64bit サイズ | `case_a11_rules` (純関数) と `case_a11_hsync` (上位が空ファイル扱いしない) |
| A12 | 全体同期 / 明示 sys / `usr/sys` / 正規化 dir / root 外 dir / 自己コピー / 引数 | `case_a12` |
| A13 | dry-run で書込みが起きない | `case_a13` — `sys_write` / `sys_mkdir` / `vfs_sync` / `O_CREAT` の呼び出し回数がすべて 0 |

A03 / A04 (mtime) は票 H3、A14 / A15 (一時ファイルと置換) は票 H2、A17〜A21
(競合・manifest・列挙上限・性能) は H4 以降なのでここでは扱わない。

## RED → GREEN

先に GREEN を貼る。

```
$ python3 -B tools/tests/test_hsync_h1.py --target
CONST COUPLING PASS (VFS_MAX_PATH_DEPTH=32 == HS_MAX_PATH_DEPTH, HSP_MAX_DEPTH=32)
HOST GNU89 -Werror COMPILE PASS (real hsync.c)
...
219 checks, 0 failures
EXIT hsync_h1_host=0
HOST GNU89 -Werror COMPILE PASS [CRC stub] (real hsync.c)
...
3 checks, 0 failures
EXIT hsync_h1_host_crcstub=0
TARGET i386-elf -Werror COMPILE PASS (hsync.c)
TARGET i386-elf -Werror COMPILE PASS (lib/crc32.c)
TARGET i386-elf -Werror COMPILE PASS (fs/hostdrvfs.c)
```

RED は**実装を 1 か所ずつ「直す前」へ戻した変異**で取った ([V4]: 時系列で先に
赤い試験を書いた項目と、実装後に変異で確かめた項目が混ざっている。下表は後者を
含む「この試験が何を捕まえるか」の証拠であって、開発順の記録ではない)。

| 変異 | 戻した内容 | 落ちた検査 |
|---|---|---|
| M1 | 同一判定を「サイズが同じならスキップ」(2026-09-14 以前の実装) へ | **149 中 41 失敗** — A01 / A02 / A05 の全「1 バイト差」/ A06 の分割・エラー注入 / A08 / A09 / A13。CRC 贋物ビルドも 3 中 2 失敗 |
| M2 | `copy_verify` の読戻し検証を省く (書けたら成功) | 6 失敗 — A09 の全項目 (破損・`vfs_sync` 失敗・正常時の sync 回数) |
| M3 | `/sys` の除外を「全体同期か」を見ずに `depth==0` で効かせる | 1 失敗 — `hsync usr は usr/sys を除外しない` (A12) |
| M4 | dry-run でも書く | 7 失敗 — A13 の PLAN 行・`sys_write`/`vfs_sync` 回数・宛先内容・名前空間 |
| M5 | `hdrv_stat_fill` が Basic/Standard 取得失敗でも成功を返し、64bit を切り詰める (直す前の `hdrv_stat`) | 3 失敗 — A11 の 3 項目 |

M1 の RED (抜粋):

```
### M1_size_only  rc=1  totals=[('149', '41'), ('3', '2')]
    FAIL UPDATE ... reason=content_changed size=100 を出す
    FAIL 同 mtime でも content_changed でコピー
    FAIL size=65536 末尾 1 バイト差 -> copied
    FAIL 分割が違っても末尾の差を検出
    FAIL 予定サイズに達しない EOF -> エラー (同一扱いにしない)
    FAIL reason=verify_failed を出す
    ...
```

## 試験が言っていないこと

- ゲスト (NP21/W) 上での動作は一切見ていない。贋ファイルシステムは ext2 でも
  HostDrv でもない。実配備の受入 (A20: 同サイズ shlib の差し替え → 再起動 → GUI 起動) は
  PM とテスターの担当。
- 読戻しはキャッシュを経由する。媒体からの物理再読の保証ではない (票 H2/H4)。
- **H1 は宛先を直接上書きする。**コピー・検証の失敗時に旧宛先が残る保証は無い。
  試験もそれを前提に「失敗を成功件数に入れない」ことだけを見ており、
  旧内容の保存は見ていない (A14 は票 H2)。
- 性能 (A21) は測っていない。CRC 表サイズの選定根拠はホスト x86-64 上の相対値
  (`lib/crc32_core.inc` の頭に記録)。PC-98 実機・エミュレータでの測定ではない。

## 追記 2026-09-15 — Codex 実装レビューの blocker 3 件 (B1 / B2 / B3)

着地 (`e43fcb8`) 後のレビューで 3 件。PM が 3 件とも到達可能性を独立に確認済み。
修正と試験を同じ作法 (実物のソース + 変異で落ちること) で足した。

| ID | 直した内容 | 主な対象 |
|---|---|---|
| B1 | `\` を含むパスを断つ。OS32 の区切りは `/` だけなので `..\other` は**普通の 1 要素**として `..` 脱出検査を素通りするが、HostDrv の `session_set_path` が `/`→`\` に直したうえで他の文字をそのまま通すため、ホスト側では `C:\os32\..\other` になる。NP21/W の `hostdrvNT_getHostPath` は `PathCanonicalizeW` のあと**境界を見ない前方一致**でルート検査をするので `C:\os32-other` が通る。`argv` の dir と**列挙で得た名前の両方**を拒否 (`reason=bad_name`) | `hsp_has_backslash` / `hsp_normalize` / `ls_cb` / `main` |
| B2 | `/host` を前置した**あと**の要素数で上限を見る。`hsp_normalize` の `HSP_MAX_DEPTH` は入力側にしか掛からないので、32 要素の dir は通り、同期元は 33 要素になる。`fs/vfs.c` は上限超過の要素を**黙って捨てる**ので「指定した親ディレクトリ」を列挙してしまい、宛先側も化けて、何も同期していないのに `errors=0` / 終了コード 0 だった。越えたら `reason=path_too_deep` で `errors` に数える。あわせて **`PROTECTED` を「保護に当たったとき」だけの語**にし、正規化失敗は `reason=path_rejected` のエラーにした (`hsp_path_protected` の真偽 1 本を 3 値の `hsp_path_classify` に置き換え) | `hsp_depth` / `hsp_path_classify` / `HS_MAX_PATH_DEPTH` / `sync_directory` / `main` |
| B3 | `sys_mkdir` が `OS32_ERR_EXIST` を返したら宛先を `stat` して**ディレクトリであることを確かめる**。`fs/ext2_dir.c` の `ext2_find_entry` は**種別を問わず** EXIST を返すので、コピー元がディレクトリ・宛先が通常ファイルのまま `errors=0` / 終了コード 0 で終わっていた。`-n` は `mkdir` を呼ばないので、**dry-run でも型検査だけは行う** (書き込みはしない) | `dst_dir_type_ok` / `sync_directory` |

追加した検査は **`case_b1` 10 件 / `case_b2` 18 件 / `case_b3` 11 件 = 39 件**
(`hsync_h1_host.c`、149 → **188 checks / 0 failures**、CRC 贋物ビルドは 3 / 0 のまま)。
`hsync_protect_host.c` にも `.inc` の純関数ぶん (`hsp_has_backslash` /
`hsp_depth` / `hsp_path_classify` の 3 値) を **20 件**足した
(114 → **134 checks / 0 failures**)。既存の検査はすべてそのまま通っている。

`test_hsync_h1.py` は走らせる前に **[C4] の結合検査**を 1 本足した:
`fs/vfs.h` の `VFS_MAX_PATH_DEPTH` と `userland/system/hsync.c` の
`HS_MAX_PATH_DEPTH` を読んで一致を確かめ、`HSP_MAX_DEPTH` がそれを越えていない
ことも見る (外部プログラムから `fs/vfs.h` を引けないので写しを置くしかない。
写しが本体より大きいと化けたパスを通してしまう)。

### RED (変異)

`tools/tests/test_hsync_h1.py` を回した結果。括弧内は
`tools/tests/test_hsync_protect.py` での失敗数。

| 変異 | 戻した内容 | 落ちた検査 |
|---|---|---|
| B1 | `hsp_has_backslash` を常に 0 にし、`hsp_normalize` の `\` 検査を外す | **188 中 6 失敗** — `reason=bad_name` を出す / 要素の途中の `\` / 列挙名の `\` (非ゼロ終了・reason・名前を作らない) / `'\' 混じりも -1` (protect 側は 4 失敗) |
| B2a | `/host` を足したあとの深さ検査を 2 か所とも外す | 4 失敗 — off-by-one の境界 / `reason=path_too_deep` / 越えた先へ書かない / 再帰の途中の超過 |
| B2b | `hsp_path_classify` が正規化失敗で `-1` でなく `1` (= 保護) を返す | 2 失敗 — `33 要素は -1` / `'\' 混じりも -1` (protect 側は 2 失敗: `正規化できない長さは -1`) |
| B3 | `mkdir` の `EXIST` を無条件に受理する (dry-run の型検査も外す) | 7 失敗 — 非ゼロ終了 / `reason=type_conflict` / errors に数える / 中身があっても衝突 / 衝突した先へ書き込まない / dry-run の 2 件 |

B2b は経路としては深さ検査と `\` 検査が先に効くため、end-to-end だけでは
変異が落ちない。3 値そのものを直に呼ぶ検査 (`hsp_path_classify(...) == -1`) を
足して固定してある — **「判定できない」を「保護」に畳まない**ことが要点で、
`PROTECTED` が 2 つの意味を持たないこと自体を守っている。

### 直せなかった非 blocker

ホスト試験は **LP64 のまま**。`gcc -m32` に必要な 32bit ヘッダ
(`gcc-multilib` / `bits/libc-header-start.h`) がこの環境に無く、パッケージの
導入はこの票の範囲外なので入れていない。したがって `u32` (= `unsigned long`) は
ホスト上で 64bit であり、**32bit の加算回り込みはこの試験では未検証**。
CRC の値照合・分岐・件数の検査には影響しないが、桁溢れの根拠には使えない。
型を試験に合わせて変えることはしていない (実機は ILP32 のまま)。
実機幅での確認は `--target` の `i386-elf-gcc -Werror` コンパイルと、
ゲスト上の受入 (A20) に委ねる。

## 追記 2026-09-15 (2) — 往復 2 のレビュー: B3 の修正漏れ

往復 1 で入れた `dst_dir_type_ok()` は**列挙ループの中だけ**で呼ばれていた。
`main` は起点の型を見ずに `sync_directory(src, dst, 0)` を呼ぶので、
**コピー元が空ディレクトリだと子項目用の検査が一度も走らない**。

反例 (PM が到達可能性を確認済み): `/host/usr/empty` が空ディレクトリ、
`/usr/empty` が通常ファイル (settings.db の保護対象とは別実体) の状態で
`hsync usr/empty`。保護判定を通り、列挙項目が無く、型が食い違ったまま
`errors=0` / 終了コード 0。`-n` でも `-f` でも同じだった。

### 直した内容

`start_point_ok(src, dst)` を足し、**明示 dir の同期を始める前に 1 度だけ**
起点の型を確かめる。`main` は

```
if (!subdir || start_point_ok(src, dst) == 0) sync_directory(src, dst, 0);
```

とし、食い違っていたら同期を始めない。集計行と終了コードは既存の末尾を
そのまま通るので、`FAILED:` / `errors=1` / 非ゼロ終了になる。
語も集計も子項目側と同じ (`reason=type_conflict`、`fail_file()` 経由)。
dry-run も force も同じ検査を通る (書き込みは一切しない)。

全体同期 (`/host` -> `/`) の起点は検査しない。`sys_is_mounted("/host")` が
先に通っている以上ディレクトリであり、宛先は `""` (root) だから。

### コピー元が通常ファイルだった場合 (PM からの確認依頼)

**検査は「あるにはあったが、当てにできない形だった」** ので、同じ起点で
コピー元の型検査も足した。

- 直す前の実際の動き: `sync_directory` が `sys_ls(src_dir)` を呼び、
  `fs/hostdrvfs.c` の `hdrv_list_dir` は `NP2_FILE_DIRECTORY_FILE` で開くので
  通常ファイルなら `hostdrv_create` が失敗し **`VFS_ERR_NOTFOUND`** を返す。
  結果 `FAIL: ls /host/usr/file.txt (err=-2)` となり `errors=1` / 非ゼロ終了。
  **落ちること自体は落ちていた。**
- ただし (a) 実在するのに `-2` (= 不存在) と報告する、(b) 合意した
  `reason=type_conflict` の語を使わない、(c) **FS が「通常ファイルに空の列挙を
  成功として返す」実装なら素通りする** — B3 と同じ「他人の偶然のエラーに
  頼っている」形だった。
- そこで `start_point_ok()` でコピー元を `stat` し、ディレクトリでなければ
  `reason=type_conflict`、種別が取れなければ `reason=type_unknown`、
  stat 自体が失敗すれば `reason=io_error` で落とすようにした。

**別件として PM へ**: `hdrv_list_dir` は列挙の途中で `hostdrv_query_dir` が
負値を返しても `break` して **`VFS_OK` を返す** (`fs/hostdrvfs.c:525`)。
途中で切れた列挙が「全部読めた」ことになるので、B3 と同じ形の穴が
カーネル FS 側に残っている。この票の範囲外なので触っていない。

### 追加した試験

`case_b3_start` の **23 件** (`hsync_h1_host.c`、188 → **211 checks / 0 failures**)。
内訳は宛先側 18 件 (通常 / dry-run / force の 3 通り + 子項目あり + 宛先が
ディレクトリ / 宛先が無い / 空ディレクトリ同士の正常系) と、コピー元側 5 件
(通常ファイル / 不存在 / 種別不明)。CRC 贋物ビルド 3 / 0 と
`hsync_protect_host.c` 134 / 0 はそのまま。

### RED (変異)

| 変異 | 戻した内容 | 落ちた検査 |
|---|---|---|
| B3c | 起点の検査をせず `sync_directory` に入る (往復 2 の反例そのもの) | **211 中 15 失敗** — 通常 / dry-run / force の 3 通り全部 + 子項目あり + コピー元側 3 件 |
| B3d | 起点の**コピー元**の型検査だけ外す (`sys_ls` 任せに戻す) | 2 失敗 — `コピー元が通常ファイル: reason=type_conflict` / `種別不明: reason=type_unknown` |
| B3e | 起点の**宛先**の型検査だけ外す | 12 失敗 — 通常 / dry-run / force の 3 通り + 子項目あり |

B3c で `コピー元が通常ファイル: 非ゼロ終了` **だけは通ってしまう**。贋 FS の
`sys_ls` が実機と同じく通常ファイルにエラーを返すためで、まさに上で書いた
「偶然のエラーに頼っている」状態が試験の上でも見えている。語を見る検査
(`reason=type_conflict`) のほうは落ちる。

## 追記 2026-09-15 (3) — `hdrv_list_dir` が途中で切れた列挙を成功で返す

往復 2 の報告で「別件」として挙げたものを PM が確認し、**実在・hsync から
到達可能**と判定。H1 の完了条件「I/O 失敗を成功にしない」と対象「HostDrv の
エラー処理」に入るのでこの票で直した。

`fs/hostdrvfs.c` の `hdrv_list_dir` は列挙ループを 2 通りで抜け、どちらも
末尾で無条件に `return VFS_OK` していた。

- `if (rc < 0) break;` — `hostdrv_query_dir` の失敗。200 件のうち 50 件目で
  失敗すると **「50 件だけの成功した列挙」**。hsync は `rc == 0` を見るので
  50 件を同期して `errors=0` / 終了コード 0 で終わる。
- `if (count++ >= HOSTDRV_MAX_DIR_ENTRIES) break;` (上限 1000) — 件数上限での
  打ち切りも成功として返る。

### 直した内容

列挙ループを `fs/hostdrv_list_rules.inc` の純関数 `hdrv_list_run()` に切り出し、
1 件取得 (`hostdrv_query_dir`) と 1 件の組み立てを関数ポインタで受ける形にした
(`fs/hostdrv_stat_rules.inc` と同じ作法)。`hdrv_list_dir` は step / emit を渡して
呼ぶだけになり、判定の写しはどこにも無い。`hostdrv_cleanup_close()` は
**戻り値にかかわらず必ず通す**。

| 抜けかた | 直す前 | 直したあと |
|---|---|---|
| 最後まで届いた | `VFS_OK` | `0` (変更なし) |
| `query_dir` が負値 | `VFS_OK` | **`OS32_ERR_IO`** (修正 1) |
| 件数上限で打ち切り | `VFS_OK` | **`OS32_ERR_FULL`** (修正 2) |

2 つを別の値にしてあるので、呼び手は「読めなかった」と「多すぎる」を
区別できる。名前が化けた / `.` / `..` を流さないことと、それでも繰り返しを
1 回消費することは**従来どおり**。

### 修正 2 は挙動の変更 — 切り離せる形

`fs/hostdrv_list_rules.inc` の `hdrv_list_run()` にある

```c
        if (count++ >= max_entries) {
            /* [H1-CAP] 修正 2。ここを `return 0;` に戻すと修正 1 だけが残る */
            return HDRV_LIST_ERR_CAPPED;
        }
```

の 1 行を `return 0;` にすれば修正 1 だけが残る。試験側も
`hostdrv_list_host.c` の `== (b) 件数上限での打ち切り ==` の節 (5 件) と
`hsync.1` の該当行を落とすだけで、他の検査には触らない。

### `list_dir` / `sys_ls` の呼び出し元と、件数上限をエラーにした影響

まず範囲: `HOSTDRV_MAX_DIR_ENTRIES` は **HostDrv (`/host`) だけ**の上限。
ext2 と iso9660 は自前の `list_dir` を持つので影響を受けない
(`kernel/kernel.c:320` で `vfs_mount("/host", "hostdrv", "hostdrv")`、
マウント点は 1 つ)。

カーネル側の `vfs_ls` 呼び出しは `kapi/kapi_generated.c` の `wrap_sys_ls` の
1 本だけ。`ops->list_dir` を直接呼ぶのは `fs/vfs.c` の `vfs_ls` と
`vfs_path_kind` のプローブ。

| 呼び出し元 | 戻り値の扱い | 自前の件数上限 | 上限エラー化の影響 |
|---|---|---|---|
| `userland/system/hsync.c` (2 か所) | `rc != 0` を `FAIL: ls` で `errors` に数える | `MAX_FILES` 128 | **狙いどおり厳しくなる**。128 件超はもともと明示エラー |
| `userland/shell/cmd_dir.c` (`ls`) | **見ていない** | `SH_LS_MAX` 128 | 表示は変わらない (128 件で先に頭打ち) |
| `userland/shell/sh_args.inc` (glob) | 見ていない | `SH_LS_MAX` 128 | 変化なし |
| `userland/shell/ui.c` (tab 補完) | 見ていない | `TAB_MAX_MATCHES` 40 | 変化なし |
| `userland/shell/cmd_file.c` (`cp -r`) | 見ていない | `MAX_COPY_ENTRIES` 64 (超過は既にエラー表示) | 変化なし |
| `userland/shell/cmd_filer.c` / `userland/lib/filer/filer_core.c` | 見ていない | `FILER_MAX_ENTRIES` 128 | 表示は変わらない |
| `userland/rust/filer` | **戻り値を検査する** (`model.rs:765` の `if rc < 0`、`lib.rs:408` の `self.error(b"Read directory", rc)`) | `FL_MAX_ENTRIES` 256 | エラー表示に変わり、ツリー展開を中止する。**「表示は変わらない」は誤り** (2026-09-15 訂正) |
| `userland/cmds/du.c` `find.c` `man.c` `tar.c` | 見ていない | なし | 変化なし (戻り値を捨てているため) |
| `userland/system/install.c` | 負値を**失敗件数に数える** | `MAX_FILES` 相当 | FDD (ext2/FAT) が相手なので HostDrv の上限に当たらない |
| `userland/shell/cmd_fs_shared.c` `fs_is_dir()` | **`rc == 0` ならディレクトリ**と判定 | なし | ★ 1000 件超の `/host/...` を「ディレクトリでない」と言う。`cp` / `mv` (`cmd_file.c` の 5 か所) が使う |
| `fs/vfs.c` `vfs_path_kind()` のプローブ | `== VFS_OK` ならディレクトリ | なし | HostDrv には `stat` があり**先に成功する**ので、このプローブには落ちてこない (票 H1 で `hdrv_stat` を直したあとは特に) |
| `userland/tests/test4.c` | 見ていない | — | 試験用 |

**意見**: 入れてよいと考える。理由は 3 つ。

1. 上限 1000 に当たるのは `/host` 配下だけで、`/host` は `make deploy` が作る
   配備ツリー (`C:\os32`)。`userland/deploy.yaml` の登録は 92 件、いちばん多い
   `man` ページでも 64 件で、**1 ディレクトリ 1000 件に届く現実的な経路が無い**。
2. ほぼ全ての呼び出し元が `sys_ls` の戻り値を**見ていない**うえ、自前の上限
   (40〜256) が先に効く。エラーにしても表示・動作は変わらない。
3. 唯一意味が変わるのは `fs_is_dir()` 経由の `cp` / `mv` で、1000 件超の
   `/host` ディレクトリを「ディレクトリでない」と判定する。ただし
   (a) その条件自体が上記 1 で現実的でなく、(b) 仮に起きたら今は
   **黙って 1000 件だけコピーする**ので、断るほうが安全。

気になるなら `fs_is_dir()` を `sys_stat` 優先に直すのが本筋 (`fs_path_kind()` は
既にそうなっている) が、**呼び出し元は指示どおり 1 行も変えていない**。判断は PM へ。

### 追加した試験

- `tools/tests/hostdrv_list_host.c` + `tools/tests/test_hostdrv_list.py` (新設、
  `build/sdk.mk` に `check-hostdrv-list-host` を登録し `check:` の列に追加)。
  **23 件 / 0 failures**。正常系 7 / (a) 途中で負値 5 / (b) 件数上限 5 /
  引数の防御 3 / 2 つのエラーが別物 2。`--target` で `fs/hostdrvfs.c` の
  クロスコンパイルも見る。上限定数が 1 か所にしか無いことの検査つき ([C4])。
- `hsync_h1_host.c` に `case_b4` **8 件** (211 → **219 checks / 0 failures**)。
  贋 `sys_ls` に「N 件流してから I/O エラー」を注入し、**呼び手側の意味** —
  部分同期を成功と呼ばない・`copied=0`・`Done:` へ進まない・dry-run でも
  `errors` に数える — を固定する。

### RED (変異)

| 変異 | 戻した内容 | 落ちた検査 |
|---|---|---|
| B4a | `rc < 0` で `return 0` (修正 1 を戻す) | `test_hostdrv_list.py` **23 中 4 失敗** — VFS_OK を返さない / OS32_ERR_IO / 1 件目で失敗 / 最後の 1 件で失敗 |
| B4b | 件数上限で `return 0` (修正 2 を戻す) | **23 中 3 失敗** — VFS_OK を返さない / OS32_ERR_FULL / 上限ちょうど |
| B4c | 呼び手 (`hsync`) が `sys_ls` の戻り値を無視する | `test_hsync_h1.py` **219 中 7 失敗** — `case_b4` の全項目 |

B4a と B4b が互いの検査を落とさないことも、この 2 つが独立して外せる根拠。

## 追記 2026-09-15 (4) — 往復 3 のレビュー: `d574704` で入れた退行 2 件

`hdrv_list_dir` を厳しくした結果、**列挙の戻り値を「種別」として使っていた
2 か所**が壊れた。どちらも自分が入れた退行で、往復 3 の影響調査が甘かった
(「戻り値を見ていないから影響なし」とだけ見て、**見ている** 2 か所を
取りこぼした)。**修正 1 (途中失敗 → IO) だけでも同じ退行が起きる**ので、
修正 2 を戻すだけでは足りない。

### B5 — `cp -r` が宛先の階層を取り違えて上書きする

`fs_is_dir()` が `sys_ls` の戻り値 0 だけを見ていた。`cmd_file.c:227` は
その結果で分岐し、偽なら `do_copy_recursive(src, dst)` を呼ぶ。
1001 件を持つ `/host/big` に `cp -r /src /host/big` すると、列挙が
`OS32_ERR_FULL` → `fs_is_dir` 偽 → `/host/big/src/a.txt` ではなく
**`/host/big/a.txt` を上書き**。`do_copy_recursive` は `sys_mkdir` の失敗を
無視するので気づけない。列挙の途中 I/O エラーでも同じ。

**直した内容** (`userland/shell/cmd_fs_shared.c`):

- `fs_is_dir()` は `fs_path_kind(path) == FS_KIND_DIR` になった。
  **型で判定する**。列挙の成否は使わない。
- `fs_path_kind()` は従来どおり `sys_stat` が正。stat が使えない FS のための
  代替だけを `fs_ls_says_dir()` に切り出し、**3 値**
  (1 = ディレクトリ / 0 = ディレクトリでない / -1 = 読めなかった) にした。
  `OS32_ERR_IO` や `OS32_ERR_FULL` を「ディレクトリでない」に畳まない。
- `cmd_fs_shared.h` の「sys_ls が成功すればディレクトリ」という説明も直した。

**呼び出し元の意味は変えていない** (`cmd_file.c` は 1 行も触っていない)。
`fs_is_dir` は真偽のまま、種別が分からないときは 0 を返す — 従来と同じ形。

| 呼び出し元 | 前 | 後 |
|---|---|---|
| `cmd_file.c:210` `cp` の宛先 | 列挙が通れば真 | **stat が DIR なら真** |
| `cmd_file.c:221` `cp` のコピー元 | 同上 | 同上 |
| `cmd_file.c:276` `mv` のコピー元 | 同上 | 同上 |
| `cmd_file.c:302` `mv` の宛先 | 同上 | 同上 |
| `cmd_file.c:334` `rm` の対象 | 同上 | 同上 |
| `cmd_fs_shared.c:52` `fs_path_kind` の代替 | `fs_is_dir` を呼ぶ | `fs_ls_says_dir` を呼ぶ (再帰しない) |

**残る穴 (直していない、PM 判断へ)**: stat も列挙も失敗した場合、
`fs_is_dir` は 0 を返すので `cp -r` は「宛先はディレクトリでない」側へ行く。
今回の反例 (HostDrv の 1000 件超) は `hdrv_stat` が答えるので**到達しない**が、
「不明」を真偽 1 本で表す限りこの形は残る。塞ぐには `cp` / `mv` 側で
`fs_path_kind()` の負値を見て**断る**のが本筋 (呼び出し元の変更になるので
この票ではやっていない)。

### B6 — HostDrv のディレクトリがファイルと判定される

`vfs_path_kind()` は stat が `NOTFOUND` 以外で失敗すると「ドライバが stat
未対応」とみなしてプローブへ落ちていた。`hdrv_get_file_size` は
`NP2_FILE_DIRECTORY_FILE` も `NON_DIRECTORY_FILE` も指定せずに開くので
**ディレクトリでも成功する**。結果、stat が一時的に読めなかっただけの
1001 件のディレクトリが `VFS_KIND_FILE` になり、`cd` は NOTDIR、
`sys_open(..., O_RDONLY)` はディレクトリ拒否 (`fs/vfs_fd.c:87`) をすり抜けた。

**直した内容** (`fs/vfs.c`):

- **`stat` を持つドライバの答えは最終判断**。失敗してもプローブへ落とさず、
  そのエラーを返す。「未対応」はドライバが `stat` を**持たない**ことで表す
  (`ops->stat == 0`)。
- プローブ自体も直した: `list_dir` が `VFS_OK` ならディレクトリ、
  **「ディレクトリではない」と分かるエラー (`NOTDIR` / `NOTFOUND`) のときだけ**
  `get_file_size` へ進む。`OS32_ERR_IO` / `OS32_ERR_FULL` はそのまま伝える。

**他の FS への影響**: ext2 / FAT / ISO9660 / HostDrv は **4 つとも `stat` を
持つ**ので、プローブはもともと「どれかの stat が失敗したとき」しか動いて
いなかった (`ops->stat == 0` のドライバは 1 つも無い)。

| 場面 | 前 | 後 |
|---|---|---|
| stat が成功 | 種別を返す | 同じ |
| stat が `NOTFOUND` | `NOTFOUND` | 同じ |
| マウントルート | `VFS_KIND_DIR` (ドライバに聞かない) | 同じ |
| stat がその他のエラー (ext2 の `read_inode` 失敗 = IO、`buf` が NULL = INVAL、ISO9660 の解決失敗、FAT の `ff_stat_to_vfs`) | プローブへ落ち、`list_dir` が通れば DIR / `get_file_size` が通れば FILE | **そのエラーを返す** |

変わるのは最後の行だけで、**読めなかった inode を種別として答えていた**のを
やめる方向。`vfs_chdir` は `kind < 0` をそのまま返す作りなので受け手側の
変更は要らない。FAT の `f_stat` はボリュームルートで失敗するが、
`vfs_path_kind` は**ドライバに聞く前に** `vfs_rel_is_root()` で DIR を返すので
そこは影響しない (試験で押さえた)。

### 追加した試験

- `tools/tests/fs_kind_host.c` + `test_fs_kind.py` (新設、`build/sdk.mk` に
  `check-fs-kind-host`)。実物の `cmd_fs_shared.c` と `cmd_file.c` を
  `#include` し、KernelAPI と shell.c の 2 本だけを贋物に。贋 FS は
  `sys_stat` を正しく答えさせたまま `sys_ls` だけを FULL / IO にできる。
  **21 checks / 0 failures**。中心は **`cp -r` の宛先階層** —
  `cp -r /src /big` が `/big/src/a.txt` へ入り `/big/a.txt` (内容 `KEEP`) を
  上書きしないこと。単一ファイルの `cp`、複数入力の `cp` も見る。
- `tools/tests/vfs_kind_host.c` + `test_vfs_kind.py` (新設、
  `check-vfs-kind-host`)。実物の `fs/vfs.c` を `#include` し、境界
  (kstring / kmalloc) だけ差し替え。合成 `VfsOps` で stat / list_dir /
  get_file_size の戻り値を 1 つずつ指定する。**20 checks / 0 failures**。
  `get_file_size` は HostDrv と同じく**ディレクトリでも成功する**贋物。

### RED (変異)

| 変異 | 戻した内容 | 落ちた検査 |
|---|---|---|
| B5a | `fs_is_dir` が `sys_ls` の戻り値を見る (`d574704` の形) | `test_fs_kind.py` **21 中 9 失敗** — 列挙 FULL / IO でディレクトリと答えない、`cp -r` が `/big/a.txt` を上書き、単一ファイル `cp`、複数入力 `cp` の誤拒否 |
| B5b | 代替判定の 3 値を真偽に畳む (`IO`/`FULL` を「ディレクトリでない」に) | **0 失敗**。今日は観測差が出ない — 代替は stat が失敗したときしか走らず、0 でも -1 でも `fs_path_kind` は stat のエラーを返すため。3 値は「将来また畳まれない」ための記述で、**挙動を担っているのは B5a 側**。[V4] のため変異が落ちないことをそのまま記録する |
| B6a | stat の失敗をプローブへ落とす (`d574704` の形) | `test_vfs_kind.py` **20 中 3 失敗** — stat のエラーを伝える / プローブへ落ちない / その他のエラーも伝える |
| B6b | プローブが列挙のエラーを問わず `get_file_size` へ進む | **20 中 4 失敗** — 列挙が FULL / IO のときそのまま伝える、`get_file_size` を呼ばない |

### 非 blocker 2 件

1. `docs/manpages/hsync.1` の上限の書き方を実態に合わせた。上限は
   **「1 ディレクトリにつき問い合わせ 1000 回」**で「1000 件までは成功」の
   保証ではない (`.` / `..` と変換失敗で読み飛ばした項目も回数を消費する。
   999 レコード + 終了応答なら成功、1000 レコードでは終了応答を確認する前に
   FULL)。
2. 上の呼び出し元表の `userland/rust/filer` の行を訂正した。**戻り値を
   検査している** (`model.rs:765` の `if rc < 0 { return rc; }`、`lib.rs:408`
   の `self.error(b"Read directory", rc)`) ので、エラー表示とツリー展開の
   中止へ進む。「表示は変わらない」は誤りだった (挙動としては適切な
   エラー処理なので、直すのは記録のほう)。

## 追記 2026-09-15 (5) — 往復 4 のレビュー: B6 の**受け手**が追従していなかった

### B7 — 種別が不明なときに open を通してしまう

`fs/vfs_fd.c:87` は `if (vfs_path_kind(resolved) == VFS_KIND_DIR) return
VFS_ERR_ISDIR;` と、**DIR に一致したときだけ拒否**していた。B6 で
「stat を持つドライバの失敗をそのまま返す」ようにしたので、**従来は列挙経由で
DIR と判定されて拒否されていた経路が負値になり、拒否をすり抜ける**。その先の
`get_file_size` はディレクトリでも成功するため FD が発行される。

反例: 実在する `/host/d` に `sys_open("/host/d", O_RDONLY)` で、`hdrv_stat` の
Basic 問い合わせだけが IO、Standard と列挙は成功する状況。ext2 でも、対象
ディレクトリの stat 用 inode 読み出しだけが一時的に失敗して後続が成功すると
同じで、**`cat /etc` がディレクトリの生データを読める**。

`fs/vfs_fd.c:84-86` の註は、まさにこの不具合を過去に直したときの記録
(`cat /etc` が生のディレクトリブロックを吐き、`mv dir x` が生データを書いた)。
**B6 でその一部を開け直してしまった。**自分が入れた退行で、B5 とまったく
同じ形の誤り — 「エラー」を「ディレクトリではない」と読み替えていた。

**直した内容** (`fs/vfs_fd.c` の `vfs_open_internal`):

```c
    kind = vfs_path_kind(resolved);
    if (kind == VFS_KIND_DIR) return VFS_ERR_ISDIR;
    if (kind < 0 && kind != VFS_ERR_NOTFOUND) return kind;
```

| `vfs_path_kind` の答え | 動作 |
|---|---|
| `VFS_KIND_DIR` | `VFS_ERR_ISDIR` (従来どおり) |
| `VFS_KIND_FILE` | 続行 (従来どおり) |
| `VFS_ERR_NOTFOUND` | 続行 — この下に `O_CREAT` の作成経路がある (従来どおり) |
| **それ以外の負値** | **そのエラーを返して open しない** (B7 で追加) |

### `vfs_path_kind` の消費者 — PM の理解は正しい

リポジトリ全体を `vfs_path_kind` / `VFS_KIND_` で洗い出した結果、
**カーネル内の消費者は 2 箇所だけ**で、PM の数えかたと一致する。

| 場所 | 消費のしかた | 判定 |
|---|---|---|
| `fs/vfs_fd.c:87` (`vfs_open_internal`) | DIR なら `ISDIR`、負値は**今回**そのまま返す | **B7 で直した** |
| `fs/vfs.c:38` (`vfs_chdir`) | `if (kind < 0) return kind;` → `if (kind != VFS_KIND_DIR) return VFS_ERR_NOTDIR;` | **正しい**。負値をそのまま返し cwd を更新しない |
| `tools/tests/vfs_fd_sqlite_host.c:90` | 試験の贋物 (常に `VFS_KIND_FILE`) | 実装ではない |

**他に種別の判定結果を消費する箇所は無い**ことも併せて確認した。

- **open の入口は 1 本**。`vfs_open()` (KAPI `sys_open` = `kapi_generated.c:924`)
  も `vfs_open_sqlite()` (SQLite VFS = `lib/sqlite3/os32_sqlite_vfs.c:359` は
  `vfs_open` 経由) も、どちらも `vfs_open_internal()` を通る。したがって
  今回の 1 か所の修正で**カーネル・KAPI・SQLite VFS の全経路**が塞がる。
- `vfs_rm` / `vfs_rename` / `vfs_rmdir` / `vfs_read` / `vfs_write` は
  `vfs_path_kind` を**呼んでいない**。ドライバの `unlink` / `rename` に委ね、
  そちら側に独自の型検査がある (`fs/ext2_file.c:177` の `EXT2_ERR_ISDIR`、
  `fs/ext2_dir.c:22` の `EXT2_ERR_NOTDIR`)。B6 / B7 の影響を受けない。
- KAPI ラッパ (`kapi/kapi_generated.c`) は種別を自分で判定せず、
  `vfs_ls` / `vfs_stat` / `vfs_open` をそのまま呼ぶだけ。
- `kapi/kapi_db.c:916,935` は `vfs_stat` を直接使う (種別ではなくサイズの確認)。
- ユーザーランド側の種別判定は `fs_path_kind` / `fs_is_dir` で、これは往復 3 の
  B5 で直した別系統 (`sys_stat` が正)。

### 検出漏れの原因と、試験の直し

`tools/tests/vfs_kind_host.c` が **`vfs_path_kind` の戻り値までしか見て
いなかった**のが原因。判定を**消費する側**を通していなければ、受け手が
追従していないことは分からない。

そこで同じ翻訳単位に**実物の `fs/vfs_fd.c` も取り込み**、`vfs_open()` まで
通すようにした (境界はコンソール / リダイレクト / `res_owner_get` の 6 本だけ)。

### 追加した試験

`vfs_kind_host.c` に `== B7: open の受け手が種別の不明をどう扱うか ==` の
**15 件** (20 → **35 checks / 0 failures**)。

- HostDrv 相当 (stat が IO / 列挙は成功 / `get_file_size` はディレクトリでも
  成功) → **FD を返さない**・そのエラーを返す・`get_file_size` まで進まない
- ext2 相当 (stat が IO で後続は成功) → 同じ (`cat /etc` を通さない)
- `O_CREAT` でも種別が不明なら開かない・作らない
- 正常系の回帰: 通常ファイルは開ける / DIR は `ISDIR` / 不存在 + `O_CREAT` は
  作れる (空ファイルを 1 度書く) / 不存在で `O_CREAT` 無しは `NOTFOUND`
- stat を持たないドライバ経由でも受け手は同じ

`test_vfs_kind.py` は `--target` で `fs/vfs.c` に加えて `fs/vfs_fd.c` の
クロスコンパイルも見る (`vfs_fd.c` は元から `-Wextra` の 2 件が出るので
そこだけ外す — `vfs_fstat` の `sizeof` 比較と `vfs_sys_compat_shell_print`
の未使用 `attr`)。

### RED (変異)

| 変異 | 戻した内容 | 落ちた検査 |
|---|---|---|
| B7a | DIR に一致したときだけ拒否 (`229ee68` の形 = 反例そのもの) | **35 中 8 失敗** — HostDrv 相当 3 件 / ext2 相当 2 件 / `O_CREAT` 1 件 / stat 無しドライバ 2 件 |
| B7b | `NOTFOUND` まで弾く (行き過ぎの否定側) | **2 失敗** — 不存在 + `O_CREAT` が作れない |
| B7c | `ISDIR` の拒否そのものを外す (過去に直した不具合の再発) | **1 失敗** — DIR が `ISDIR` にならない |

B7b と B7c があるので、この試験は「厳しくしすぎ」も「緩めすぎ」も捕まえる。

