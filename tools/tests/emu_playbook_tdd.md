# GUI playbook ホストTDD記録

2026-09-09。対象は `tools/emu_agent/playbook.py` と
`tools/tests/test_emu_playbook.py`。実機・ネットワーク・LLM呼び出し、実設定・環境変数・
資格情報・`docs/hw/`の読み取り、配備、コミット、追加エージェントは使用していない。
`/tmp/os32_local_gui.py` はソースを読んだだけで、実行していない。

## 読んだ実装と境界

- `CLAUDE.md`、`docs/CONSTRAINTS.md`、開発案内・索引。
- `tools/emu_agent/agent.py` の定義、`run`、`main`、`parse_action`。
  汎用ループは不正JSONに再試行し、未知操作後も継続する。`done` は台本進捗を確認しない。
- `tools/gui_gate.py`: `key(seq=None, text=None)` は4文字ごとにPOSTして0.35秒待つ。
  `Mouse(h).click(x,y)` / `.drag(x0,y0,x1,y1)` がmove/press/releaseを実行する。
  POST応答は元ヘルパーでは捨てられる。横座標換算は640ピクセル固定。
- `tools/np21w_mcp/np21w_client.py` と `server.py` のステータスAPI定義。
  `act_status({})` が返す生JSONの `scrn_xmax` / `scrn_ymax` を事前確認に使う。
- 旧scratch wrapperは `int(...)` による変換、`agent.ACTIONS` の差し替え、広い既存
  プロンプトへの追記だけで、実行順・引数のホスト照合がなかった。

新CLIは本番の `llm_chat` / `clip` / `obs_failed` と観測ヘルパーを再利用する。
`agent.run` / `ACTIONS` / `parse_action` は使わない。元のログ書き込み関数 `rec` は
`run` 内部のローカル関数なので、同じJSONL・時刻・flush形式で独立記録する。
元モジュールのグローバルを変更せず、実ヘルパー関数を専用の通信境界に束縛する。

テストでは `gui_gate` の実関数に偽POST・偽sleepを接続する。
`agent.py` はASTから必要な観測関数と失敗判定定義だけを抽出して実行し、
モジュール初期化時の環境変数読み取りも避ける。本番factory試験も偽agentモジュールを使用。

## 実行したRED→GREEN

実装前に `strict_object` / `exact_equal` / `validate_plan` / `Runner` /
`LiveDriver` / `main` の最小スタブを置き、assertionを先に実行した。
以下の件数はunittestの実出力。subTestの失敗件数はテストメソッド数を超える。

### 1. 台本・照合・本番ループ・CLI

```sh
python3 -m unittest tools.tests.test_emu_playbook > /tmp/emu_playbook_red1.txt 2>&1
```

```text
Ran 15 tests in 0.011s
FAILED (failures=75, errors=1)
```

厳格JSON、再帰的な型付き比較、不変台本、完全な事前検証、専用dispatcher、停止ラッチ、
既定dry-runと明示executeを実装。
最初のGREEN試行は `FAILED (failures=1)`。改変耐性テストが共有テスト定数KEYを書き換え、
後続のプロンプトテストへ漏れていた。台本fixtureをdeepcopyに修正した。

```sh
python3 -m unittest tools.tests.test_emu_playbook > /tmp/emu_playbook_green1b.txt 2>&1
```

```text
Ran 15 tests in 0.006s
OK
```

### 2. 実キー・Mouseヘルパーへの接続

`LiveDriver` がまだスタブの状態で、ペース送信・座標換算・応答失敗時の即時停止・
途中操作失敗からの後続遮断のassertionを追加。

```sh
python3 -m unittest tools.tests.test_emu_playbook > /tmp/emu_playbook_red2.txt 2>&1
```

```text
AttributeError: 'LiveDriver' object has no attribute 'preflight'
Ran 19 tests in 0.026s
FAILED (errors=24)
```

私有名前空間で既存ヘルパーを再利用し、全POSTの厳格な成功応答を確認する実装を追加。
最初の試行は `FAILED (failures=1, errors=23)`。偽POSTが再束縛先に存在しない
`copy` グローバルを参照していたため、fixture側でdeepcopy関数をクロージャへ捕捉した。

