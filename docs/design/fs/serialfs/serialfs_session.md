# sys/fs/serialfs/serialfs_session.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SERIAL_HOSTFS 部品 B (§1-v2 B-4'・B-6'、§1-v3、ユーザー決裁 1〜3) の契約を今のソースと照らして要約
ソース: `sys/fs/serialfs/serialfs_session.c` (`serialfs_session_begin`・`serialfs_session_end`)・PC-98 前景 `sys/fs/serialfs/sfs_session_fg.c` (`sfs_finish`)・ゲート `sys/dev/char/serial.c` (`serial_gate_set`・`serial_gate_put`・`serial_gate_get`・`serial_hold_take`・`serial_hold_dropped`)・`sfs run` `userland/shell/rshell.c`・`userland/shell/serial_watchdog.c` (`rsh_sfs_child`・`rsh_esc_classify`)

UART の SerialFS のセッションは常駐シェルの組込み **`sfs run <コマンド行>`** の 1 行の中でだけ開く (KAPI `sfs_begin` / `sfs_end`)。
`mount /host COM1 serialfs` の単独実行は断る (`userland/shell/cmd_mnt.c`)。自動マウントはしない。LAN の常駐のときの
scope は `docs/design/fs/serialfs/serialfs_persistent.md`。

PowerMac の USERSHELL 像は前景を登録しない。常駐 LAN が無効なら begin と end はともに `OS32_ERR_NOSYS` を返す。

## 流れ

- begin: `/host` が空いているか (HostDrv や既存のマウントがあれば断る) → ゲートを上げる → HELLO (ホストがセッション ID を返す)
  → `/host` に SerialFS をマウント。
- 子のコマンドを exec で走らせる。子がどう終わっても (通常・非ゼロ・fault kill) `sfs run` に戻る。ゲートの持ち主は `sfs run`
  だけで、exec_exit・fault kill はゲートに触らない。
- end: BYE → アンマウント (線が死んでいても手元は解放) → 隔離 (受信を捨てて `SFS_QUIET_MS` = 500ms 静まるのを待つ、上限
  `SFS_QUIET_MAX_MS` = 5s) → 溜めた出力と終了コードを長さ付きのフレーム (`SFS_T_LOG`・`SFS_T_EXIT`) で送る → ゲートを下ろす
  (受信リングも空にする) → rshell の行末 EOT。
- ログのフレームは**ゲートを下ろす前に**送る (下ろした後は console の複写が同じ線に出て、フレームの間に文字が挟まりうる)。
- 隔離の上限に達したら `sfs: line not quiet after Nms; closing the session anyway` を出して進む (保留の状態は設けない)。EXIT の
  flags に `SFS_XF_NOT_QUIET`、途中で線を死んだと見なしたら `SFS_XF_DEAD`。
- HELLO に答えが無ければセッションは始まっていない。フレームは送らず、隔離を済ませてからゲートを下ろし、溜めた文字を
  そのまま流す (相手が遅れて HELLO に答えた可能性を前提にする)。

## ゲート (`sys/dev/char/serial.c`)

- 送信: セッション中は SerialFS の専用の口 (`serial_gate_put`) だけが線に出す。ほかの送信 (console の複写、KAPI の
  `serial_putchar` / `serial_puts`、ISR の kprintf) は保留リング (`SER_HOLD_SIZE` = 8KB) に溜める。溢れた分は数えて
  `sfs: log dropped N bytes (oldest)` を出す。パニック・例外の polled 出力は通す。
- 受信: セッション中は KAPI の `serial_trygetchar` / `serial_getchar` と kbd の読みは -1 (持ち主は SerialFS の受信器だけ)。
  受信器は RxRDY を直接見て FIFO に残った末尾も汲み出す。
- セッション中の速度変更 (`serial_init`・`serial_init_vfast`) は断る (`[ser] refuse serial_init during SerialFS session`)。
- ゲートの上げ下げで受信リングと UART / FIFO の残りを読み捨てる。

## rshell の側

- `sfs run …` の行は rshell が**行全体を**引き受ける (`execute_command` に渡さない。組込みの `sfs run` として届いた行は常に断る)。
  `--serve-host` 無しの `sfs run` はホストの道具が送らない。
- シリアルから来た ESC で rshell を閉じるのは行の先頭の単独の ESC のときだけ (フレームの中の 0x1B で閉じない)。

## 確かめ

`make check-serialfs-host` (`test_serialfs.py (旧版 3080b693e474)`) の C の段 (`serialfs_session_host.c (旧版 99645a4a4966)`)・GATE の段
(`serial_gate_host.c` が実物の `sys/dev/char/serial.c` を偽のポートで回す)。セッション中に rshell へフレームが漏れない、
セッション外の本文にフレームがあっても答えない、を見る。NP21/W と実機は残る。
