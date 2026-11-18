# userland/shell/serial_watchdog.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SERIAL_VFAST §4 (番犬を明示の `serial ack` に置き換えた設計) の契約を今のソースと照らして要約
ソース: `userland/shell/serial_watchdog.c`・`userland/shell/serial_watchdog.h` (`serial_watchdog_arm`・`serial_watchdog_ack`・`serial_watchdog_poll`・`serial_watchdog_leave`・`serial_watchdog_decide`)・配線 `userland/shell/rshell.c`・ホスト `tools/rshell_serial.py --fast N`

速度を上げた後、ホストと足並みが揃ったかはゲストには分からない (013Ah が効かない機種・ケーブル・ホストの開き直しの失敗)。
確認が取れなければ**ゲストが自力で元の設定へ戻す**。依存を持たない純粋な状態機械で、ホスト試験は .c をそのまま組む。

## 規則

- **arm**: rshell 経由の `serial N` が速度を変えた瞬間 (EOT の前後も成否も問わない)。戻し先のモードと速度を覚える。
  ローカル CUI で打った `serial N` は arm しない (戻す相手が居ない)。
- **解除**: 新速度で `serial ack` という行を受けて実行したときだけ (`serial_watchdog_ack`)。受信バイト数や往復した行数では
  解除しない。`serial ack` は arm されていなくても同じ応答 `ACK <baud> <mode>` を 1 行返す (冪等)。
- **戻す**: 期限 `SER_SWITCH_WATCHDOG_TICKS` (500 tick = 5 秒) まで ack が無いとき (`[ser] no ack after switch: reverted to Nbps`)、
  または arm 中に rshell を抜けるとき (`serial_watchdog_leave`、即座に REVERT)。
- 判定 (`serial_watchdog_decide`) は **ack が先**: 期限を過ぎていても ack を受けていれば LINKED。`poll` が LINKED か REVERT を
  返したら番犬は下ろされ、2 度は返らない。

## ホストの道具 (`tools/rshell_serial.py --fast N`)

9600 で `serial N` を送り、切替応答をエコーで識別してから N で繋ぎ直し、`serial ack` を 0.5 秒ごとに 4 秒まで繰り返す。応答本文に
`ACK` が見えたら同期。終わる前に `serial <--baud>` で元の速度へ戻す (`--keep-fast` で残せる)。

## 同じファイルの小道具

`rsh_esc_classify` (シリアルの ESC は行頭の単独のときだけ rshell を閉じる。本体の ESC は常に閉じる)、`rsh_sfs_child`
(`sfs run <行>` の子の行を取り出す、`docs/design/fs/serialfs/serialfs_session.md`)、行の読み (`rsh_line_*`)。

## 確かめ

`make check-serial-vfast-host` (`tools/tests/test_serial_vfast.py` のケース watchdog)。rshell_serial の側は同じ試験の Python の段。
