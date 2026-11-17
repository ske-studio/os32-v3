# tools/np21w_trial.py — 設計 (ディスクの試行)

> 状態: **現行 (2026-11-18)** — 出所: TASK_S3I2 (S3I2-T §2a) の契約を今のソースと照らして要約
ソース: `tools/np21w_trial.py` (`make_plan`・`_plan`・`launch_command`・`identity_mismatch`・`identity_unstable`)

使い捨ての NHD と FDD で NP21/W を起動する承認済みの 1 回の試行 (`action='disk-trial'`)。ini の欄の約束は `docs/design/tools/np21w_ini.md`。

## 計画

- `make_plan` は `hdd` (`.nhd` の名前)・`fdd_eject` (明示の真偽)・`fdd_arg` (任意の `.d88` の名前) を受け、`resolve_image` で解決した Windows のパスとホストのパスを計画に **束縛** する (`hdd_path`・`hdd_host`・`fdd_arg`・`fdd_arg_host`)。環境を見るのはここだけで、検証 (`_validate_plan`) と実行は環境を見ない。承認した後に `NP21W_DIR` を変えても別のファイルで起動しない。
- 束縛の検査: 名前・ホストのパス・Windows のパスの末尾が一致し、ホスト側が通常ファイル (symlink でない)。`.d88` は `.nhd` と同じディレクトリ。
- 変更の集合は `HDD1FILE` (解決済みのパス)・`e_resume=false`、`fdd_eject` なら `FDD1FILE`・`FDD2FILE` を空に、だけ。Cirrus などほかの欄には触れない。
- 起動のコマンドは `"<exe>" "/i<試行の ini>"` (+ ` "<d88>"`)。各引数を二重引用符で囲み、区切りは空白 1 つ。Win32_Process の CommandLine とバイト単位で突き合わせる。

## 起動の確認と後片付け

- 起動した行の照合 (`identity_mismatch`): `created` は古い行と違うこと、`exe` は大小文字を無視して承認した exe と同じ、`command` は `launch_command` と一致。食い違った項目名を理由に付ける。2 回の照会で行が揺れたら `identity unstable: <項目>`。
- PowerShell 側は `UseShellExecute = $true` で起動し、標準入出力を一切リダイレクトしない (起動した NP21/W が PowerShell の stdout のパイプを継承しない)。同一性の要は `$p.Id`、生存の確認は `HasExited`。`StartTime` は読めたときだけ 2 秒の許容で照らす。`CheckFile` で `hdd_path`・`fdd_arg` がディレクトリでないことを見る。
- 理由は固定の語彙 (`REASON_CODE`) に合う文字列だけを受ける。起動の後の失敗では、起動した PID (`started_pid`、行が取れれば `process`) を必ず結果に残す。
- 後片付けで reader が EOF に届かないのは失敗のまま (`cleanup: inherited pipe still open`)。`wait` の時間切れは `cleanup: executor exit timeout`。自動の停止・復旧・再試行はしない。

## 検査

`tools-host` の中の `tools/tests/test_np21w_trial.py`。贋の transport と一時ディレクトリで組み、PowerShell の部分は生成されるコードの文字列だけを見る (Windows の上での動きは実走でしか分からない)。