```sh
python3 -m unittest tools.tests.test_emu_playbook > /tmp/emu_playbook_green2b.txt 2>&1
```

```text
Ran 19 tests in 0.017s
OK
```

### 3. 観測変換前の失敗・コンテキスト分離・ローカル限定

TVRAMが失敗JSONを空文字に変換する場合、screenshotが失敗JSONをBMPとして保存する場合、
元のCTXが変わる場合、非ローカルLLM URL、途中での画面寸法変更をテスト追加。

```sh
python3 -m unittest tools.tests.test_emu_playbook > /tmp/emu_playbook_red3.txt 2>&1
```

```text
Ran 25 tests in 0.041s
FAILED (failures=7, errors=1)
```

観測の生JSON検証、BMPヘッダー検証、私有CTX、ループバックLLM URL検証を追加。

```sh
python3 -m unittest tools.tests.test_emu_playbook > /tmp/emu_playbook_green3.txt 2>&1
```

```text
Ran 25 tests in 0.042s
OK
```

### 4. 矛盾した応答と起動失敗の記録

`ok:true` と `error` が共存する応答の拒否、factory起動例外の失敗終了と記録を追加。
正常prefix後のドライバー例外、最終done欠落、preflight例外、本番factoryの結線と
汎用ACTIONS/parse_action迂回禁止も確認。

```sh
python3 -m unittest tools.tests.test_emu_playbook > /tmp/emu_playbook_red4.txt 2>&1
```

```text
Ran 29 tests in 0.044s
FAILED (failures=3, errors=1)
```

矛盾したACKの拒否と起動例外のrejection記録を実装。

```sh
python3 -m unittest tools.tests.test_emu_playbook > /tmp/emu_playbook_green4.txt 2>&1
```

```text
Ran 29 tests in 0.041s
OK
```

### 5. 拒否ログに失敗したドライバー結果を保持

最終レビューで、失敗理由だけでなくドライバーの返却値そのものを `result` に
残すassertionを先に追加。

```sh
python3 -m unittest tools.tests.test_emu_playbook.FinalBoundaryTests.test_rejection_retains_failed_driver_result > /tmp/emu_playbook_red5.txt 2>&1
```

```text
AssertionError: None != 'error: input refused'
Ran 1 test in 0.001s
FAILED (failures=1)
```

各ステップで結果を初期化し、拒否時に得られた結果を保持する実装へ変更。

```sh
python3 -m unittest tools.tests.test_emu_playbook -v > /tmp/emu_playbook_final_tests.txt 2>&1
```

```text
Ran 30 tests in 0.042s
OK
```

## 振る舞いとassertionの対応

| 要件 | テスト（`test_`以下）/確認内容 |
|---|---|
| 事前に台本全体を検証 | `entire_plan_invalid_tail_rejected_before_calls`: 正常prefixの後ろに違反があってもRunner構築時拒否 |
| 厳格JSON・重複キー・型 | `strict_json`, `recursive_type_sensitive_equality`: ネストした重複キー、複数object、説明文、NaN/Infinity/overflow、bool/int/float/string差を拒否 |
| 台本外操作は実行前拒否 | `bad_proposals_zero_driver_calls_and_latched`: 実際に観測されたv86 `cmd_nowait`、`CTRL+STOP`、未知操作、余分/不足/異なる引数、順序違いに対しdriver呼び出しリストが空 |
| 途中から逸脱しても後続停止 | `failure_after_prefix_no_later_execution`: 反復、early done、壊れたJSON、CTRL+STOP後に正しい応答を用意しても読まず、再runも実行しない |
| doneは最後に完全一致 | `full_order_done_and_transcript`, `done_report_must_match`, `no_final_done_never_success_and_exception_preflight` |
| ドライバー失敗・例外に再試行なし | `driver_failure_and_exception_stop_without_replay`, `driver_exception_after_valid_prefix_blocks_replay` |
| LLM失敗に再試行なし | `llm_exception_and_tool_calls_abort`: tool callsも拒否 |
| 台本・状態の改変防止 | `400_bounds_and_immutable_snapshot`, `plan_and_messages_mutation_cannot_change_authorization`, `session_latch_and_progress_are_read_only` |
| 宣言寸法と実寸法の照合 | `geometry_mismatch_or_invalid_before_gui`, `changed_geometry_after_valid_prefix_stops`, `live_geometry_and_observations_use_existing_helpers`: 400/480、幅不一致、bool/float寸法も確認 |
| 小さい専用プロンプト | `prompt_is_limited_to_plan`: 台本は含み、広い汎用操作指示は含まない |
| 正常な4文字ペースとMouse順序 | `real_helpers_pacing_and_mouse_no_extra_release`: 4/4/2文字、0.35秒×3、端点65535、releaseは1回 |
| 部分操作後にも追加送信なし | `ack_failure_stops_inside_paced_key_or_gesture`, `production_loop_with_real_helper_partial_failure`: 第2文字チャンク失敗後の第3送信、次の操作、自動releaseがない |
| 生の観測失敗を隠さない | `raw_observation_failure_cannot_be_hidden_by_helper`, `actual_observation_helpers_success_and_private_context` |
| 本番CLIを偽LLM/driverで実行 | `execute_production_loop_and_invalid_file_no_factory`, `dry_run_default_no_live_factory`, `cli_bad_json_and_existing_transcript_no_live_calls` |
| 本番結線の迂回禁止 | `live_factory_and_agent_actions_table_are_not_dispatcher`: ACTIONSとparse_actionにtrapを置いても正規ループだけが動く |
| 失敗した返却値も記録 | `rejection_retains_failed_driver_result`: expected/proposal/rejection/resultを照合 |
| 初期化失敗も記録 | `startup_failure_returns_failure_and_transcript` |

