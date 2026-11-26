# sys/fs/serialfs/sfs_block.c — SFS の上の読み取り専用のブロックの RPC (BOPEN・BREAD・BCLOSE)

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `sys/fs/serialfs/sfs_block.c`・`sys/fs/serialfs/sfs_block.h`・`sys/fs/serialfs/sfs_proto.h` (`SFS_T_BOPEN` ほか、`SFS_BOPEN_*_OFF`・`SFS_BREAD_*`)・ホストは `tools/host_image.py` ([tools/lanfs_host](../../tools/lanfs_host.md))

ホストが別名 (alias) で公開した像を、512B のセクタ単位で読む。利用者は netcd だけ ([drivers/block/netcd](../../drivers/block/netcd.md))。
このファイルは状態を持たない (lan.mod に置く)。セッションは常駐の client ([fs/serialfs/serialfs_persistent](serialfs_persistent.md)) のもの。

## 通信の形式 (SFS の既存の sid/seq/CRC の上、全部 LE、構造体で読まない)

```text
BOPEN 0x30: alias UTF-8 (要求の残り全体、NUL なし、1..63B)
  応答: status i32, handle u32, size_lo u32, size_hi u32, mtime_ns_lo u32, mtime_ns_hi u32 (24B)
BREAD 0x31: handle u32, lba512 u32, count u16 (1..2)
  応答: status i32, data[count*512] (成功の時だけ、長さの一致が必須)
BCLOSE 0x32: handle u32 → status i32
応答の種別は op|0x80。誤りの応答は status だけ (OS32_ERR_*)。
```

- 2 セクタ + status (1028B) が LAN の上限 `SFS_LAN_MAX_PAYLOAD` に収まる。シリアルの上限 512B は変えないので、この RPC は LAN 専用。ISO の 2048B の読みは呼び手が 2 往復に割る。
- 新しい op を足す時は 0x30〜0x32 との衝突を確かめる。NOTIFY は SFS に足さない (link の側)。

## 不変条件

- handle はその sid の中だけ有効。`SfsBlock.sid` が今の client の sid と違えば、古い handle を送らずに `STALE`。張り直した後は BOPEN からやり直す (再取得が要る遠隔の handle はこれだけ。/host のファイルはパス + offset で要求し、遠隔の handle を持たない)。
- 像の同一性は **size u64 + mtime_ns u64**。先頭のハッシュは取らない (ISO の先頭は 0 が続き、交換の検出に弱い)。わざと同じ size と mtime に戻した変更は検出しない。
- size は 512 の倍数で、セクタ数が u32 に収まること。媒体は読み取り専用。
- ホストは BREAD の前後で fstat を比べ、違えば `STALE`。範囲外・掛け算の溢れは `INVAL`、EOF での短い読みは `IO`。
- 期限は呼び手 (netcd) が操作ごとに 1 つ決めて、ensure・BOPEN・BREAD に同じ値を渡す。ホストの `SFS_T_ERR` は `STALE` で、常駐の世代も進める (`sfs_persistent_stale`)。

## 確かめ

- `test_hostsvc_block_contract.py (旧版 4810acf9d233)` (wire の offset と長さ、libc なし ILP32)・`test_hostsvc_block.py`・`test_hostsvc_image.py` (ホストの 2048B の一致・範囲外の拒否・像の差し替えで STALE)。
