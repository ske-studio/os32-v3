# sys/dev/hostdrv/hostdrv_list_rules.inc — HostDrv の列挙ループ

> 状態: **現行 (2026-11-18)** — 出所: HSYNC_IMPROVEMENT_PLAN (H1 の「I/O 失敗を成功にしない」) の契約を今のソースと照らして要約
ソース: `sys/dev/hostdrv/hostdrv_list_rules.inc` (`hdrv_list_run`・`HDRV_LIST_ERR_IO`・`HDRV_LIST_ERR_CAPPED`)・`sys/dev/hostdrv/hostdrvfs.c` (`hdrv_list_dir`・`hdrv_list_step`・`hdrv_list_emit`・`HOSTDRV_MAX_DIR_ENTRIES`)

HostDrv の一覧は「1 件ずつ問い合わせ、終わりの応答が来るまで繰り返す」。ハイパーコールはホストで組めないので、
ループの規則だけを純関数 `hdrv_list_run` に切り出し、`hdrv_list_dir` は 1 件の取得 (`hostdrv_query_dir`) と
1 件の組み立てを関数ポインタで渡して呼ぶだけにしてある。判定の写しは他に無い。

## 契約

**部分的な一覧を `VFS_OK` で返さない。** 呼び手 (hsync など) は `rc != 0` で「このディレクトリは同期できなかった」と言えるが、
途中までの一覧を成功で返すと、残りを「存在しない」と扱ってしまう。

| 抜け方 | 戻り値 |
|---|---|
| 終わりの応答まで届いた (`step` が正) | 0 |
| 1 件の取得が負値 | `HDRV_LIST_ERR_IO` (= `OS32_ERR_IO`) |
| 問い合わせ回数が上限に達した | `HDRV_LIST_ERR_CAPPED` (= `OS32_ERR_FULL`) |
| `step` / `emit` が NULL、上限が 0 以下 | `HDRV_LIST_ERR_IO` (無限ループにしない) |

- 2 つのエラーは別の値 (どちらも 0 ではない)。呼び手は「読めなかった」と「多すぎる」を区別できる。
- 上限は**問い合わせの回数**で数える。`count++ >= max_entries` を問い合わせの前に見るので、上限ちょうどの回数に達した時点で
  終わりの応答を確かめる前に `CAPPED`。上限の 1 つ手前の件数 + 終わりの応答なら成功。上限を超えて問い合わせない。
- 最初の問い合わせだけ `first = 1`、2 回目以降は 0 (再開の約束)。毎回 1 にすると同じ 1 件を読み続ける。
- 空のディレクトリ (最初の応答が終わり) は 0。1 件目で失敗しても「空」と言わない。最後の 1 件で失敗しても失敗。
- `.`・`..`・名前の変換に失敗した項目は `hdrv_list_emit` が流さないが、繰り返しは 1 回ぶん消費する。
- `HOSTDRV_MAX_DIR_ENTRIES` (1000) は HostDrv (`/host`) だけの上限。ext2・ISO9660 は自前の `list_dir` を持ち影響を受けない。

## `hdrv_list_dir` の約束

- ディレクトリを開く `hostdrv_create` の失敗は、その VFS エラーをそのまま返す (`NOTFOUND` に畳まない。写し方は `hostdrv_stat_rules.md` の `hdrv_create_status_to_vfs`)。
- `hostdrv_cleanup_close` は戻り値にかかわらず必ず通す。
- `hdrv_list_run` の戻り値をそのまま返す。

## 呼び手への影響

- `sys_ls` の戻り値を見ない呼び手 (シェルの `ls`・補完・filer など) は、自前の件数上限が先に効くので表示は変わらない。
- 戻り値を見る呼び手 (`hsync`、`userland/rust/filer`) はエラーとして扱う。
- 列挙の成否を「ディレクトリかどうか」に使ってはいけない (`FULL` / `IO` はディレクトリでも返る)。種別は stat で決める
  (`docs/design/fs/vfs_path_kind.md`、`docs/design/userland/shell/cmd_fs_shared.md`)。
