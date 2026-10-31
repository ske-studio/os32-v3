# tools/tests/ide_pio_host_multiple.inc — MULTIPLE の受入模型

> 状態: **現行 (2026-10-31)**

`ide_pio_host.c` のポート模型を使い、製品コードを直接呼ぶ。
word47=0/1/2/16/128 と count=0/1/m±1/64/255/256/257 を組み合わせ、
命令数・ceil(n/m) の相数・全256n語・データ・canary を照合する。
模型は相内の status 読取りも拒否するので、セクタごとの settle は通らない。

相頭・中間・端数・最終完了の BSY/DRQ/ERR/DF を注入し、
READ ERR+DRQ の相全体の読捨て、WRITE の送信停止・部分変更、
後続チャンクなし・再試行なしを検査する。
CHS のトラック分割と SASI の従来コマンド列も検査する。
4台別能力・交互要求、word59 の一致/不一致/無効、IDENTIFY 失敗、
C6h の拒否/BSY/DRQ残留、bank1 reset と再initも検査する。
入口は [test_ide_pio.py](test_ide_pio.md)。

## 落とし穴

模型側の相サイズは製品の状態を読まず、C6h の完了から決める。
ホストの成功は、実機の m と時間への寄与を保証しない。
