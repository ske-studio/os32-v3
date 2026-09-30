# NP21/W live ini: 新規ホスト TDD 記録 (2026-09-08)

変更対象は新規 `tools/np21w_ini_live.py`、新規
`tools/tests/test_np21w_ini_live.py`、この記録、設定スキルの4ファイルのみ。
既存 `np21w_ini.py` と `np21w_ini_tdd.md` は変更していない。
既存20件の過去の追加テストについて、当時存在しなかった per-test RED を補記・捏造しない。

## 今回実際に実行した RED/GREEN

以下の `L` は実行した共通コマンド:

```bash
python3 -B -m unittest discover -s tools/tests -p test_np21w_ini_live.py -v
```

| 段階 | コマンド・結果 | 実装前の assertion と追加実装 |
|---|---|---|
| A RED | L: 17 tests, failures=41 | no-op skeleton に対して Identity 4件、Workflow 13件の全メソッドが assertion failure。例: bytes未変更、diffがNone、例外が発生しない。import error/skip は無し |
| B RED | L: 17 tests, failures=3 | 制御実装後、`test_receipt_tampering_is_rejected` の target/original/operation 改変がすべて `IniError not raised`。最初のAではレシート不存在のassertionで止まったため、改変3ケースのREDをここで改めて確認 |
| C GREEN | L: 17 tests, OK | レシートの対象・純粋変換再計算・原本・差分・起動情報を検証 |
| D RED | L: 23 tests, failures=37 | WindowsExecutor/main のno-opと空PS_SERVERに対し WindowsContract 6件。空配列、失敗応答、バイト転送、CLI差分、PS契約のassertion RED |
| E GREEN | L: 23 tests, OK | 固定PSプログラム、JSON/base64チャネル、Windows executor、CLIを追加 |
| F RED | L: 27 tests, failures=6 | RecoveryAndBoundary 4件。失敗診断にIDなし、束縛入口なし、予約デバイス名3ケース未拒否、native構造体Pack=4なし |
| G RED | L: 27 tests, failures=2 | 診断ID追加後、停止状態からの復元が実際に拒否されるassertion RED。Pack=4修正後、新規ファイルのアクセス権指定がないassertion RED |
| H RED | L: 28 tests, failures=3 | Gの2件に加え、`test_mutating_model_binding_is_single_use` が2回目を拒否しないassertion RED |
| I GREEN | L: 28 tests, OK | 成功した不在照会+記録済みprovenanceで復元、所有者専用FileSecurity、単回承認を実装 |
| J RED | L: 30 tests, failures=1 | `test_bound_apply_requires_exact_operator_approved_request`: 操作者が具体的requestを束縛していないのに適用できるassertion RED。もう1件は既存再照会ガードの回帰例で初回からGREEN（下記） |
| K GREEN | L: 30 tests, OK | `approved_request` の完全一致が必要。承認済み試行は1回のみ |
| L RED | L: 31 tests, failures=1 | `test_nonzero_powershell_exit_overrides_success_frame`: 偽チャネルが成功JSONとexit3を返しても拒否しないassertion RED |
| M GREEN | 全体コマンド: 51 tests, OK | transportで異常終了を確認。旧20+新31、skipなし |

D以後のRED出力は作業中 `/tmp/np21w-live-red2.log` 〜 `red7.log` に保存して確認した。
リポジトリに残す証拠はこの件数・対応表とテスト本体。REDコマンドはFAILED、
ログ末尾を表示したシェル全体の終了値は `tail` の0になる場合があるため、
その0をテスト成功として扱っていない。

## テスト名と挙動の対応

`test_` 接頭辞を省略。括弧内は実際のRED段階。

| クラス | メソッド | 検証する挙動 |
|---|---|---|
| Identity | exact_identity (A) | PID、exe、コマンド行、明示iniの一致 |
| Identity | absence_is_not_query_failure (A) | 成功空配列と不正/欠落照会を区別 |
| Identity | reject_ambiguous_commandlines (A) | 暗黙、相対、追加引数、未対応-i形を拒否 |
| Identity | wrong_pid_exe_config_or_multiple (A) | 別PID、別exe/ini、複数プロセス拒否 |
| Workflow | dry_run_exact_diff_without_mutations (A) | 既定読取のみ、限定差分、原本保持 |
| Workflow | bounded_operation_and_operator_gate (A) | 自由操作と専有前提なしを接触前に拒否 |
| Workflow | exclusive_lock_before_any_contact (A) | lock失敗なら照会/保存なし |
| Workflow | apply_order_bytes_backup_receipt_restart (A) | 停止→不在→backup→replace→readback/receipt→同一対象start |
| Workflow | queries_fail_closed_at_every_phase (A) | 通常適用の8照会の各地点で失敗注入、後続を拒否 |
| Workflow | stop_requires_exit_not_acknowledgement (A) | stopの成功応答だけで保存しない |
| Workflow | pid_reuse_before_stop (A) | 同じPIDでも生成時刻が変わればstopしない |
| Workflow | stop_save_or_external_change_aborts (A) | 停止中の識別情報変更を拒否 |
| Workflow | backup_replace_readback_failures_never_restart (A) | backup/replace/receiptエラー、読み戻し不一致でstartなし |
| Workflow | restart_requires_same_identity_and_observed_pid (A) | 戻り値PIDをCIM観測で確認、失敗時に自動復元しない |
| Workflow | restore_dry_run_and_apply (A) | 逆差分プレビューと全バイト復元 |
| Workflow | restore_rejects_intervening_identity_or_bytes (A) | 内容/識別情報の途中変更を停止前に拒否 |
| Workflow | receipt_tampering_is_rejected (A,B) | 実際のレシート改変3ケースのREDはB |
| WindowsContract | successful_empty_query_and_transport_cleanup (D) | 成功空配列、対象束縛、チャネルclose |
| WindowsContract | query_error_malformed_output_fail_closed (D) | false/欠落/null/非配列/偽booleanを拒否 |
| WindowsContract | byte_transport_and_no_arbitrary_executor_operation (D) | opaque bytesのbase64往復と自由shell拒否 |
| WindowsContract | powershell_query_stop_lock_contract (D) | 固定PSのCIM/Stop/Wait/Handle/mutex/失敗応答を静的検査 |
| WindowsContract | powershell_atomic_backup_identity_and_start_contract (D) | 固定PSのfile ID/link/create/flush/replace/再確認/startを静的検査 |
| WindowsContract | cli_default_dry_run_and_explicit_apply (D) | 偽executorを使う実CLIパーサ、既定preview、明示apply、秘密非出力 |
| RecoveryAndBoundary | restore_when_start_failed_and_query_proves_absence (F,G) | 診断IDと停止後のレシート復元。後者の実assertion REDはG |
| RecoveryAndBoundary | model_binding_rejects_paths_shell_and_forged_authorization (F) | 返すcallableの限定requestと信頼側の設定 |
| RecoveryAndBoundary | reserved_device_paths_are_not_operator_targets (F) | CON/NUL/COM1パス拒否 |
| RecoveryAndBoundary | windows_native_identity_layout_and_private_new_files (F,G) | Win32構造体Pack=4、新規ファイルのACL付きconstructor |
| RecoveryAndBoundary | mutating_model_binding_is_single_use (H) | 次の操作に承認を再利用できない |
| RecoveryAndBoundary | bound_apply_requires_exact_operator_approved_request (J) | 具体的requestを承認側で束縛するまで書き込めない |
| RecoveryAndBoundary | intervening_start_during_backup_or_readback_aborts | Aの再照会ガードに対する追加の回帰入力。初回GREEN。新しい実装挙動を追加しておらず、このテスト固有のREDを主張しない |
| ChannelFailure | nonzero_powershell_exit_overrides_success_frame (L) | 成功JSONに見えても異常プロセス終了は拒否 |