## 最終検証と制限

```sh
python3 -m unittest tools.tests.test_emu_playbook -v
python3 -m py_compile tools/emu_agent/playbook.py tools/tests/test_emu_playbook.py
```

30テスト成功。`py_compile` は出力なし、終了コード0。
エミュレータ・実LLM・実際の画面/入力配送は未検証。
カーネルビルド、ゲスト試験、配備はこのホスト限定作業では実施していない。
BMPヘッダーの確認や台本完走は、画像内容やGUI結果が正しいことの証明ではない。
部分ジェスチャーの失敗後も自動releaseしないため、復旧入力は別途ホスト判断が必要。


## 2026-09-09 独立レビュー指摘の修正（旧RED履歴は上記のまま保存）

対象は `tools/emu_agent/playbook.py`、`tools/tests/test_emu_playbook.py`、
本記録、`tools/emu_agent/README.md` の4ファイルのみ。

### 実ソースで確認した契約

`tools/np21w_mcp/np21w_client.py` の `_direct_available` は最初のstatus例外を
捕捉して `_direct=False` とし、`get` / `get_to_file` がWindows curlへfallbackする。
従来のprivate observation bindingでも、この内部の再試行相当の通信を防げなかった。
`tools/np21w_mcp/server.py` の説明に加え、実サーバーの
`~/np21w-src/src/win9x/aidebug/aidebug_api.cpp` の `handle_status`
（214〜261行）を読んで次を確認した。

- `ok` はJSON true。`running` はuser pauseまたはtrap pauseで0、そうでなければ1。
- `user_pause` / `trap_pause` / `fault_generation` は `kv_int` による整数。
  `fault_generation` は回復不能fault発生後に非ゼロになる。
- `scrn_xmax` / `scrn_ymax` も整数。`paused` というフィールドは使わない。

入力の契約は `ok is True`、整数 `running=1`、整数 `user_pause=0`、整数
`trap_pause=0`、整数 `fault_generation=0`、整数の画面寸法と台本の一致。
read-only診断には安全状態条件を課さず、通信・応答形式だけを検証する。

### assertion REDを先に実行

実装に触れる前に `ReviewTests` の4メソッドを追加し、次を実行した。

```sh
python3 -m unittest tools.tests.test_emu_playbook.ReviewTests > /tmp/emu_playbook_review_red.txt 2>&1
```

```text
Ran 4 tests in 0.462s
FAILED (failures=105)
```

