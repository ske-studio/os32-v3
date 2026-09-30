# emu_agent — ローカル小型 LLM に OS32 実機を操作させる

`flm serve` (FastFlowLM, OpenAI 互換 API, `127.0.0.1:52625`) 上のモデルに
NP21/W (ai-debug 版) の HTTP API 経由で OS32 のシェルを触らせる最小ドライバ。

```
python3 tools/emu_agent/agent.py run "ver を実行してバージョンを報告して"
python3 tools/emu_agent/agent.py run "..." --model gemma4-it:e4b --max-steps 12
python3 tools/emu_agent/agent.py suite tools/emu_agent/tasks/regress.txt   # 定型回帰
python3 tools/emu_agent/agent.py suite tools/emu_agent/tasks/v86_dos.txt   # DOS 操作
python3 tools/emu_agent/agent.py tail            # 直近セッションのログ末尾
```

`suite` はタスクファイル (`#` コメント、空行区切りで 1 タスク) を順に実行し、
SUMMARY 表と全体の `RESULT:` を出す。

最後に `RESULT: {...}` を 1 行出す (`ok` / `report` / `steps` / `session`)。
ステップごとの生ログは `tools/emu_agent/logs/<session>/steps.jsonl`。

## 設計

- **ツール定義は system prompt の平文 + JSON 1 個の返答**。FLM のネイティブ
  function calling はモデルごとに挙動が違うので依存しない (`tool_calls` が
  返ってきた場合は受ける)。
- 実機側の行動は `cmd` (`/api/cmd`) / `key` (`/api/key`, 値は URL エンコードするので
  `CTRL+STOP` がそのまま書ける) / `tvram` / `status` / `done`、および
  `cmd_nowait` (V86 セッションなど返ってこないコマンドを投げっぱなし) /
  `wait` (最大 60 秒) / `screenshot` (`logs/<session>/shots/stepNN.bmp` に保存) /
  `selftest` (kernel.map で `kselftest_pass/fail` を引いて `/api/mem` を読む)。
- ホスト側の行動は **許可リスト方式** (自由なシェルは渡さない):
  `make` (target は kernel/programs/sdk/all/check/clean のみ) /
  `hotdeploy` (`make hotdeploy FILE=<repo 相対 .bin>`) /
  `deploy` (`os32-cycle deploy` = 停止→NHD 配備→起動→ver。[D1] を機構で守る)。
  ログは RESULT 行・error 行・末尾 15 行に絞って渡し、モデルには
  「解釈せず exit= と RESULT: を引用せよ」と指示している。
- 実機出力は 1600 文字で切って渡す (4B 級モデルの文脈を守るため)。
- HTTP 層は `tools/np21w_mcp/np21w_client.py` を流用 (WSL 直叩き → curl.exe
  フォールバック)。`/api/cmd` のタイムアウトは 90s ([V3])。

## 上位モデルのコンテキストを守る使い方

- `--quiet` (MCP 経由の既定) は `RESULT:` 1 行しか返さない。行動と観測は
  `steps.jsonl` にあるので、裏取りは `grep` で該当行だけ読む。
- 単発コマンド 1 つなら curl 直叩きの方が安い。委譲が効くのは「待ち」や
  多段の手順 (ブート → キー → 画面 → 脱出、ビルド → 配備 → 検証) を
  1 呼び出しに畳めるとき。

## 注意

- **未ロードのモデル名を渡すと flm が自動ダウンロードを始める。**
  `/v1/models` に並んでいるだけでは「取得済み」の保証にならない。
- FLM はリクエストを直列処理する。タイムアウトで切った側のリクエストも
  サーバ側では最後まで走るので、待たされたら数分置く。
- **chat が長く無応答なら flm serve 自体が落ちている可能性が高い**
  (`/v1/models` だけ返ることもある)。flm のコンソールを確認して再起動する。
- モデルの `report` は自己申告。合否の判断は `steps.jsonl` の観測 (`obs`)
  を人間/上位モデルが読んで行う。
