# tools/np21w_ini.py — 設計 (パスの欄)

> 状態: **現行 (2026-11-18)** — 出所: TASK_S3I2 (S3I2-T §2a) の契約を今のソースと照らして要約
ソース: `tools/np21w_ini.py` (`ALLOWED_PATHS`・`resolve_image`・`image_name`・`windows_path`・`transform`)

NP21/W の ini の書き換えのうち、ディスクの装着を変える欄の約束。固定値の欄 (`ALLOWED`) とは別の表 `ALLOWED_PATHS` で扱う。

## 欄

| 欄 | 受ける値 |
|---|---|
| `HDD1FILE` | `NP21W_DIR` 直下の `.nhd` の **名前** (`[A-Za-z0-9_.-]+`、`.` 始まりや `..` を含む名前は不可) |
| `FDD1FILE`・`FDD2FILE` | 空だけ (= 装着を外す)。変更前の値は非空の CP932 のパスでも中を検査せず空へ差し替える |

- `transform` の「変更する欄があれば全部の欄が ini に在ること」は `ALLOWED` と `ALLOWED_PATHS` で別々に掛ける (固定値の欄だけの操作がパスの欄の有無に依らない)。

## 名前の解決 (`resolve_image`)

- 環境を読むのはここだけ。`NP21W_DIR` は環境変数だけから読み、`.env` は読まない。WSL の絶対パスであること。
- `wslpath -w` で Windows の表記に直し、`wslpath -u` で戻して同じディレクトリになることを確かめる。`wslpath` の出力は終端の改行だけを取り、空白は残す。drive の絶対パスでなければ (UNC など) 拒否。
- 名前の先がホストで **通常ファイル** (symlink でもディレクトリでもない) であること。
- 書く値は解決済みの Windows の絶対パス。`windows_path` は ASCII・drive の絶対・240 未満・`"<>|?*:` と制御文字と `;`・`#` を含まない (ini の注釈と区別できないので書く側でも拒否する)・空や `.`・`..`・末尾が空白やドットの成分を含まない・予約デバイス名を含まない、だけを通す。自分が書いた ini を同じ変更でもう一度変換しても変わらない。
- CLI の `--set HDD1FILE=` は名前だけを受け、必ず `resolve_image` を通す。絶対パスは `unsupported image name` で拒否する (存在・通常ファイル・`NP21W_DIR` の中の検査を迂回させない)。`transform` は解決済みのパスだけを扱う純粋な関数。

## 検査

`tools-host` の中の `tools/tests/test_np21w_ini.py` (`PathFields` ほか)。贋の `wslpath` と一時ディレクトリだけを使い、実 ini・実プロセスには触らない。
