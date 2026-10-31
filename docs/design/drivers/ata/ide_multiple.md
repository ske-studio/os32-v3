# drivers/ata/ide_multiple.inc — ドライブ別 MULTIPLE 設定

> 状態: **現行 (2026-10-31)**

通常の 512B ATA の IDENTIFY word 47 下位8bitを最大相サイズとして保持する。
4台それぞれの max/active/pending は各1byte、常駐領域は計12B。IdeInfo/KAPI は変えない。
max が 0/1、SASI なら従来の READ/WRITE SECTOR(S) を使う。
init は全状態を消し、検出成功時に最大広告値で SET MULTIPLE MODE (C6h) を行う。
成功した非データ完了 (BSY/DRQ/ERR/DF=0) だけで active にする。

C6h の拒否は active/pending を消し、idle なら従来コマンドへ戻す。
BSY/DRQ が残る間は次命令を出さず失敗する。通常要求で設定を再試行しない。
公開 identify は設定命令を出さない。成功時だけ word 59 bit8 と現在値を照合し、
不一致なら active を落とし pending にする。読取り失敗では状態を保つ。
SRST 発行前に対象バンク2台の active を落とし、次の HDD 要求で設定し直す。
ATAPI DEVICE RESET はこの通知を行わない。

## 資料

[T13 ATA/ATAPI-6 rev.3a](https://pdos.csail.mit.edu/6.828/2005/readings/hardware/ATA-d1410r3a.pdf)
§8.15.22/28、§8.30.8、§8.49、§8.60 を照合。
BSD 手本は [NetBSD wd.c rev.1.476](https://github.com/NetBSD/src/blob/trunk/sys/dev/ata/wd.c#L355)
355–359 (ドライブ別能力)、[ata_wdc.c rev.1.121](https://github.com/NetBSD/src/blob/trunk/sys/dev/ata/ata_wdc.c#L269)
269–281、488–525 (設定・相・端数)。BSD 2条項のライセンスを確認し、処理を照合した。コードは移植していない。
PC-98 の DATA 16bit幅・代替 status はローカル資料
`docs/hw/undocumented/io_ide.md:137,356` と既存選択手順を継承する。

## 落とし穴

word 59 の現在値だけで active にしない。設定成功を確認してから C4h/C5h を出す。
C6h 失敗時の残留 DRQ を、次のデータコマンドで上書きしてはいけない。
