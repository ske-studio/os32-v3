# fs/isofs/iso9660_media.c — ISO の媒体の境目 (Device ごとの世代・FD の失効・載せ直し)

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `fs/isofs/iso9660_media.c`・`fs/isofs/iso9660_media.h`・`fs/isofs/iso9660.c`・`fs/vfs.h` (`resource_check`・`resource_refresh`・`vfs_fd_stale_mount`・`vfs_resource_stale_mount`)・`fs/vfs_fd.c`・`kernel/dev/dev.h` (`media_check`・`blk_read_u32`・`dev_media_check`)

ISO の読みとキャッシュの寿命そのものは [iso9660](iso9660.md)。ここは媒体が替わった・相手が落ちた時の扱い。

## 口

- Device の末尾の任意の欄: `blk_read_u32` (u32 の LBA、あれば旧 `blk_read` より優先。旧い口へは範囲が int に収まる時だけ渡し、それ以外は `INVAL`) と `media_check(dev, &generation)` (NULL は交換しない媒体で世代 0)。どちらも task の中だけ。
- VfsOps の末尾の任意の hook: `resource_check(ctx, &generation)`・`resource_refresh(ctx)`。ISO は `iso9660_resource_check`・`iso9660_resource_refresh`、/host は `serialfs.c` の `sfs_resource_check`・`sfs_resource_refresh` (常駐の世代、[serialfs_persistent](../serialfs/serialfs_persistent.md))。
- VFS は全部のパスの操作の前と、FD の read・write・fstat・seek の前に check を呼ぶ。変化を見つけたその操作は `STALE`、次の操作で refresh。close は通信せずに解放する。

## 不変条件

- 媒体の世代は Device ごと (ATAPI 全体の世代を見ない)。
- netcd などの交換できる媒体で世代が変わったら、`vfs_fd_stale_mount` で既存の FD に永久の失効の印を付け、パス・LRU・先読み・PVD を全部捨てる。世代が巡っても印は戻さない。FD は失効の後に旧い内部のポインタを見ない。
- ATAPI (`DEV_BUS_ATAPI`) だけは別: 世代は reset や UNIT ATTENTION でも進むので、キャッシュを捨てて同じ操作の中で 1 回読み直し、FD は失効させない (従来の動作)。FD から見た世代は 0 に固定。
- refresh は mount と ctx の外枠を保ち、中身 (PVD とルート) だけを作り直す (umount・mount し直さない)。PVD を読む前後で媒体の世代が違えば `STALE`。外枠の解放は本当の umount の時だけ。
- `HOSTDOWN`・`STALE` は `IO` に潰さずそのまま返す (ATAPI の私的な負値 -1〜-4 だけ `IO` に写す)。失敗した読みを成功として返さない。媒体が無い時に古いキャッシュを成功として返さない。

## 確かめ

- `tools/tests/test_hostsvc_media.py` (`hostsvc_media_host.c`: キャッシュの hit の直前の像の差し替えで旧い PVD・ディレクトリを返さず載せ直す、ATAPI の回帰)。
