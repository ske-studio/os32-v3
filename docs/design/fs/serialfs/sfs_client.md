# sys/fs/serialfs/sfs_client.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SERIAL_HOSTFS 部品 B (§1-v2 B-5'・B-7'、§1-v3) の契約を今のソースと照らして要約
ソース: `sys/fs/serialfs/sfs_client.c`・`sys/fs/serialfs/sfs_client.h` (`sfs_client_call`・`sfs_client_hello`・`sfs_client_quiesce`)・形式 `sys/fs/serialfs/sfs_proto.h`・`sys/fs/serialfs/sfs_proto.c` (フレームの組み立て・受信器・期限の式)・ホスト `tools/serialfs_host.py` (`serve_line`)・`tools/rshell_serial.py --serve-host`

SerialFS (シリアル越しの `/host`) の要求 / 応答。この文書は UART の経路を扱う。LAN の常駐の期限と再送は
`docs/design/fs/serialfs/serialfs_persistent.md`。

## フレーム (両向き同じ形、リトルエンディアン)

`ENQ (0x05)` `'S' 'F'` + 種別 (u8、応答は要求 | 0x80、拒否は `SFS_T_ERR`) + セッション ID (u32、HELLO の要求だけ 0) +
番号 (u16) + ペイロード長 (u16、0〜`SFS_MAX_PAYLOAD` = 512) + ペイロード + CRC32 (種別からペイロードの末尾まで、zlib.crc32)。
正典は `sfs_proto.h` の頭の表で、ホストの写し `tools/serialfs_host.py` と**同時に**変える。

- 応答のペイロードは必ず status (i32、`OS32_ERR_*`) で始まる。READ の count の上限 = 512 - 4。
- パスは「長さ u8 + バイト列 (NUL なし)」の `/` 始まりのルート相対、最大 255。
- 受信器は長さを確保・コピーの前に上限と照合し、フレーム全体 (長さ 0・誤りの応答も) を消費して検証する。

## 要求ごとの規則

- 1 要求 = 1 応答。番号は 1〜`SFS_SEQ_LAST` (0xFFFE) を使い**周回させない**。越える要求は IO で失敗させ、線を死んだと見なす。
- 番号違い・種別違い・セッション違い・CRC 違いのフレームは**数えずに捨て**、同じ期限まで待つ (期限を延ばさない — 流れ続けても抜ける)。
- 期限 (10ms tick): 最初の 1 バイトまで `SFS_FIRST_BYTE_MS` (2s)、バイト間 `SFS_GAP_MS` (100ms)、全体 = 2s + バイト数 ×
  1 バイトの時間 (今の実効速度、8N1 = 10 ビット) × 2 + `SFS_SLACK_MS` (500ms)。
- 期限切れは同じ番号で再送 (最大 `SFS_RETRIES` = 3、試行は 4 回)。再送の前に線が `SFS_RESYNC_QUIET_MS` (200ms) 静まるまで
  読み捨てる (上限 1s)。
- 要求が `SFS_DEAD_AFTER` (3) 回続けて失敗したら線を死んだと見なし、以後アンマウントまで即 `OS32_ERR_IO`。`SFS_T_ERR`
  (ホストが知らないセッション = ホストの再起動) も死んだと見なす。
- IF=0 (待てない、`can_wait` が 0) で呼ばれたら要求を出さずに `OS32_ERR_IO`。

## ホスト側 (`tools/serialfs_host.py`)

- `sfs run` の行を送ってから、その行の EOT までの間だけフレームを解釈する (セッション外の `cat` の本文で要求が動かない)。
  `ENQ 'S' 'F'` で始まり長さと CRC が合うもの = フレーム、フレームの外の EOT = 行の終わり、それ以外 = rshell の文字。
- 応答のキャッシュは直前の 1 件。一致条件は (セッション ID, 番号, 要求フレームの CRC32)。同じ要求の再送には操作を
  再実行せず保存した応答を返す (RENAME・UNLINK・MKDIR・WRITE を二重に実行しない)。HELLO で消える。
- 要求を受けてからゲストの 1 試行の期限を過ぎた応答は送らない。BYE の後・行末 EOT の後は何も送らない。
- 未知のセッション ID の要求には `SFS_T_ERR` (`OS32_ERR_STALE`) で答える。
- パスの `..`・`.`・`\`・NUL の要素は断り、realpath で根の外 (symlink 経由も) を断る。POSIX では起動時に開いた根から
  `O_NOFOLLOW` で辿る。ホストへの書き込みは既定で禁止 (`--allow-write`)。

## 確かめ

`make check-serialfs-host` (`tools/tests/test_serialfs.py`): C の段 (`tools/tests/serialfs_host.c` が実物の `sfs_proto.c`・`sfs_client.c`・
`serialfs.c` を偽の線と偽のホストで回す)、PY の段 (実物の `serialfs_host.py`・`rshell_serial.py` を偽のポートで)、XC の段 (C と
Python が互いの列を読む、定数の照合、実時間のパイプで結合)。障害の注入: 応答の喪失 (1〜3 回は通る / 4 回で IO / 3 要求続けて死ぬ)、
遅延・CRC 破損・番号違い・セッション違い・ホストの停止・ごみの連続・ERR・契約違反の応答、再送で副作用が二重にならない。