| 指摘 | REDの実結果 |
|---|---|
| 安全状態 | `test_safe_state_contract_before_all_input`: 4状態項目×8不正値（欠落を含む）×key/click/drag = 96件で `True is not false`。危険な状態でも完走していた |
| 隠れたfallback | `test_first_transport_failure_is_single_attempt`: 入力3種と観測3種の6件で `fallback.assert_not_called()` が失敗。実client関数が偽Windows curlを呼んだ |
| 部分入力の記録欠落 | `test_drag_failure_records_completed_prefix_and_uncertainty`: negative ACK、不正JSON、通信例外の3件で `None is not an instance of dict`。press後の失敗詳細を失っていた |
| 診断観測を維持 | `test_diagnostic_status_allows_unsafe_state`: RED時から成功。fault/trap停止状態でも読み取り専用診断は可能 |

実clientはASTから関数・例外クラスだけを抽出し、import時のBASE環境変数読み取りを
実行しない。実agentの観測関数もAST抽出。実 `gui_gate.post` / key / Mouseを使い、
urllibのHTTP境界、HTTPConnection、sleep、LLMを偽物に置換した。
したがって単なるFakeDriver試験ではなく、Runner→LiveDriver→実ヘルパー→偽HTTPの
統合経路で、旧clientのprobe/fallbackまで実際に再現した。

### GREENと追加回帰確認

- 観測用 `_single_get` を追加。http.clientで1回だけGETし、probe / fallback /
  redirect / proxy環境変数参照をしない。BMPはヘッダー確認後に元バイト列を保存し、
  TVRAMの既存変換を維持。共有clientに変更なし。
- preflightに実APIの安全状態契約を追加。失敗時は入力ゼロ。
- 各入力ステップのPOST journalを追加。成功ACK済みprefixと失敗した試行、
  許可したACK項目・固定エラー分類を保持する。任意の応答文字列や例外本文は破棄。
  完了ステップ数とPOST試行数を分離し、部分実行とボタン状態の不確実性を明示。
  自動release、後続入力、再runによる再送は行わない。

```sh
python3 -m unittest tools.tests.test_emu_playbook.ReviewTests > /tmp/emu_playbook_review_green.txt 2>&1
```

```text
Ran 4 tests in 0.427s
OK
```

既存fixtureの正常statusに安全状態項目を追加し、旧raw観測試験の偽通信を
新しい観測境界へ移した。旧30テストを削除・skipしていない。
さらに正常keyの「台本2ステップ（key+done）対POST3回」、status成功ACKと
画面寸法の拒否、観測成功時の余分なstatus probeゼロ、prefixの正確な引数・ACKと
固定エラー分類を検証した。これでplaybook全36テスト。

### 最終実行結果と範囲外の失敗

```sh
python3 -m unittest tools.tests.test_emu_playbook -v > /tmp/emu_playbook_review_final.txt 2>&1
python3 -m unittest discover -s tools/tests -p 'test_*.py' -v > /tmp/emu_playbook_review_discover.txt 2>&1
python3 -m py_compile tools/emu_agent/playbook.py tools/tests/test_emu_playbook.py
```

- playbook: `Ran 36 tests in 0.516s` / `OK`。
- tools/tests全体: `Ran 97 tests in 0.997s` /
  `FAILED (failures=1, errors=1, skipped=1)`。94成功。
- 範囲外の `test_np21w_ini_live.ReceiptAndPathBoundaries`:
  `test_review_counterexample_rejected_before_first_stop` は `IniError not raised`。
  `test_receipt_last_accepted_and_first_rejected_byte_sizes` は
  `IniError: receipt exceeds snapshot size limit; refusing before stop`。
  この2件は修正していない。
- skip 1件は明示opt-inの実PowerShell fixture。実行していない。
- py_compileは出力なし、終了コード0。

全体試験のini関連入力も合成fixtureのみ。実ネットワーク、エミュレータ、LLM、
実ini・env・秘密情報の呼び出し／読み取り、配備、追加エージェントは使用していない。
ゲスト操作はPMレビュー完了まで実施しない。ACKは入力の受付であり、実際のGUI結果や
ゲスト配送の確認ではない。preflight後の状態変化を原子的に防ぐ契約でもない。

## 2026-09-09 残存POSTリダイレクト阻害要因の修正

変更は playbook.py、playbookテスト、本TDD記録、emu_agent READMEの4ファイル。
上記の旧RED・全体試験のini関連2失敗・skipの記録は保存した。
実ネットワーク、エミュレータ、実LLM、ini、env、秘密情報、追加エージェント、
ゲスト操作は使用していない。今回の試験対象はplaybookのみ。

