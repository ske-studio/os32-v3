# userland/lib/cfg/cfg_json.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S3 (S3-C) の契約を今のソースと照らして要約
ソース: `userland/lib/cfg/cfg_json.c` (`cfg_json_header`・`cfg_json_record`・`cfg_json_check`・`cfg_json_scope_usable`)・`userland/lib/cfg/cfg_internal.h` (`CfgJsonRow`・`CFG_JSON_*`)

`cfg import` が読む最小の JSON reader。純関数。**`cfg export` の writer が書く形だけ** を読み、汎用 JSON は持ち込まない。使い手は `cfg_import.c` (`docs/design/userland/lib/cfg/cfg_import.md`)。

## 形

```
{"schema_version":N,"exported":"..."}                                   1 行目 (ヘッダ)
{"scope":"…","key":"…","type":0|1|2,"v":<int>|"<text>"|"<base64>"|null}  以降 1 行 1 レコード
```

- キーの順は固定、空白は許さない、余分なキー・閉じ忘れ・二重の `}` は `CFG_JSON_E_SYNTAX`。ヘッダの数値の位置の空白は `E_VALUE` (どちらでもヘッダとしては拒否)。
- ヘッダの版: 0 以下は `E_VALUE`、`CFG_SCHEMA_VERSION` (1) より大きければ `E_VERSION` で、その値を返す。
- 行は長さ付きで渡す (埋め込み NUL も行の一部)。行バッファは `CFG_JSON_LINE_MAX` (6144 byte)。writer の最長行は text 255 byte が全部 `\u0001` の行 1,693 byte、blob 4096 byte の行 5,627 byte (scope・key が 63 byte のとき、LF・NUL を含まない)。

## 値

- `type` は `0`・`1`・`2` の 1 桁だけ (`"0"`・`3`・2 桁は拒否)。型は値の形を決めるので構文として扱う。`v:null` は宣言型を保つ。
- int: int32 の正準形だけ。`+1`・`01`・`-0`・空は拒否。範囲外は数字を読み切ってから `val_range` の傷にする (`INT_MIN` で符号あふれを踏まない)。
- text のエスケープは writer が出す 6 種だけ: `\"`・`\\`・`\n`・`\r`・`\t`・`\uXXXX`。`\u` は BMP だけで、サロゲートは `E_UTF8`、`\u0000` は `E_NUL`、非 hex は拒否。`\b`・`\/` は拒否。生の制御文字・不正な UTF-8・途中で切れた UTF-8 は拒否。生の 4 byte UTF-8 は通す。
- blob は base64。4 の倍数でない・詰め (`=`) の位置や個数の誤り・詰めの後の本体・エスケープの混入は構文として拒否。詰めで捨てるビットが 0 でない非正準形 (`AB==`・`AAB=`) は `val_b64` の傷にする。空の blob と `00`・`FF` を含む blob は通る。UTF-8・NUL の規則は文字列にだけ掛け、復号した blob には掛けない。

## 構文と意味の分離

- `cfg_json_record` は **構文だけ** を見る (骨組み・キーの順・エスケープ・base64 の形・数字の並び)。上限・値域・名前の規則・base64 の非正準は `CfgJsonRow` の傷 (`scope_over`・`scope_nul`・`key_over`・`key_nul`・`val_over`・`val_nul`・`val_range`・`val_b64`) に控えるだけで拒否しない。
- `cfg_json_check` が **対象行にだけ** 掛ける意味の検証: scope・key の規則と 63 byte、UTF-8、NUL、text 255 byte・blob 4096 byte、int32 の範囲、base64 の正準形。
- `cfg_json_scope_usable` は scope を丸ごと復号できたか。切り詰めた scope を対象名と比べない。
- だから `--scope` の対象外の行は、構文が正しければ値が上限を超えていても非正準の base64 でも import を落とさない。先頭ゼロの数字のような構文の誤りは対象外の行でも拒否する。

## 検査

`cfg-host` (`tools/tests/test_cfg.py`、本体 `tools/tests/cfg_host.c`) の `s3_json` (reader 単体の受理と拒否、最長行、エスケープ、base64、int32 の境界) と `s3_scope` (対象外の行の非正準 base64・4096 byte 超の blob は通り、先頭ゼロは落ちる)。