Pythonの制御順序・障害分岐は偽プロセス/ファイルexecutorで検証した。
PSの静的契約テストは、Windows API実行の代替ではない。Win32/.NET呼び出しを
実機で検証済みとは主張しない。

## 根拠を実際に読んだ範囲

非機密の対応ソースを読み、実iniは読んでいない。
最初の探索先 `.../windows/np2.cpp` 等は不存在。その後以下を確認した:

- `~/np21w-src/src/win9x/np2arg.cpp:40`:
  `Np2Arg::Parse()` はGetCommandLine→milstr_getarg、位置引数 `.ini` を採用する。
- 同 `ini.cpp:943`: `initgetfile()` は指定されたiniを使用。未指定時の探索は今回未対応。
- 同 `np2.cpp:4402`: modulefile設定と `file_setcd(modulefile)` 後に引数解析・ini読込。
- `~/np21w-src/src/common/milstr.c:618`: 引用符を除去し、引用外空白で区切る。
  実装は安全に理解できる2つの完全引用絶対パストークンに限定した。

PowerShell/.NETの根拠（Microsoft一次資料、2026-09-08参照）:

- [Get-CimInstance](https://learn.microsoft.com/en-us/powershell/module/cimcmdlets/get-ciminstance):
  Win32_Processとフィルタ、共通ErrorAction。
  [ErrorAction](https://devblogs.microsoft.com/powershell/erroraction-and-errorvariable/):
  Stopでエラーを終了扱いにする。成功空配列を明示的JSON配列として返す設計。
- [WaitForExit](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.process.waitforexit):
  ミリ秒指定の終了待ちの戻り値を確認し、さらにCIM不在を確認する。
- [File.Replace](https://learn.microsoft.com/en-us/dotnet/api/system.io.file.replace):
  同じボリューム上の候補で既存ファイルを置換する。前もって別の固有バックアップを保存。
- [BY_HANDLE_FILE_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information):
  volume/file index、FILETIME、サイズ、link数の配置。NTFSを前提としReFS等の保証はしない。
- [FileStream constructors](https://learn.microsoft.com/en-us/dotnet/api/system.io.filestream.-ctor?view=netframework-4.8.1):
  CreateNew/FileSystemRights/FileSecurityの.NET Framework overloadを使用。
- [Mutex](https://learn.microsoft.com/en-us/dotnet/api/system.threading.mutex?view=netframework-4.8.1):
  Windows Global named mutexで協調ワークフローを排他。

## 最終検査と境界

実行したホスト検査:

```bash
python3 -B -m unittest discover -s tools/tests -p 'test_*.py' -v
PYTHONPYCACHEPREFIX=/tmp/os32-np21w-live-pycache python3 -m py_compile tools/np21w_ini_live.py tools/tests/test_np21w_ini_live.py tools/np21w_ini.py tools/tests/test_np21w_ini.py
python3 -B ~/.codex/skills/.system/skill-creator/scripts/quick_validate.py .claude/skills/os32-emu-config
python3 -B tools/np21w_ini_live.py --help
```

結果: **51 tests / OK、skipなし**。Python構文検査exit0、スキル検査
`Skill is valid!`、help exit0。カーネル等は変更せず、OSビルド/配備/ゲスト試験は範囲外。

追加でWindows PowerShellの **Parser.ParseInputだけ** を実行しようとした。
固定PS_SERVERをUTF-8/base64のデータとして渡し、次の式で構文を解析するだけで、
スクリプト本文を実行する経路は渡していない:

```powershell
$source = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('PS_SERVERのbase64'))
$tokens = $null; $errors = $null
$null = [Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors)
Write-Output ('syntax errors: ' + $errors.Count)
if ($errors.Count) { $errors | ForEach-Object { Write-Output $_.Message }; exit 1 }
```

`powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand <上記のUTF16LE/base64>`
は **exit1: `WSL ERROR: UtilBindVsockAnyPort:309: socket failed 1`** で起動できず、
PS構文解析と埋め込みC#のコンパイルは未確認。Python構文成功をPS構文成功と混同しない。
この制限を迂回する実機操作や権限昇格はしていない。

実エミュレータ/CIM照会、実iniの読書き、実プロセスの停止・起動、環境変数ファイル/
資格情報/docs/hw読込、配備、commit、エージェント起動はすべて未実施。

残るライブ前提は **PMがソース対応を確認して選んだ明示起動対象、操作者の専有と
当該差分・強制停止の承認、利用可能なWSL→Windows PowerShell 5.1/CIM/NTFS権限**。
その環境でPS構文/C#と実制御を検証し、起動後にゲストbackendを観測する。
CLIと限定callable、停止検証器の実装は存在する。既存emu_agentへの自動登録は
今回の4ファイルの範囲外であり、PMホストがcallableを接続するかCLIを直接使う。
外部非協調プロセスの任意の競合を完全に防げるとは主張しない。

## 独立レビュー3件の修正 (2026-09-09)

この追補の変更は `tools/np21w_ini_live.py`、
`tools/tests/test_np21w_ini_live.py`、このTDD記録の3ファイルのみ。
既存の他作業者ファイルと並行作業中のplaybook実装は編集していない。
上記2026-09-08の実行記録は履歴として保持する。

### 指摘の確認と修正

- **File.Replace**: 修正前の固定PS本文は第3引数が `$null` だった。
  [.NET File.Replace](https://learn.microsoft.com/en-us/dotnet/api/system.io.file.replace?view=netframework-4.8.1)
  は追加バックアップ不要時にnullを要求する。
  [NullString](https://learn.microsoft.com/en-us/dotnet/api/system.management.automation.language.nullstring?view=powershellsdk-7.4.0)
  は.NETのstring引数にnullを渡すための型で、資料にはPowerShell 5.1参照アセンブリも載っている。
  この契約に従い `[System.Management.Automation.Language.NullString]::Value` に修正。
  当環境で旧 `$null` が空文字列になることを実測できたとは主張しない。
- **レシートサイズ**: `Snapshot` の4 MiB制限がini、原本、レシート読み戻し・loadに
  共通適用され、旧コードの`NewFile`は書き込んだ後にこの制限で失敗する構造を確認した。
  2,097,199 bytesの合成入力から、このテストのメタデータを含むPython JSONは
  **5,592,924 bytes**。レビュアーの5,592,619 bytesと完全一致するfixtureではないが、
  同じ上限超過を確認した。停止前に両データのbase64長とメタデータの保守的上限を検査する。
  メタデータはASCII JSON長の6倍、未取得の適用後file signatureは256文字を予約。
  実際の`FileIdentity.Read`は8整数と区切り文字だけでこの予約に収まる。
  `checked_snapshot`もsignatureの上限を検査する。候補ini自体のサイズ増加も停止前に拒否。
  `NewFile`にも書き込み前の4 MiBガードを追加した。
  上限は引き上げず、プレビュー可能でも適用時には保守的に拒否する場合がある。
- **派生パス**: 入力227文字にbundle/receipt suffixの57文字を加えると284文字になる。
  旧コードでは入力だけを検査し、GUID付きbundle、original.bin、receipt.json、pending
  の検査が無かった。
  [Windowsのパス長制限](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)
  を踏まえ、全派生パスに既存の保守的な「240未満」をUTF-16単位で適用する。
  これにより全componentも255以下、bundle directoryもlegacy directory制限内になる。
  最長suffixを含めたini上限は182 UTF-16単位。183は拒否する。
  検査はLive構築時なので、停止だけでなく実行器の起動・lock・照会にも先行する。
  ASCIIと非BMP文字で境界を試験し、単なるPython文字数の検査にしない。

### 実際のRED → GREEN

実装は最初に現行ソースを読み、ReviewBlockersの3メソッドを追加してから変更した。
次のコマンドの終了値はREDが1、GREENが0。ログを表示するtailの終了値とは区別した。

```bash
python3 -B -m unittest discover -s tools/tests -p test_np21w_ini_live.py -v
```

修正前（`/tmp/np21w-review-red.log`、最初の実行も同じ5 failures。
パスfixtureのprocess commandを合成targetに合わせて再実行した最終RED）:

```text
Ran 34 tests in 0.065s

FAILED (failures=5)
```

| defect / assertion | 修正前に観測した失敗 |
|---|---|
| `test_replace_uses_true_null_string` | `[IO.File]::Replace($temp, $target.ini, $null)` が期待するNullString式と不一致（1 failure）。静的契約assertionでありWindows APIのREDではない |
| `test_large_receipt_rejected_before_stop_or_write` | 2,097,199 / 4,194,304 bytesの各入力で `AssertionError: IniError not raised`（2 failures） |
| `test_derived_paths_rejected_before_executor_contact` | ASCII 227文字 / 非BMP文字の各targetで、callsが空であるべきassertionに失敗。旧処理はstop/backup/replace/receipt/startまで進んでから合成restart identity不一致で終了（2 failures）。preflight拒否ではないことをcallsで確認 |

3件修正後（`/tmp/np21w-review-green.log`）:

```text
Ran 34 tests in 0.074s

OK
```

追加した境界・統合回帰5件は初回からGREEN。これら固有のREDは主張しない。

- `test_receipt_last_accepted_and_first_rejected_byte_sizes`:
  同一メタデータで許容端を探索し、最後の許容入力と次の1 byteを実ワークフローで確認。
  このfixtureでは入力1,571,424 bytesで上限計算4,194,304、実Python JSON4,190,856。
  入力1,571,425 bytesは上限計算4,194,308となり停止前拒否。
  許容側は合成レシートをJSON/base64往復後、restoreの検証まで通す。
- メタデータ巨大化、cirrus-offによる候補の1 byte増加も停止前拒否。
- 全派生パスの182/183 UTF-16単位境界をASCII/非BMP双方で検査。
- `test_executor_integration_preflight_failure_order`:
  Live → 本物のPython WindowsExecutor → **偽transport** の構成。
  サイズ失敗時は正確に `lock, query, snapshot` だけを送ってclose。
  パス失敗時は送信ゼロ。stop/backup/replace/receipt/startを送るとテスト自体が失敗する。
  Windows APIの実行検証ではない。

### PowerShell一時fixture検証と残るプラットフォーム制約

`RealPowerShellFixture` は通常のdiscoveryではskip、次の明示コマンドだけで起動する。
固定PS_SERVERはstdinのデータとして**構文解析のみ**。本文全体は実行しない。
別の最小C#関数でstringへのnullマーシャリングを検査し、
`C:\Windows\Temp\np21w-live-fixture-<GUID>` の新規binファイル2個だけで、
ソースから抽出した実際のFile.Replace式、内容、候補消滅を検査してfinallyで清掃する。
既存ini・エミュレータ・CIM・環境変数へのアクセス経路は渡していない。

```bash
python3 -B tools/tests/test_np21w_ini_live.py --windows-fixtures RealPowerShellFixture -v
```

最終実行（`/tmp/np21w-review-windows-final.log`）はexit1:

```text
AssertionError: 1 != 0 : <3>WSL (4 - ) ERROR: UtilBindVsockAnyPort:309: socket failed 1

Ran 1 test in 0.018s

FAILED (failures=1)
```

先行実行も同じWSLエラーで `Ran 1 test in 0.016s / FAILED (failures=1)`。
後で固定PS本文をEncodedCommand内のbase64からstdin入力へ移し、Windowsコマンドライン
長の余裕を確保して再実行したが、同じ起動失敗だった。fixtureファイル作成には到達していない。
これは環境による起動失敗であり、欠陥のTDD REDやAPIの成功として数えない。
PS5.1での構文解析、stringマーシャリング、File.Replaceの実動作、
実NewFile/Snapshotによるレシートの保存・load、NTFS上のパス境界は未検証。
これらは利用可能なWindows環境でのfixture実行が残る。権限昇格や制限回避はしていない。

### 全新旧テストの最終出力

```bash
python3 -B -m unittest discover -s tools/tests -p 'test_*.py' -v
```

`/tmp/np21w-review-all-final.log`、exit0:

```text
Ran 90 tests in 0.545s

OK (skipped=1)
```

内訳はoffline既存20、live既存31 + 今回追加9（実PS fixtureのskip1を含む）、
その時点のplaybook30。playbookの実装・テストは編集していない。
全体の先行実行も `Ran 90 tests in 0.542s / OK (skipped=1)`。
Pythonの`compile(..., 'exec')`による対象2ファイルの構文検査も
`Python syntax: OK (2 files)`、exit0（コード実行・pycache書き込みなし）。

実エミュレータ/ini/env/secret読取、ライブプロセス操作、配備、agent起動は未実施。
OSカーネル等は変更しておらず、OSビルド・ゲスト試験は実施対象外。

## 再レビュー: apply → restore のレシート予約 (2026-09-09)

変更は `tools/np21w_ini_live.py`、`tools/tests/test_np21w_ini_live.py`、
この記録だけ。他作業者の変更は保持した。前節の境界試験はrestoreのpreviewまでで、
restore applyの成功を証明していなかった。前節の数値は修正前の履歴である。

### ソース確認と反例の実行

旧 `receipt_size_bound` は `applied.signature` だけ256文字を予約し、
`original.signature` は現在長を使っていた。`Live.run('restore')` は適用後snapshotを
次の `planned_record.original` に入れるので、その識別情報が長くなると再予約量が増える。
また再起動後のPIDと生成時刻も変わる。固定PSソースの `Query` はPIDをInt32にcastし、
UTC生成時刻を `ToString('o')` で返す。`FileIdentity.Read` は実際には7整数を連結する
（従来コメントの8を訂正）。PS本文は実行していない。

修正前に合成executorで1,571,320 bytes、元signature 53文字、置換後256文字を実行。
53文字はこのテストのtarget/process/diffでレビューの数値を再現するための合成値。
実Windowsの識別情報を取得したものではない。実際の出力（exit0、拒否例外は捕捉）:

```text
input bytes: 1571320 apply bound: 4194304
apply applied: True
restore refusal: receipt exceeds snapshot size limit; refusing before stop
restore calls: ['lock', 'query', 'snapshot', 'load', 'close']
```

### RED → GREEN

先に境界試験を変更し、保存レシートをJSON/base64往復後、署名256文字・PID最大値・
UTC生成時刻28文字への増加を伴う **`restore(..., live_apply=True, exclusive=True)`**
を実行させた。別途、上記固定反例を最初のstop前に拒否するassertionを追加。

```bash
python3 -B tools/tests/test_np21w_ini_live.py ReceiptAndPathBoundaries -v
```

修正前の実出力、exit1:

```text
ERROR: test_receipt_last_accepted_and_first_rejected_byte_sizes
np21w_ini.IniError: receipt exceeds snapshot size limit; refusing before stop
FAIL: test_review_counterexample_rejected_before_first_stop
AssertionError: IniError not raised
Ran 6 tests in 0.414s
FAILED (failures=1, errors=1)
```

ERRORはimport等の準備失敗ではなく、適用成功・レシート往復後のrestore apply本体が
サイズ検査で拒否したもの。実装修正後、同じコマンドはexit0:

```text
Ran 6 tests in 0.541s
OK
```

### 予約の根拠と追加検証

- 両signatureをmetadataから空文字にし、各々 `12 * SIGNATURE_LIMIT` bytesを別枠予約。
  `checked_snapshot` の最大256 Unicode文字は、1文字あたり最大12 ASCII bytes
  （JSONのescaped UTF-16 surrogate pair）で収まる。数値署名だけでなく、引用符、
  backslash、制御文字、BMP/非BMP文字を含む許容snapshotも覆う。
- processは現在のJSON長と、束縛targetの起動コマンド・Int32最大PID・28文字のUTC時刻を
  持つ再起動時の予約用rowのJSON長の大きい方を使う。その他metadataの6倍予約は保持。
  固定Query/Start経路で変わるPID/時刻を初回から見込む。dataのbase64長の和は交換しても
  不変、diffの逆転も同長、operationは `cirrus-on/off` から `restore` へ短くなる。
  この固定executorのapply→restoreで、既知の可変metadataによる再予約増加を防ぐ。
- 境界の許容側はrestore applyの `applied=True`、原本全bytes、stop/replace/start各1回、
  restoreレシートのサイズとJSON/base64往復を検査。拒否側は
  `lock, query, snapshot, close` のみ、backup無し、snapshot不変を検査。
- 両cirrus操作について各種escaped signatureと再起動metadataで、実JSON長が予約以下、
  restoreの予約量が初回以下である追加試験を実行。これは初回GREENであり固有REDはない。

修正後の境界探索と合成apply/restoreの実出力（exit0）:

```text
input bytes: 1569661 bound: 4194302
input bytes: 1569662 bound: 4194306
apply JSON bytes: 4186404
restore applied: True restore bound: 4194290 restore JSON bytes: 4186673
```

### 全ini試験・pycompile・未検証範囲

```bash
python3 -B -m unittest discover -s tools/tests -p 'test_np21w_ini*.py' -v
```

実出力、exit0:

```text
Ran 62 tests in 0.639s
OK (skipped=1)
```

`py_compile.compile(..., doraise=True)` をoffline/liveのソース・テスト4ファイルに実行。
`cfile` は `/tmp` のTemporaryDirectory内に指定し、終了時に清掃。
実出力は `py_compile: OK (4 files)`、exit0。環境変数の読取・変更はしていない。

skip1はopt-in `RealPowerShellFixture`。今回はWindows fixtureを起動していない。
前節に記録されたWSL `UtilBindVsockAnyPort:309: socket failed 1` の制限は未解消・未再検証。
PS5.1構文/C#、nullマーシャリング、実File.Replace、NewFile/Snapshotのreceipt保存/load、
NTFSパス境界のWindows fixture検証は残る。Python/fake executorの成功をWindows APIや
ライブ試験の成功とはしない。ネットワーク、実ini、env/secret、実エミュレータ操作、
配備、エージェント起動は未実施。全ini試験の一時合成ファイルのみを使用した。

## np21w_ctl.py の起動形を受け付ける (2026-09-30)

### 不具合

`tools/np21w_ctl.py start --ini <name> [--fd <name>]` は
`Start-Process -FilePath <exe> -ArgumentList @('"/i<ini の絶対パス>"'[, '"<fd の絶対パス>"'])`
で起動する (`start()` の `args = ['/i' + win_of(ini)]`、`start_script()` が各要素を `"…"` で包む)。
実測のコマンド行は `"<exe>" "/i<ini>" ` (末尾に空白)。旧 `identify()` の 2 トークンの正規表現は
これに一致し、2 語目 `/iC:\…` を `path_key` に渡して `unsupported absolute Windows path` で
止まっていた。プレビュー (読み取りのみ) も使えず、ctl で起動した NP21/W にメモリ量の切り替えができなかった。

### NP21/W ソースの根拠 (`~/np21w-src/src`)

- `win9x/np2arg.cpp` `Np2Arg::Parse`: `GetCommandLine()` を `milstr_getarg` で区切る。
  `/` か `-` で始まる引数は 2 文字目を `_totlower` してスイッチ (`i` → ini = 3 文字目以降、
  後勝ち。`f` → 全画面)。それ以外は拡張子で分類: `ini`/`npc`/`npcfg`/`np2cfg`/`np21cfg`/`np21wcfg` → ini、
  `iso`/`cue`/`ccd`/`cdm`/`mds`/`nrg` → CD、残りはディスク (最大 4)。
  ini が `:` を含まず `\` で始まらなければ現在のディレクトリを前に付ける。
- `common/milstr.c` `milstr_getarg`: 引用の外の空白で区切り、`"` は取り除く
  (`"/iC:\x.ini"` は `/iC:\x.ini` になる)。
- `win9x/np2.cpp` (WinMain): コマンド行のディスク `disk(i)` を `diskdrv_readyfdd(i, …)` で FDD i+1 に入れる。
- `win9x/ini.cpp` `initgetfile`: `iniFilename()` があればそれを使う。
- `diskimage/fddfile.c`: d88/88d/d98/98d → D88、fdi、nfd、その他は生イメージ。

### 受け付ける形 / 拒否する形

受け付けるのは 3 形だけ (トークンはどれも引用されたドライブ絶対パス、末尾の空白は無視):
`"<exe>" "<ini>"` (従来)、`"<exe>" "/i<ini>"`、`"<exe>" "/i<ini>" "<fd>"`
(fd の拡張子は `.d88/.88d/.d98/.98d/.fdi/.nfd/.hdm/.img`)。`/i` は小文字だけ (ctl が出す形)。

NP21/W が受け付けても拒否する: `-i`、`/I`、引用の無い `/i…`、`/i` 2 回、相対 ini、`/i` の後の空白、
`/f`・未知のスイッチ、スイッチが ini より前、相対 fd、CD (`.iso`)・2 つ目の ini・cfg、fd 2 個、
位置引数の ini + fd、別の ini、引用の無い fd、別の exe。

### 再起動の形

`Live.run` は止めたプロセスのコマンド行から `launch_of()` で `{'form': 'switch'|'positional', 'fd': …}` を作り、
`start` に `launch` として渡す。固定 PS の `start` は `$target.ini` と `launch` だけから
`"/i<ini>"` か `"<ini>"`、FD 付きなら ` "<fd>"` を組み立てる (fd は `^[A-Za-z]:\\[^"\r\n]+$` と `CheckPath`、
位置引数の形との組み合わせは拒否)。.NET の `Process.Start` のコマンド行は `"<FileName>" <Arguments>`。
起動後の CIM のコマンド行から `launch_of()` を取り直し、元と違えば `restart launch shape changed` で失敗
(自動の巻き戻しはしない — 従来どおりレシート ID を付ける)。restore は停止中でもレシートに記録した
`process` のコマンド行から同じ形を作る。**FD 引数付きで起動していたら同じ FD 引数で起動し直す**
(ctl の `start --ini --fd` の再現。FD は ini の変更対象キーと無関係で、強制終了なので NP21/W は ini を書き戻さない)。
`receipt_size_bound` の再起動行も同じ形の文字列 (`launch_arguments`) で見積もる。

### RED → GREEN

`L` = `python3 -B tools/tests/test_np21w_ini_live.py`。

| 段階 | 結果 | 内容 |
|---|---|---|
| RED | 基点 e620f1e の `np21w_ini_live.py` の写し + 新しい試験: 56 tests, failures=9, errors=24 | 新クラス `CtlLaunchShapes` の 9 件中 7 件が失敗 (ctl 形の受理 (`launch_of` 不在を含む)・プレビュー・形の維持・restore の形・形の変化の検出・PS の組み立て・`launch_arguments`)。既存の errors は偽 executor の `start` が `launch` 引数を要求するようになった契約変更による |
| GREEN | L: 56 tests, OK (skipped=1、既存の Windows fixture) | `launch_of` / `launch_arguments`、`start` への `launch`、再起動後の形の照合、PS の組み立て |

`test_rejects_unsupported_variants` は基点でも GREEN (旧実装は 2 トークン以外を全部拒否していた)。
受理範囲を広げた後も拒否が保たれることの回帰試験であり、固有の RED は主張しない。
`test_executor_forwards_launch_to_fixed_start` も基点で GREEN (executor は引数をそのまま運ぶ)。
契約の固定であり、固有の RED は主張しない。両者の否定側は変異で見る。

### 変異 (`--mutate`、`make check-np21w-ini-live-host`)

`test_np21w_ctl.py` と同じ作り (一時ディレクトリの写しに当て、実物は読むだけ)。13 本すべて RED、
恒等の対照 GREEN、構文を壊す 1 本は NOT COUNTED。変異: `-i`/`/I` の受理、`identify` が形を見ない、
位置引数 + fd の受理、fd の拡張子を見ない、`.d88` を拒否、fd を何個でも受理、再起動を常に位置引数の形で、
再起動で fd を落とす、再起動後の形を比べない、PS が `/i` を落とす、PS が fd の `CheckPath` をしない、
PS が位置引数 + fd を受理、`launch_arguments` が `/i` を落とす。

### 未実施

実 NP21/W・実 ini・Windows 側の起動は触っていない (ホスト限定の依頼)。PS の `start` の組み立ては
静的な断片の検査だけで、.NET 上の実行は未検証。ctl で起動した実プロセスへのプレビュー・適用は PM が別途行う。

## 置換後の失敗でレシートが残らない (2026-10-01)

### 不具合 (PM が実プロセスで遭遇)

ctl で `"<exe>" "/i<ini>"` の形で起動した NP21/W に `ram-8mb --live-apply --exclusive-operator` を当てると
`Windows operation failed: snapshot; retain backup and verify process state; retained receipt ID: <ID>` で終わった。
NP21/W は停止したまま、ini は `ExMemory=16 → 7` の 1 行だけ置換済み、バンドルには `original.bin` だけで
**`receipt.json` が無く `restore` 不能**。直後の `np21w_ctl.py status` で `os32.nhd` が数秒 `locked`。

### 原因

- 失敗した段: 基点 5eca3b8 の `Live.run` 272 行 — `replace` の直後、レシートを書く前の `ex.call('snapshot')`。
  PS が `{"ok":false}` を返した (`WindowsExecutor.call` の `Windows operation failed: snapshot`)。
  273 行より前の段は置換前 (ini が変わっていない) か、`receipt` の後 (レシートがある) なので、症状と合うのはここだけ。
- 何が失敗したか (推定): 同じ `Snapshot` を `replace` 要求の中で直前に通しているので、決定的な検査
  (`CheckPath`・hardlink・サイズ) ではない。`[IO.File]::Open(..., FileShare.Read)` の**共有違反**が最有力
  — 強制終了と `File.Replace` の直後に Windows 側 (Defender の走査など) がファイルを一時的に掴む。
  NHD が停止の数秒後まで `locked` だったこと、§4-60 の同種の観測と合う。掴んだ主体は未確認 (実機で再現していない)。
- 仮説 (b) `/i` 起動形 (9155860) は無関係: 置換後・再起動前の検査はコマンド行を見ない
  (`launch_of` は再起動の直前に初めて使う)。偽 executor で `/i` 形の置換後失敗を再現しても同じ。
- レシートが無い理由 (構造): レシートは置換の**後の別要求** (`receipt`) で書いていた。固定 PS は失敗した要求で
  セッションを閉じる (`break`) ので、置換と `receipt` の間のどの失敗 (`query`・`snapshot`・読み戻しの不一致) でも
  レシートは書かれず、Python からも書き直せない。

### 直し

1. **レシートは `replace` 要求の中で書く**: `File.Replace` → 全バイトの読み戻し → 一致したら `WriteReceipt`
   (`original.bin` の照合、`applied` = 読み戻し、`receipt.json` を `NewFile`) → `AssertAbsent`。
   Python は `replace` に `receipt=<backup ID>` と計画済みレシート (`applied` は `pending`) を渡す。
   独立の `receipt` 操作は PS と `WindowsExecutor.OPS` から削除した。
2. **置換後の確認を 1 回に**: `replace` の後は `applied['data'] == candidate`、不在、スナップショット一致を見て
   再起動へ進む (従来のレシート前後 2 回の比較を、レシートが先に書かれた後の 1 回に統合)。
3. **失敗の文言で復元の可否を分ける**: `replace` が成功した後の失敗は `receipt ID for restore: <ID>`、
   その前は `retained backup ID (receipt not confirmed): <ID>`。
4. **一時的な共有違反だけ待つ**: `Snapshot` のファイルを開く所を `OpenShared` にし、HRESULT の下位 16 ビットが
   32 (共有違反) / 33 (ロック違反) の `IOException` だけを 250ms 間隔・1 回の open につき 4 秒まで再試行する
   (`$TransientMs = 4000`)。ファイル無し・その他の I/O エラー・識別情報の変化は再試行しない。最も重い `replace` は
   最大 5 回開くので 5 × 4 秒 + 余裕 5 秒 ≦ 1 要求の上限 `EXCHANGE_TIMEOUT` (30 秒) を試験で固定。

保つ性質: 原本の保存と読み戻し、置換前の不在・内容・識別情報の再確認、別プロセス・別 ini を対象にしない、
自動の巻き戻し・追加の起動・再試行による再起動はしない (再試行はファイルを開く 1 か所だけ)。
読み戻しの不一致や読み戻し自体の失敗ではレシートを書かない (restore はどのみち `applied` の検証で拒否する) —
その場合は従来どおり `original.bin` を残して操作者が調べる。

### RED → GREEN

`L` = `python3 -B tools/tests/test_np21w_ini_live.py`。

| 段階 | 結果 | 内容 |
|---|---|---|
| RED | 基点 5eca3b8 の `np21w_ini_live.py` + 新しい試験: 63 tests, failures=11, errors=8 | 新クラス `PostReplaceFailure` の 7 件すべて (事故の再現: `/i` 形 + `ram-8mb` + 置換後の `snapshot` 失敗 → レシートあり・再起動なし・停止状態から `/i` 形で restore 可、置換後の各失敗点 (不在照会・snapshot・start・起動後の照会 2 回) でレシート、置換前の失敗は「未確認」、置換後の変化で再起動しない、executor が ID とレシートを `replace` に運ぶ、PS の順序、PS の再試行の範囲)。既存の errors は偽 executor のレシート記録を `replace` に移した契約変更による (restore 系) |
| GREEN | L: 63 tests, OK (skipped=1、既存の Windows fixture) | 上記の直し |

既存の `test_queries_fail_closed_at_every_phase` は照会の回数 (8 → 7) を成功時の実測から数えるように変えた。

### 変異 (`--mutate`)

追加 10 本 (合計 23 本) すべて RED、恒等の対照 GREEN、構文を壊す 1 本は NOT COUNTED:
`replace` にレシートを渡さない、`replaced = True` を消す、置換後のスナップショット比較を消す、PS がレシートを
書かない、PS がレシートを不在確認の後に書く、PS が `applied` を読み戻しで上書きしない、PS が再試行しない、
PS が全 `IOException` を再試行する、PS が上限なしに再試行する、再試行の上限が 1 要求の上限を超える。

### 未実施

実 NP21/W・実 ini・Windows の PowerShell では動かしていない (ホスト限定の依頼、この環境に pwsh も無い)。
PS の変更 (`OpenShared`・`WriteReceipt`・`replace` の順序) は静的な断片と順序の検査だけで、PS 5.1 での構文解析
(`--windows-fixtures`) も未実行。原因の「共有違反」は推定で、実機で失敗時の例外を観測していない
(PS は例外文を出さない設計)。再起動直後に NHD が掴まれている場合 (§4-60) の NP21/W 側の起動の失敗は、この道具は
待たない (NHD のパスを知らない) — 別件。

### Codex レビュー (296a360、Request changes) への対応

- **P2-1 同じバイト列の別ファイルを適用結果として採る**: 置換後の読み戻しが共有違反で待っている間に、
  同じバイト列でファイル ID の違う B に差し替わると、バイトの一致だけで合格し、B の識別情報が `applied` になっていた。
  → PS の `NewFile` が読み戻しのスナップショットを返すようにし、`replace` は候補 (一時ファイル) の
  volume:IndexHigh:IndexLow (`FileKey`) と置換後の読み戻しのそれを照合する (`ReplaceFile` は候補の ID を残す)。
  違えばレシートを書かずに固定の理由コード `{"ok":false,"reason":"candidate-identity"}` を返す
  (例外文は従来どおり出さない)。`WindowsExecutor` はそれを `readback identity differs from the candidate
  (no receipt written)` に訳す。`replace` の戻り値は `{applied, candidate}` になり、Python 側も
  `file_key(applied.signature) == candidate` を実行時に再確認する (PS の検査が壊れていても再起動・receipt 案内に進まない)。
  他の `NewFile` の呼び手は `$null =` で戻り値を捨てる (応答への混入を防ぐ)。
- **P2-2 待機中に入った reparse point を追う**: `CheckPath` を `OpenShared` のループの各試行の前と、open 成功後
  (失敗なら handle を閉じて失敗) に移した。
- **P3**: restore 自体が置換後に失敗したときは `restore-operation receipt ID (not restorable; investigate)`
  (そのレシートは `operation=restore` で restore できない)。再試行の待ちの予算は 4 秒 (Codex 2 回目 P3: 期限の直前に始めた CheckPath と同期の open そのものは期限を越えうるので「厳密な期限」ではない): 残り時間 `$left` が 0 以下なら
  眠らずに失敗、眠るのは `min(250, $left)`、眠った後に期限を過ぎていれば次の open をしない。

RED (296a360 の実装 + 新しい試験): 70 tests, failures=13, errors=19 (新クラス `ReviewRetryRaces` 7 件すべてと、
`replace` の戻り値の契約変更に伴う既存の偽 executor 経由の試験)。GREEN: 70 tests OK (skipped=1)。

変異: 追加 11 本 (計 34 本) すべて RED、恒等 GREEN、構文 1 本 NOT COUNTED。**実行時に検出するのは Python 側の 5 本**
(同じバイト列の別ファイルを採る、理由コードを一般の失敗に落とす、`file_key` が不正な識別情報を通す、
executor が不正な候補キーを通す、失敗した restore が自分のレシートを restore に案内する)。
PS 側 (試行ごとの `CheckPath`・open 後の `CheckPath`・期限・`FileKey` の照合・`$null = NewFile`・理由コードの出力、
および前節の PS 変異) は PS 本文の断片と順序の静的検査で検出するだけで、**34 本の RED は PS の実行を保証しない**。

### 未実施 (追記)

PS 5.1 での実行・構文解析は今回も行っていない。共有違反の実例外、`OpenShared` の待機と期限、`FileKey` の照合、
`receipt.json` の実シリアライズ (`applied` の上書き)、読み戻しの失敗、試行中の差し替え (別ファイル・シンボリックリンク) は
Windows 上で再現していない。

## 実適用・restore が成功しても最後に cleanup timeout (2026-10-01)

**症状** (PM の実プロセス): `ram-8mb --live-apply --exclusive-operator` で ini は置換、`receipt.json` も
作られ、NP21/W は再起動して 8MB で動いていたのに、出力は `error: Windows executor cleanup timeout;
inspect process state` だけ (`receipt:` / `verified process PID:` が出ない)。`restore` も同じ。プレビューは正常。

**原因**: PS の `'start'` が `UseShellExecute=$false` で NP21/W を起動していた。.NET Framework はこの経路で
`CreateProcess(bInheritHandles=TRUE)` を呼ぶので、NP21/W が PS の stdout パイプ (継承可能なハンドル) を持ち続ける。
PS は stdin の EOF で終わる (`wait` は成功) が、reader は EOF に届かず、`_close_reader()` の 0.1 秒の
bounded join が失敗して `IniError` (`PowerShellTransport._close_reader`、np21w_ini_live.py の旧 626 行) になる。
`Live.run` は成功を返し終えているが、`with self.executor` の `__exit__` で投げられるので CLI には失敗だけが出る。
NP21/W を起動しないプレビューでは起きない。trial は同じ原因を 2026-09-14 (実走 F3、`np21w_trial_tdd.md` 追記 6)
に ShellExecute で直していたが、live には入っていなかった。

**直し方** (`'start'` だけ、trial と同じ形): `$si.UseShellExecute=$true`、リダイレクト無し。exe・引数
(`"/i<ini>"` / 位置引数、FD)・作業ディレクトリは不変。ShellExecute では `$p.Handle` が取れないことがあるので
`$p` が無ければ失敗、PID (`$p.Id`) を要にし、生存確認は `Start-Sleep -Milliseconds 1000` + `$p.HasExited`
(`WaitForExit(1000)` はやめた)。PID から先の照合 (CIM の exe / コマンド行の形 / 生成時刻を 2 回) は Python 側で不変。
**変えないもの**: EOF 未到達・wait timeout を失敗とする transport の不変条件、live の kill fallback、
kill / retry を足さないこと、成功の判定が PS の応答であること。

**RED** (`python3 -B tools/tests/test_np21w_ini_live.py`、修正前): 71 tests, failures=2, skipped=1 —
新試験 `test_powershell_start_does_not_hand_the_transport_pipe_to_the_emulator` (ShellExecute・リダイレクト無し・
`$p` の検査 → `$p.Id` → 1 秒 → `HasExited` → `$value` の順) と、`$false` を要求していた既存の断片を `$true` に
改めた `test_powershell_atomic_backup_identity_and_start_contract`。**GREEN**: 71 tests OK (skipped=1)。

**再現** (`test_np21w_transport.py` の `test_live_cli_restart_success_depends_on_launcher_not_passing_the_pipe`):
PS の代わりの無害なホスト Python 子が `start` で孫 (1.5 秒 sleep、kill しない) を起動し、継承あり / 無しで
live の CLI 全体 (合成の Fake 経由のライフサイクル、`start` と close だけ実 pipe) を回す。継承ありで
rc=2・`cleanup timeout`・`receipt:` / `verified process PID:` 無し (= PM の症状)、継承無しで rc=0 と両方の行。
Python 側は変えていないので**この試験は最初から GREEN** (特性の固定であって、修正の RED ではない)。

変異: 追加 5 本 (`$false` に戻す、`RedirectStandardOutput` を足す、`HasExited` の検査を消す、`$null` の検査を消す、
1 秒の待ちを消す) を含め 39 本すべて RED、恒等 GREEN、構文 1 本 NOT COUNTED。5 本は PS 本文の静的検査で検出する
だけ。PS 5.1 の ParseInput (データとして渡すだけ、ファイル・プロセス操作なし) で PS_SERVER の構文解析は通った。

**未確認** [V4]: ShellExecute で起動した NP21/W が実際にパイプを継承しないこと、`HasExited` / `Id` が実プロセスで
期待どおりに取れること、CIM の CommandLine が従来の照合 (末尾空白の許容) で通ること — trial の実走では
同じ形が通っているが、live での実プロセス確認は PM 待ち。

## 実走の段ごとの失敗を読むための診断 (2026-10-01、5793c75 の後)

**経緯** (PM の実プロセス、`wt/inicleanup`): 1 回目は `replace` が失敗 (ini は置換済み・`receipt.json` 無し)、
2 回目は止める前の `snapshot` が失敗。03:40 の main (4233715) では同じ操作が replace とレシートまで通った。
PS は例外文を出さないので何が落ちたか分からなかった。

**変更**:
- PS: `$Op` (既知の操作名だけ、他は `request`)・`Step` (段)・`Detail` (手順) を固定の文字列で記録し、
  catch は `FailureJson` で `{"ok":false,"step":"<op>:<段>[.<手順>]","type":"<最内の例外の型名>",
  "win32":<HResult の下位 16 ビット>,"hresult":"0x<8 桁>"}` を返す (candidate-identity のときは `reason` も)。
  例外の Message は candidate-identity との比較にだけ使い、写さない。整形自体が失敗したら従来の `{"ok":false}`。
- Python: `failure_detail()` が固定の 4 項目 (+ `reason`) だけを受け、全項目が型・正規表現・`hresult` と
  `win32` の整合に合うときだけ ` [step type win32=N hresult=0x...]` を失敗の文言に足す。余分な項目や外れた値は
  捨てる (文言は従来どおり、何も写さない)。candidate-identity は診断つきでも認識する。

**RED/GREEN の順序について**: この節は実装を先に書き、既存の試験 3 件が落ちたのを見て (旧 catch 行の断片・
`CheckPath` の断片・candidate-identity の応答の形) 直し、その後に新しい試験を足した。新試験の RED は取っていない。
代わりに変異 10 本 (下) で新試験が実装の各部を検出することを確かめた。

**試験** (`FailureDiagnosis` 6 件 + opt-in 1 件): 正しい診断が文言に入る (snapshot / replace / candidate-identity)、
余分な項目 (message)・パス入りの段・長すぎる段・空白入りの型・bool / 範囲外 / 文字列の win32・hresult と食い違う win32・
余計な語のある hresult・欠けた項目は捨てられて素の文言になる、CLI の stderr に出る、PS のすべての `Step` / `Detail` の
文字列が Python の正規表現に収まる、`replace` と `OpenShared` / `Snapshot` / `WriteReceipt` の段の順序、
`FailureJson` が Message を写さないこと。
opt-in `--windows-fixtures` の `test_failure_json_runs_under_windows_powershell` は PS_SERVER から `Step` / `Detail` /
`FailureJson` だけを抜き出して **PS 5.1 で実行** (合成の例外だけ。ファイル・CIM・プロセス・ini に触らない):
IOException(0x80070020) → `snapshot:ini.open IOException win32=32`、`[Convert]::FromBase64String` の失敗 →
最内の `FormatException win32=5431 hresult=0x80131537`、`throw '...'` → `RuntimeException win32=5377
hresult=0x80131501` (最初は 0x80131500 と見込んで外れ、実測で直した)。出力に例外文 (`secret`) が無いことも見る。
全体の PS_SERVER は PS 5.1 の ParseInput で構文解析が通った (実行はしていない)。

変異: 追加 10 本 (Message を写す、最内をたどらない、整形の失敗の受け皿が無い、`Detail 'open'` / `Step 'File.Replace'` /
`Step 'readback'` を消す、Python が任意の段を通す・win32 と hresult の食い違いを通す・余分な項目を通す・診断を
文言に入れない)、段名が入って形の変わった既存 4 本を追従。計 49 本すべて RED、恒等 GREEN、構文 1 本 NOT COUNTED。

**PS 5.1 の観点での静的な見直し** (`OpenShared`・`FileKey`・`NewFile` の戻り値・`start`): 問題は見つからなかった。
- 関数の戻り値への漏れ: `CheckPath`・`Step`・`Detail` は代入だけ、`NewFile` 内の `SetAccessRuleProtection` /
  `AddAccessRule` / `Write` / `Flush` は void、`backup` と `WriteReceipt` の `NewFile` は `$null =`、`[void]CreateDirectory`。
  `OpenShared` は FileStream (列挙されない) を返す。
- API: `Exception.HResult` の getter は .NET 4.5 で public (PS 5.1 の前提)、`FileStream(path, mode, FileSystemRights, share,
  size, options, FileSecurity)` と `Flush(bool)` は .NET Framework 4.x にある (Core には無い)、`-notin` / `-cin` は PS 3 以降。
  `[Math]::Min(250, $left)` は long 同士に解決される。
- 1 要素の配列: 応答の配列は `@(Query)`、件数は `@(Query).Count`。受け取った `record.diff` は ConvertFrom-Json の配列のまま
  ConvertTo-Json に渡る。
- `start`: ShellExecute の `Process.Start` は `hProcess` を持つので `Id` / `HasExited` は使える (trial で実績)。
  `$null` の検査を足してある。
**分からないままのこと**: 1 回目・2 回目の失敗の実体 (共有違反・拒否・読む間の変化など)。この版で再現すれば角括弧の中に出る。
NP21/W が ini を開く時間帯 (np21w-src `win9x/np2.cpp` の `initload` / `initsave`、`win9x/ini.cpp` の
GetPrivateProfileString) の検討はスキルの「失敗の読み方」に書いた — 起動直後は重なりうるので `--wait-ready` の後に実行する。
GetPrivateProfileString が開くときの共有モードは確かめていない。
