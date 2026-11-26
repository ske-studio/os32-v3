# tools/mk_blank_nhd.py — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S3I2 (S3I2-T) の契約を今のソースと照らして要約
ソース: `tools/mk_blank_nhd.py`

新規インストールの試験に使う、全部ゼロの使い捨て NHD (NP21/W の T98-Next HDD 像) を作る。

## 形

- ヘッダは 512 byte の `NHDHDR`: 署名 `T98HDDIMAGE.R0` (NUL 込み 15 byte を NP21/W が比べる)、offset 0x110 から little endian で headersize = 512・シリンダ数 C (4 byte)・ヘッド 8 (2 byte)・セクタ 17 (2 byte)・セクタ長 512 (2 byte)。他のバイトは 0。
- 幾何は H = 8、S = 17、セクタ長 512 で固定 (`tools/nhd_deploy.py` の定数と同じ値)。`C = floor(size_mb × 1MiB / (8 × 17 × 512))`。NP21/W は C = 0 と 65536 以上を拒否するので 1 ≤ C ≤ 65535 だけ受ける。
- ファイル長は 512 + C × 8 × 17 × 512。NP21/W は C × H × S を全セクタ数として読むので、実ファイル長と一致させる。本体は `ftruncate` で伸ばす (疎ファイル、読めば 0)。

## 使い方と失敗

- `--out <path>` (必須)・`--size-mb <n>` (既定 200)・`--force`。成功は `C=<n> capacity=<bytes>` で終了 0 (200MB なら C = 3011)。
- 既にある宛先は `--force` が無ければ上書きしない (`O_EXCL`)。symlink は `O_NOFOLLOW` で拒否する。範囲外の大きさ・既存・書けない宛先は `error: …` で終了 2。
- 書き込みの途中の失敗では、書きかけの像を調べるために残す。

## 検査

`tools-host` の中の `test_mk_blank_nhd.py (旧版 702d850df2a8)`: ヘッダのバイト列、C の計算 (1・100・200・4351 と範囲外)、本体の長さ、`--force`、symlink の拒否、`nhd_deploy.py` の定数との一致。
