# tools/tests/ide_pio_host.c — IDE ポート模型

> 状態: **現行 (2026-10-31)**

`test_ide_pio.py` は製品 `ide.c` と `ide_addr.c` をそのままコンパイルする。
`ide_hostshim/io.h` の宣言だけを include 順序で差し替え、
IDENTIFY の語で LBA28／現在 CHS／既定 CHS／SASI を設定する。
模型はコマンド数・番地・count・DRQ 相・語数を記録し、全転送語と canary を照合する。

- count=0,1,2,64,255,256,257、256 のレジスタ 0、ドライブ正規化。
- CHS のトラック・ヘッド・シリンダ境界、容量と LBA28 末尾、範囲外で番地発行 0。
- SASI の単コマンド列と FatFs の 2 刻み要求、単数／CHS 単数 API。
- 初相・中間相・最終相・終了時の BSY、DRQ 欠落、ERR、DF。
- 相間の古い DRQ と BSY+DRQ で転送しない。READ ERR+DRQ は読み捨て、WRITE は送らない。
- 途中失敗で後続チャンクと再試行を止め、次要求の BSY 待ち上限と idle 復帰後の内容を確認。

実行は `python3 -B tools/tests/test_ide_pio.py`。
`--mutate` は未変更コピーの成功を先に確認し、製品 inc の意味変異を組んで
実行時 assertion による RED を要求する。コンパイル失敗や signal は kill と数えない。
CHECK に製品・inc・shim・試験入力を宣言する。

基点の count=2 は 2 コマンドで RED、変更後は 1 コマンドで GREEN。
製品の手順は `ide_pio`（../../drivers/ata/ide_pio.md）。
ポート模型の成功はゲスト／実機受入を代替しない。
