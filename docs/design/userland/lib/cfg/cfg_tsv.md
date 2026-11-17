# userland/lib/cfg/cfg_tsv.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S2 の契約を今のソースと照らして要約
ソース: `userland/lib/cfg/cfg_tsv.c` (`cfg_tsv_parse`)

設定の初期値 tsv (`assets/settings/defaults.tsv`、ゲストでは `/etc/settings.tsv`) の reader。`cfg_init` が使う。ホストの生成ツール `tools/mk_settings_db.py` と **accept・reject の判定が一致する** ことが契約。

## 形

- 1 行 = `scope<TAB>key<TAB>type<TAB>value`。4 列ちょうど (足りない・5 列目があれば `CFG_TSV_E_COLS`)。
- 空行と `#` で始まる行は飛ばす。改行の無い最終行も読む。
- type は `int`・`text`・`blob`。
- scope・key は `libos32cfg.md` の値の規則 (`cfg_i_valid_scope`・`cfg_i_valid_key`)、63 byte まで。
- int: 符号の後の先頭ゼロを畳んで 10 桁まで数え、int32 の範囲だけ受ける。何千桁の `0` や `-0…0` も 0 として受ける。空は拒否。u32 の累算で桁あふれを先に見る (64 bit 整数を使わない)。
- text: UTF-8 で 255 byte まで。末尾の空欄は NULL ではなく空の text。
- blob: hex で 8192 文字 (4096 byte) まで。奇数桁・空白・非 hex を拒否。

## ファイル全体に当てる規則

- CR と不正な UTF-8 はコメントの中でも拒否する (生成ツールはファイル全体を先に検査するので、同じ判定にするため)。
- NUL はコメントの中では通し、scope・key・type・値の中では拒否する。
- 行長の上限は持たない (1 byte ずつ引き取る)。列ごとの上限だけで判定する。
- 重複した `(scope, key)` は reader では見ない。流し込み先の `settings` の PRIMARY KEY が弾く。突き合わせの単位は reader 単体ではなく `cfg_init` (その tsv で DB が作れるか)。

生成ツールとの意図した違いは、誤りが複数あるときにどれを報告するかだけ (ツールは CR → UTF-8 → 行の規則の順にファイル全体を見る、reader はストリームの順に最初の物)。

## 検査

`tools/tests/test_cfg.py` の `TSV_FIXTURES` (58 件) を同じ byte 列のまま `cfg_tsv_parse` (`cfg_init` 経由) と `tools/mk_settings_db.py` の両方に通し、accept・reject の一致を見る (`TSV PARITY`)。