- 2026-09-04 時点で `qwen3.5:9b` は FLM/NPU 上で記号の羅列しか出さない
  (ツール無しの平文でも同じ)。`gemma4-it:e4b` は正常。

## 委譲実績 (gemma4-it:e4b, 2026-09-04)

| タスク | ステップ |
|---|---|
| ver / ファイル作成→cat→ls / cd エラー→pwd / 画面読取 | 2〜5 |
| vdos.bin の所在調査 (/bin, /usr/bin, /sys, /host/bin) | 6 |
| `make check` | 2 |
| wc.bin の hotdeploy → `echo hello \| wc -c` で検証 | 3 |
| `os32-cycle deploy` → ver | 2 |
| DOS ブート (cmd_nowait) → dir → CTRL+STOP → ver (`tasks/v86_dos.txt`) | 11 |
| 定型回帰 6 本 (`tasks/regress.txt`: selftest / klibc_test / alloc_demo / ring3_fault / パイプ / screenshot) | 2〜3 ずつ |

## MCP

`tools/emu_agent/mcp_server.py` を `.mcp.json` に登録済み
(`emu_agent_run` / `emu_agent_suite` / `emu_agent_tail`)。ゲームの自動プレイは
os32-game 側の `tools/autoplay` (同じ flm/gemma 構成) で、`.mcp.json` の
`autoplay` エントリは `../os32-game/tools/autoplay/mcp_server.py` を指す
(隣に clone してある前提)。Claude Code から「ローカルモデルに
このタスクをやらせて結果を見る」ために使う。

## 厳格な GUI playbook コマンド

ホストが承認した JSON 台本の操作・引数・順番だけを実行する場合は、独立した
`playbook.py` を使う。事故のあった **`/tmp/os32_local_gui.py` は再使用禁止**。
このコマンドは `agent.run` / `ACTIONS` / 寛容な `parse_action` を使わない。

```sh
python3 tools/emu_agent/playbook.py /tmp/approved-gui.json
python3 tools/emu_agent/playbook.py /tmp/approved-gui.json --execute \
  --model gemma4-it:e4b --transcript /tmp/gui-playbook-session.jsonl
```

既定は **dry-run**。台本全体の検証と表示だけで、LLM・エミュレータ・設定への
アクセスは行わない。`--execute` でのみ既存のローカル `llm_chat` を呼ぶ。
`FLM_URL` はループバック URL のみ受け付ける。実行時の通信先は既存クライアントの
`NP21W_AIDEBUG_URL` をキー・マウスにも適用する。
strict runnerの観測GETとGUI入力POSTは専用の単発HTTP通信を使う。共有クライアントの
`_direct_available` / `get` / `get_to_file` を通さず、接続プローブ、再試行、
Windows curlへのfallback、proxy環境変数参照、HTTP redirect追従は行わない。
直接接続できなければ最初の失敗で停止する。入力は `gui_gate.post` を通さず、
観測と同じ `http.client` の通信経路を使う。共有クライアント・gui_gate自体は変更しない。
POSTはHTTP 2xxかつ厳格なJSON ACKの `ok: true`（errorなし）の場合だけ成功とする。
301/302/303/307/308を含む非2xxは、本文が `ok: true` でも拒否し、
`Location` は追従しない。GETはHTTP 200のみ受け付ける。
TVRAMは既存の行末空白除去と改行結合、画像はBMP保存形式を維持する。

台本例（JSON のコメントは不可。ホストが内容を確認したファイルを指定する）:

```json
{
  "width": 640,
  "height": 480,
  "steps": [
    {"action": "click", "x": 30, "y": 468},
    {"action": "key", "seq": "ESC"},
    {"action": "screenshot"},
    {"action": "done", "report": "OK"}
  ]
}
```

トップレベルは `width` / `height` / `steps` のみ。既存 `Mouse` の横座標換算に合わせ
幅は640、高さは2以上の整数（例:400/480）。座標は宣言した画面内の整数に限定する。
各キー・クリック・ドラッグの直前に、読み取り専用の `/api/status` で実際の
次の安全状態契約を確認する。GUIへの自動移行などは行わない。