### 実装変更前のRED

現行コードは観測GETだけが `_single_get` の直接通信で、入力POSTはprivate bindingした
`gui_gate.post` → `urllib.request.urlopen` の自動リダイレクトを許していた。
`RedirectTransportTests` を先に追加した。urlopen自体はmockせず、実urllib openerの
HTTPHandler.http_openをインメモリ応答に置換し、HTTPRedirectHandlerと
HTTPErrorProcessorは実物を通す。ProxyHandler({})によりproxy環境変数を参照しない。
http.client側も同じ偽応答を返すConnectionに置換し、ソケット通信は行わない。

テストfixture構築途中の失敗も区別して保存:

- `/tmp/emu_playbook_redirect_red.txt`: 30 assertion失敗。opener構築後のhandler差し替えで
  旧bound methodが残り、偽Connectionの不足によりPOST応答まで到達しなかった。
- `/tmp/emu_playbook_redirect_red_handler.txt`: 50 errors。偽Connectionにdebuglevelが不足。
- `/tmp/emu_playbook_redirect_red_valid.txt`: 50 errors。偽Connectionに_http_vsnが不足。
- `/tmp/emu_playbook_redirect_red_final.txt`: 30 assertion失敗。urllibの要求準備に必要な
  _get_content_lengthが不足。これらはリダイレクト不具合を実証したREDとは扱わない。

偽ConnectionをHTTPConnectionから派生させ、request/getresponse/closeを置換した後、
**playbook.pyの変更前**に有効なREDを得た:

```sh
python3 -m unittest tools.tests.test_emu_playbook.RedirectTransportTests -v > /tmp/emu_playbook_redirect_red_assertions.txt 2>&1
```

```text
Ran 4 tests in 0.342s
FAILED (failures=15)
```

301/302/303それぞれ、先頭入力3種と途中入力2種が失敗（3×5=15）。
実POSTの後にLocation先 `/api/status` へのGETが加わり、その200/ok:trueで
後続のキー・マウス送信まで進むことを通信履歴のassertionで実証した。
307/308、他の非成功HTTPステータス、GETのリダイレクト拒否はRED時から成功。

### 修正と検証

- `_single_request` をGET/POSTで共用。POSTのフォーム符号化とContent-Typeを維持し、
  redirect / proxy / probe / fallback / retryを使わない。
- POSTはHTTP 2xxと厳格な成功ACKの両方が必要。非2xxのコードは失敗操作に残す。
  `ok:true` のリダイレクト本文を成功prefixへ追加せず、後続送信も行わない。
- 共有gui_gateは変更せず、実key/Mouseのprivate bindingで分割と待機順を維持。
- 初回GREEN試行 `/tmp/emu_playbook_redirect_green.txt` は40テスト中1失敗。
  通信例外をRejectedへ変換したことで `invalid_ack` に誤分類した回帰を既存試験が検出。
  POST通信部分で `transport_failure` を明示し、40テスト成功へ修正した
  （`/tmp/emu_playbook_redirect_final_tests.txt`）。
- 最終5メソッドは、5種類のredirect×先頭key/click/drag、
  5種類×途中key/dragの正しい成功prefix保持、199/300/304/400/500の拒否、
  GETの5種類×入力preflight/status/tvram/screenshot、HTTP 200/201/299の成功と
  `SHIFT+SPACE` のフォーム符号化を検証。失敗後のrun再呼び出しでも通信しない。
  先頭POST失敗時の成功prefix空、失敗操作・試行数・不確実状態保持も確認した。

```sh
python3 -m unittest tools.tests.test_emu_playbook -v > /tmp/emu_playbook_redirect_final_41.txt 2>&1
python3 -m py_compile tools/emu_agent/playbook.py tools/tests/test_emu_playbook.py
```

```text
Ran 41 tests in 0.793s
OK
```

py_compileは出力なし、終了コード0。従来のキー4文字分割、0.35秒待機、Mouseの
座標換算・move/press/release順と待機を含め全playbookテスト成功。
範囲外のini試験、カーネルビルド、配備、ゲスト検証は実施していない。
ACKは受付応答であり、ゲスト配送やGUI結果を検証したことにはならない。
