# userland/system/hsync.c — 一時ファイル + 検証 + 置換

> 状態: **現行 (2026-11-18)** — 出所: TASK_H2 の契約を今のソースと照らして要約
ソース: `userland/system/hsync.c` (`replace_file`・`build_temp_path`・`drop_temp`・`fail_before_publish`・`remove_stale_temp`・`clean_temps`・`ls_cb`・`dst_on_floppy`・`sync_directory`・`main`)

この文書は「写すと決まった後」に旧宛先を守る手順を扱う。写すかどうかの判定は同じ置き場の `hsync_compare.md`。
カーネル側の前提 (`O_EXCL` と ext2 の置き換え順序) は `docs/design/fs/vfs_fd_open.md` と `docs/design/fs/ext2/ext2_rename_replace.md` にある。

## 保証すること

- 新しい内容の書き込み・検証が済むまで**旧宛先の名前・内容・サイズ・mtime・inode を変えない**。
- 途中で落ちても本名に半端な内容は現れない (新規ファイルなら本名は無いまま)。
- 「公開した (本名が新しい内容を指した)」後は旧内容へ戻そうとしない。
- 電源断に対する原子性は保証しない。止まった媒体は次の実行で内容が収束する (下の「予約名」)。媒体の修復 (links の過大・漏れ) はホストの `e2fsck`。

## 手順 (`replace_file`、dry-run 以外)

| 手順 | すること | 失敗したとき |
|---|---|---|
| 1 | 一時名 `<宛先の親>/.hs~<名前>` を組む。`strlen(名前) + 4 >= NAME_CAP` (64) なら組まない | `name_too_long`。直接上書きへは落とさない |
| 2 | `sys_open(tmp, O_WRONLY \| O_CREAT \| O_EXCL)` | `EXIST` → 予約名として消せれば 1 回だけ作り直す、消せなければ `temp_exists`。`NOSYS` → `replace_unsupported` (**直接上書きへ黙って落ちない**)。`NOSPC` / `FULL` → `no_space`。他は `io_error` |
| 3 | コピー元を一時ファイルへ書き、CRC と総数を取る | `copy_body` の理由 (`io_error` / `no_space`)。総数が判定時のサイズと違えば `source_changed` |
| 4 | `vfs_sync` → 一時ファイルを読み直して長さと CRC を照合 | `verify_failed` |
| 5 | 再確認: コピー元のサイズと mtime が判定時と同じ / 宛先が判定時の状態のまま (無かった → まだ無い、通常ファイル → まだ通常ファイルでサイズと mtime が同じ) / 保護判定を取り直す。一時ファイルと旧宛先の `st_ino` を控える | `source_changed` / `dest_changed` / `settings_db` (保護)・`path_rejected`・`io_error` |
| 6 | 宛先の `st_nlink > 1` を断る (一時ファイルを作る**前**にも `sync_file` が見る) | `hardlink`。別名まで更新しない |
| 7 | 一時ファイルに `sys_set_mtime` | `metadata_failed`。**公開の前の失敗**なので中断し `copied` に数えない |
| 8 | `sys_rename(tmp, 宛先)` | 下の「公開の判定」 |
| 9 | `vfs_sync` | `sync_failed` (置換は済んでいる可能性があると表示) |

手順 1〜7 の失敗は**公開の前**で、表示に「公開の前なので旧宛先はそのまま」と添える。
後始末で消すのは**この実行が `O_EXCL` で作った一時ファイルだけ** (`drop_temp`)。消せなければ `STALE <tmp>` を表示する。
手順 7 の後に ext2 がマウントを書き込み禁止 (`ROFS`) にしていると、続く rename も一時ファイルの unlink も断られる。
その一時ファイルは次の実行が予約名として片づける。

## 公開の判定 (手順 8 の rename が非ゼロ)

**宛先の `st_ino` で決める。** サイズや CRC は証拠にしない (`-f` で新旧が同じ内容だと未公開を公開と誤る)。

| 宛先の stat | 理由 | 意味 |
|---|---|---|
| 成功し、一時ファイルの ino と一致 | `replace_partial` | 公開済み。宛先は検証済みの新しい内容で、後始末が落ちた |
| 成功し、旧宛先の ino と一致 | `replace_failed` | 未公開。宛先は旧内容のまま |
| 宛先が判定時に無く、今も NOTFOUND | `replace_failed` | 未公開。宛先は作られていない |
| それ以外 (stat の失敗・どちらでもない ino) | `replace_unknown` | 公開したか分からない |

