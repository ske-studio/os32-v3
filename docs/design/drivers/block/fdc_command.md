# drivers/block/fdc_command.inc — 単発転送のコマンド送出

> 状態: **現行 (2026-11-01)**

`fdc_send_sector_command` は単発 READ/WRITE DATA の IRQ フラグを消し、
opcode、HD/drive、C/H/R/N、EOT、GPL、DTL を従来の順で送る。
EOT は R と同じセクタ番号とし、最初の送信失敗で負を返す。
DMA 設定、IRQ 待ち、リザルト判定、回復は呼出し元が行う。

`test_fdc_track.py` は両方向・両ジオメトリの列と、各バイトの送信失敗を検査する。
EOT の変更と GPL 失敗の無視は、コンパイル成功後の実行時 RED で検出する。
