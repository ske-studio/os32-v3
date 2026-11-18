# tools/check_kapi_abi.py — KAPI は末尾追加だけ

> 状態: **現行 (2026-11-18)** — ハッシュで固定する試験を、基点との比較に替えた。
ソース: `tools/check_kapi_abi.py`・`tools/tests/test_kapi_abi.py`、検査 `kapi-abi`・`kapi-abi-host`

## 何を守るか

[ABI]: 配った後の `sdk/kapi.json` の口は、番号 (並び)・名前・戻り値・引数を変えない。足すのは末尾だけ。

## 比べ方

| 項目 | 決まり |
|---|---|
| 基点 | `origin/main` との merge-base。HEAD がその上に在る (main そのもの) 時は 1 つ前のコミット |
| 比べる欄 | 口の名前・戻り値・引数の列、データの欄の名前と型、口の表の容量 (`func_capacity`) |
| 比べない欄 | カーネルの側の関数の名前 (`target`)・本体 (`body`)・`in`/`out` の註・`includes`・`externs`・版 |
| 合格 | 基点の並びが、今の並びの先頭にそのまま在る (末尾への追加は自由) |
| 壊す時 | 基点から HEAD までのコミットの本文に `KAPI-Break: <理由>` の行を書く。理由の無い宣言は数えない |
| 履歴が無い | 終了コード 2 (環境の不足)。浅い clone では `git fetch origin main` |

## 固定の値を持たない

前は、定義の全体や先頭の N 口の sha256 を試験と fixture に固定していた (`dep_dir_a` の l01・l04・l05a、
`test_host_notice.py`、`test_ver_api.py`、生成物 4 本の sha256)。口を足すたびに値を手で直す必要があり、
ABI でない欄 (`target`) を直しても落ちた。守りたい事は「前の口が変わらない」なので、基点と直に比べる。
生成物が定義と合っているかは、CI が作り直して差分を見る (`check.yml` の static-check) と `kapi-out` が見る。

## 試験

`test_kapi_abi.py`: 偽の git の木で、欄の選び方・追加の合格・削除/改名/型の違い/並べ替え/データの欄/容量の
検出・宣言の読み方・枝と main の基点・履歴が無い時の終了コード。変異 12 本。