どれも errors で非ゼロ終了、`copied` に数えない。rename が `BUSY` (開いている SQLite DB とそのジャーナル・祖先) で未公開なら、
DB を閉じてから同期するよう案内を添える。

## 数え方

- **1 つの失敗は 1 件。** 手順 7 の `metadata_failed` の後の `STALE` は同じ失敗の続きなので数え直さない。
- 公開の前の失敗と手順 8 の失敗では、一時ファイルを消せなかったことを独立した 1 件として数える。
- 再起動の案内 (`/sys` / `/boot`) は**内容が媒体の上で入れ替わったら**出す: 成功、手順 9 の `sync_failed`、`replace_partial`。
  `replace_failed` と `replace_unknown` では出さない。

## 予約名 `.hs~`

- `.hs~` で始まる名前は hsync の予約 (`HS_TEMP_PREFIX`)。利用者のファイルでも消える。予約は `docs/manpages/hsync.1` に明記してある。
- 所有の根拠は名前空間の予約だけ。`st_nlink` や作成時刻は**見ない** (途中で止まった媒体では 2 になり得る)。
- 掃除 (`clean_temps`) は hsync が**実際に訪れた**宛先ディレクトリで、そのディレクトリの同期より前に行う。
  対象は `.hs~` で始まる**通常ファイル**だけ (ディレクトリ・特殊ファイルは消さない)。表示は `CLEAN <path>` (dry-run は `PLAN-CLEAN`、消さない)、集計は `cleaned`。
- 保護対象の実体 (settings.db 系への hardlink) は予約より保護を優先し、消さずに `PROTECTED <path> reason=protected`。
- 掃除の列挙は同期の列挙と枠を分ける (`MAX_TEMPS` 32)。枠を超えた・`NAME_CAP` に入らない予約名があれば「全部は見ていない」と表示し、掃除の完了を主張しない。
- 手順 2 で一時名が既に在ったとき (`remove_stale_temp`) も同じ条件: 通常ファイルで保護対象でなければ消して作り直す。ディレクトリ等は `temp_exists`。
- コピー元の列挙で `.hs~` で始まる名前は同期しない。同期の 128 件の枠に入れる前に弾き、`excluded` に数える (-v で `EXCLUDE <コピー元のパス> reason=reserved_name`)。

## 古いカーネルの門

- 一時ファイル方式は KAPI v53 (`O_EXCL` と ext2 の新しい置き換え順序) が要る (`HS_MIN_KAPI_H2`)。
- `api->version < 53` なら**既定は 1 件も書かずに** `kernel_too_old` で非ゼロ終了。dry-run でも同じ。`-f` では解除しない。
- `--unsafe-overwrite` を明示したときだけ旧来の直接上書き (`copy_verify`、宛先を `O_TRUNC` で開く) で進み、
  `WARN kernel KAPI v52 < 53: direct overwrite (no H2)` と集計 `direct_overwrite=N` を出す。失敗した回の旧内容は残らない。
- v53 以上のカーネルで `--unsafe-overwrite` を付けても無視して一時ファイル方式で進む。
- `build/app.conf` の hsync の要求 API 版は **52 のまま**にする。53 にすると v52 のカーネルの exec が起動そのものを断り、この門に届かない。

## 同期先の制限

- 同期先 (根を前置した宛先) のマウントのデバイス名が `fd` で始まれば、掃除・名札・mkdir より前に `dest_on_fd` で断る。
  FAT には `O_EXCL` が無く、ファイルは `replace_unsupported` で落ちても mkdir は通ってしまうため。
  マウントは宛先から親へ遡り、最初に名前の返るマウント点 (最長一致) で決める (`vfs_devname`)。
- 同期中、宛先の子が別のマウントの根なら入らない (掃除・mkdir・コピーもしない)。`EXCLUDE <path> reason=other_mount` を -v が無くても 1 行出し、
  `excluded` に数える (失敗にしない)。既定の `/sys` 除外より先に見る。明示した起点そのものは対象にしない。
