# sys/fs/vfs.c — 更新日時の設定 (`vfs_set_mtime`、KAPI `sys_set_mtime`)

> 状態: **現行 (2026-11-18)** — 出所: TASK_H3 の契約を今のソースと照らして要約
ソース: `sys/fs/vfs.c` (`vfs_set_mtime`)・`sys/fs/vfs.h` (`VfsOps.set_mtime`)・`sys/fs/ext2/ext2_vfs.c` (`ext2_vfs_set_mtime`)・`sys/kapi/kapi_sysinfo.c` (`kapi_sys_set_mtime`)

`sys_set_mtime(path, mtime)` (KAPI v52、`sdk/kapi.json` の末尾に追加) はファイルの mtime (UNIX 秒、UTC) を設定する。
hsync がコピー元の日時を宛先へ写すために使う (同じ日時なら次の同期で読まずに省略できる)。

## 振り分け (`vfs_set_mtime`)

| 条件 | 戻り値 |
|---|---|
| パスが NULL か空 | `VFS_ERR_INVAL` |
| `mtime == 0` | `VFS_ERR_INVAL` (0 は「不明」の印。書かせると次の同期で証拠の無い状態を自分で作る) |
| パスの解決に失敗 | そのエラー |
| マウントが無い | `VFS_ERR_NOMOUNT` |
| 資源の準備が失敗 | そのエラー |
| ドライバが `set_mtime` を持たない | `VFS_ERR_NOSYS` |
| それ以外 | ドライバの戻り値をそのまま |

- `set_mtime` は `VfsOps` の**任意実装**。実装は ext2 だけで、HostDrv・FAT・ISO9660 は持たない (初期化子に明示の 0 を置く)。
  `VfsOps` はカーネル内部の構造体で ABI ではない。
- ext2 はマウントが書き込み禁止なら `VFS_ERR_ROFS`。inode を読み、mtime (と ctime = 現在時刻) を書き換えて書き戻し、同期する。
  読めない・書けないはそのエラーで、パスの解決の失敗を `NOTFOUND` に畳まない (メタデータの I/O 失敗はマウントを書き込み禁止にする)。
- KAPI の入口 (`kapi_sys_set_mtime`) はパスが NULL・`mtime == 0` を `OS32_ERR_INVAL` で返し、CPL=3 から渡されたパスを検証して写してから呼ぶ。
- 呼び手は**データを書き終えてから**呼ぶ (書き込みは mtime を現在時刻で上書きする)。`NOSYS` は「この FS には無い」で、失敗とは別に扱う。
