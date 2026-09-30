# Transport shutdown 補足 TDD

## PM による先行記録の回収

共有ファイルへの同時編集で先行ログが上書きされた。以下は保存済みの
`deleg_f0383505/task-0.log` を読み直して回収した経過であり、独立した過去実行の再現ではない。
ログは `~/.hermes/cache/delegation/live/` 以下にある。

- 43–46行: 実 pipe 回帰テスト作成後、`python3 -B tools/tests/test_np21w_transport.py -v`
  が exit 1、trial の bounded failure テストが FAIL。保存ログの出力は途中省略されている。
- 47–52行: reader 所有の close と bounded join を実装後、同コマンドが1件 OK。
- 53–56行: live の kill 後に timeout を成功扱いしない追加テストが FAIL。
  この時点で担当が中断した。後続担当の修正・追加試験とは区別する。

PM は最終コードで全 tools 128件、失敗なし・2 skip、および別途 Windows parser
1件成功を再確認した。先行ログの省略部分や上書きされた文書本文は復元したと主張しない。
同時編集により履歴帰属に制限があるため、現在の機能検証と完全な TDD 履歴受入は分離する。

この作業開始時点で、共有 workspace には reader 所有の stdout.close、0.1秒 bounded join、live の kill 後も失敗にする変更と実 pipe テスト2件が既に存在した。その変更の RED をこの記録で新たに主張しない。並行作業による追加テストは保持した。

## この作業の RED → GREEN

`python3 -B -m unittest discover -s tools/tests -p test_np21w_transport.py -v`

追加した `test_trial_preserves_wait_timeout_while_reader_is_blocked` を実装修正前に実行:

```text
Ran 3 tests in 0.312s
FAILED (failures=1)
AssertionError: IniError('Windows executor cleanup timeout; inspect process state')
is not an instance of <class 'subprocess.TimeoutExpired'>
```

`_close_reader(failed=...)` で既に伝播中の wait/kill 例外を上書きせず、reader の bounded join は必ず行う。両 sibling close から同じ判定を渡す。試験追加後の full tools GREEN:

```text
python3 -B -m unittest discover -s tools/tests -p 'test_*.py' -v
Ran 128 tests in 8.419s
OK (skipped=2)
```

追加の回帰試験（初回 GREEN、独自 RED とは主張しない）:
- 固定の無害なホスト Python 子プロセスを Popen し、本物の buffered stdout と実 wait(timeout=3) で trial/live の成功と timeout を試験。4秒上限の caller join。試験所有の子だけを finally で kill/wait し、reader 終了・stdout.closed を確認。
- trial timeout 後も子が生存していることを確認し、trial の kill 禁止を実動作でも検証。
- 合成 lifecycle と実 pipe cleanup を組み合わせた CLI で exit2、ok=false、dispose/lock stage、completed、trial/plan/cwd、process、retry=false/restore=false を確認。失敗した lock stage も cleanup timeout で失われない。raw timeout 診断は JSON に出ない。

## Parser only

```text
python3 -B tools/tests/test_np21w_trial.py --windows-parser PowerShellParser -v
Ran 1 test in 0.236s
OK
```

生成した trial PS_SERVER は stdin DATA として ParseInput に渡すのみ。実 lifecycle は実行していない。

## 残る境界

transport shutdown の fail-closed と待機上限の試験であり、実 Windows 子の継承 pipe / WSL wait 問題の解消や emulator lifecycle 成功を証明しない。reader は継承 writer が EOF を出すまで daemon として残り、その EOF で自分の stdout を閉じる。永久に writer が残る場合の即時資源回収は保証しない。stdin write/flush/close の一般的な backpressure 問題は今回の reader-lock 修正対象外。

実 emulator/PID34864、CIM、実 ini、API/network、env/secrets、docs/hw、build/deploy、agent 起動、commit は操作していない。live の既存 kill semantics と trial の no-kill、承認 gate、ini 変換、PS lifecycle 本文は変更しない。

## 2026-10-01 追記 — live の再起動後の cleanup timeout

live の実適用・restore が、成功しても最後に `Windows executor cleanup timeout` を返していた。原因は transport ではなく
PS `'start'` の `UseShellExecute=$false` (NP21/W が stdout パイプを継承)。直したのは起動側だけで、この節の
不変条件 (EOF 未到達は失敗、wait timeout を成功にしない、trial は kill しない、live は元の kill fallback のみ) は不変。
`test_live_cli_restart_success_depends_on_launcher_not_passing_the_pipe` が、孫が stdout を継承する / しない場合の
live CLI の出力 (失敗のみ / receipt と PID) を実ローカル pipe で固定する。詳細は
[`np21w_ini_live_tdd.md`](np21w_ini_live_tdd.md) の同日の節。
