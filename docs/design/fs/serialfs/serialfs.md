# sys/fs/serialfs/serialfs.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SERIAL_HOSTFS 部品 B (§1-v2 B-3') の契約を今のソースと照らして要約
ソース: `sys/fs/serialfs/serialfs.c` (VfsOps・`sfs_status`)・形式 `sys/fs/serialfs/sfs_proto.h`

SerialFS の VFS の口。要求 / 応答は `sys/fs/serialfs/sfs_client.c`、セッションは `sys/fs/serialfs/serialfs_session.c`。
hsync と push が使う操作を満たす。

## VFS の契約

- `stat`: 種別 (`SFS_KIND_FILE` / `DIR` / `OTHER`)・サイズ・0 でない mtime (UNIX 秒 UTC) を返す。
- `get_file_size`: ディレクトリには `OS32_ERR_ISDIR`。
- `list_dir`: ファイルには `OS32_ERR_NOTDIR`。エントリは境界を検査し、途中で切れた列挙を OK にしない。コールバックの前に
  頁を手元へ写す (コールバックが FS に触ると受信器の中身が書き換わる)。LS に mtime は載せない (hsync は別に stat する)。
- `read_stream`: 要求より多く返さず、EOF の前に 0 を返さない。
- `write_stream` / `write_file` (push 用): 1 回目は `SFS_WF_TRUNC` で作り直す。
- `set_mtime` / `create_excl` は持たない (`OS32_ERR_NOSYS`)。
- 「無い」は `OS32_ERR_NOTFOUND`、UART の通信失敗は `OS32_ERR_IO`、LAN の無応答は `HOSTDOWN`。

## ホストの status (`sfs_status`)

0 以上はそのまま、既知の `OS32_ERR_*` だけを通し、それ以外 (契約違反の値) は `OS32_ERR_IO` に畳む。status は **int で読む**
(64 ビットのホスト試験で `i32` を経ると負の値が正に化ける)。契約違反の応答 (要求より多い READ・`/` を含む名前・進まない
cookie・知らない status) は IO。

## 確かめ

`make check-serialfs-host` (`tools/tests/test_serialfs.py`) の C の段 (VFS の契約と障害の注入)。
