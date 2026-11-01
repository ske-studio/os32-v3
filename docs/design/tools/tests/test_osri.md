# tools/tests/test_osri.py

生成模様から固定画像を作り、CRC とプレビューを照合する。実 loader を 32 bit の VFS スタブに結合し、短い読み、破損、長さ、イベント、プレーン配置の拒否を調べる。`--mutate` は CRC・予約領域・末尾検査を個別に外し実行時 RED を確認する。