| `/api/status` の必須項目 | 入力を許可する値 |
|---|---|
| `ok` | JSON boolean `true` |
| `running` | JSON integer `1` |
| `user_pause`, `trap_pause`, `fault_generation` | 各JSON integer `0` |
| `scrn_xmax`, `scrn_ymax` | JSON integerで台本の幅・高さと一致 |

欠落、null、文字列、booleanでの整数代用、小数、危険値は入力前に拒否する。
契約はNP21/Wの `src/win9x/aidebug/aidebug_api.cpp` の `handle_status` に基づく。
`running` はユーザー停止またはtrap停止で0となり、`fault_generation` は
回復不能faultの発生ごとに増える。存在しない `paused` フィールドは使わない。
`status` / `tvram` / `screenshot` の読み取り専用診断には安全値の条件を課さない
（通信や応答形式の失敗では停止する）。状態確認は各入力ステップ直前の観測であり、
その後のゲスト状態を固定するものではない。

| 操作 | 必須フィールド（`action` 以外） |
|---|---|
| `key` | 空でない文字列の `seq` または `text` の片方だけ |
| `click` | 整数 `x`, `y`（左クリック） |
| `drag` | 整数 `x0`, `y0`, `x1`, `y1`（左ボタン） |
| `status`, `tvram`, `screenshot` | なし |
| `wait` | 整数 `seconds`（1〜60） |
| `done` | 文字列 `report`。最後に必ず1回だけ |

追加・不足フィールド、型変換、自由なコマンド・配備・reset操作は受け付けない。
キーも台本との完全一致が必要で、台本外の `CTRL+STOP` などは送信前に拒否する。
モデルの返答は厳格なJSONオブジェクト1個のみ。重複キー、前後の説明、コードフェンス、
複数オブジェクト、tool calls、順番違い、省略、反復、早すぎる `done` は直ちに停止する。
最終 `done` の `report` も台本と完全一致しなければならない。

台本は検証後に不変の独立コピーへ固定する。プロンプトはこの台本から作り、
既存の汎用指示文を継承しない。既存の4文字/0.35秒ペース送信と `Mouse` を使い、
各送信の応答を確認してから次の送信へ進む。失敗・例外時はセッションを停止状態に固定し、
再試行・再送・自動releaseなどの復旧入力を行わない。途中でボタンが押されたままに
なる場合もあり、必要な復旧は別途ホストが判断する。

実行ログは指定した新規JSONLファイル、または
`tools/emu_agent/logs/playbook-<時刻>-<識別子>/steps.jsonl` に記録する。
既存ファイルは上書きしない。`expected` / `proposal` / `preflight` / `result` /
`rejection` の各イベントに期待手順・提案・結果または拒否理由を残す。
入力成功イベントの `side_effects` と入力失敗イベントの `result` には、
`completed_suboperations`（成功ACKを得たPOSTの順序・引数）、
`attempted_operation`（失敗したPOST）、`attempted_side_effects`（POST試行数）を残す。
ACKはbooleanの `ok` と `error_present` のみ保存し、エラーは
`negative_ack` / `invalid_ack` / `transport_failure` に分類する。
HTTPエラーではステータスコードも残す。任意の応答本文や例外メッセージは保存しない。
途中失敗は `partial_execution` / `button_state_uncertain` と
`last_acknowledged_buttons` で明示する。例えばpress成功後のmove失敗なら、
成功したmove・press、失敗したmove、最後のボタンACK値1を残し、releaseは送らない。
`steps` は完了した台本ステップ数（終端doneも含む）で、POST数やゲストへの配送保証ではない。
スクリーンショットは同セッションの `shots/stepNN.bmp`。
`ok: true` は全手順と終端 `done` の実行完了を意味し、画面内容の正しさの証明ではない。

ホスト試験とRED→GREENの記録:
[`../tests/test_emu_playbook.py`](../tests/test_emu_playbook.py)、
[`../tests/emu_playbook_tdd.md`](../tests/emu_playbook_tdd.md)。

今回の検証はホスト限定。ゲスト操作はPMレビュー完了まで実施しない。
