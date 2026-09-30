# NP21/W controlled trial ini — ホスト TDD 記録 (2026-09-09)

対象は新規 `tools/np21w_trial.py`、`tools/tests/test_np21w_trial.py`、この記録と
`.claude/skills/os32-emu-config/SKILL.md` のみ。既存 live/offline/playbook は変更していない。
実エミュレータ・CIM・実 ini・ネットワーク・env/secret・docs/hw へのアクセス、
OS ビルド・配備・NHD コピー・commit・agent 起動は実施していない。

## ソース根拠と承認された差

読取対象は `~/np21w-src/src/win9x/` のソース。

| 根拠 | 意味 |
|---|---|
| `np2.cpp:3318` WM_CLOSE、WM_DESTROY | 通常終了。確認ダイアログがあり得る。CloseMainWindow の成功応答だけでは終了証明にならない |
| `np2.cpp:4999–5000` | sys_updates に応じて initsave。終了に伴う baseline 更新を拒否する旧方式とは別の trial 手順が必要 |
| `ini.cpp:943` initgetfile | 明示指定がなければ modulefile の stem + `.ini`。これは bare 起動時の既定導出であり、現在選択中の ini の証明ではない |
| `np2arg.cpp` Np2Arg::Parse | `.ini` の positional 絶対パスを受理する |
| `ini.cpp:628–629` | USEGD5430 / GD5430TYPE の登録。91=Xe10 は既存 offline の証明範囲を再利用 |
| `ini.cpp:848` | `e_resume` は PFTYPE_BOOL、np2oscfg.resume に対応 |
| `np2.cpp:4655–4658` | resume 有効時は保存状態をロードするため trial だけ false にする |
| `np2.cpp:750–780,4961–4965` | resume state の名前は initgetfile の stem に従い、通常終了時に保存/削除される |
| `np2.cpp:4402–4404` | modulefile のディレクトリを基準にする。新 launch にも明示 cwd を記録 |

通常終了で設定や状態が保存される可能性、cold start、新しいプロセス・trial 名・明示 cwd、
trial 内の `USEGD5430=true` / `GD5430TYPE=91` / 必要時 `e_resume=false` が許容差。
VM RAM の厳密な保存は保証しない。共有ディスクに対する通常のゲスト動作は隔離しない。
ツールは終了後の baseline を read-only で扱い、そのバイト列を限定変換して
新規専用ファイルへ書く。終了前の baseline の保存・巻き戻しはしない。
`e_resume` を含む必須キーが欠落・重複・未知値なら停止後に拒否し、追記や推測をしない。

## 実際の RED → GREEN

共通コマンド:

```bash
python3 -B -m unittest discover -s tools/tests -p test_np21w_trial.py -v
```

| 段階 | 実出力 | 内容 |
|---|---|---|
| A RED | Ran 12 tests / FAILED (failures=21, errors=2), exit1 | 新規 no-op skeleton に先行テストを実行。変換、ゲート、順序、失敗停止、PS 契約の assertion failure。errors は空の CLI JSON 出力と未生成 trial キーの検査であり import error ではない |
| B GREEN | Ran 12 tests / OK, exit0 | 固定計画、strict proposal gate、通常終了、終了後 snapshot、CreateNew、明示起動と再照合を実装 |
| C RED | Ran 15 tests / FAILED (failures=1), exit1 | transport の close が失敗しても ok=true のままになる反例。追加した他2件（CLI LLM gate、途中/新プロセス識別不一致）は初回 GREEN |
| D GREEN | Ran 16 tests / OK (skipped=1), exit0 | executor の context 終了後にのみ成功を確定。parser opt-in fixture を追加（通常は skip） |
| E 回帰 | 全体 Ran 120 tests / OK (skipped=2), exit0 | 承認フラグ・固定変更集合の追加テストは初回 GREEN。ゲート不一致ケースをそれぞれ新しい単回束縛で実行するよう改善 |

E のログは作業時 `/tmp/np21w-trial-all.log`。最終 trial は17件（16成功、parser 1 skip）。
RED を後から全テスト固有の RED として主張しない。

## モック統合が証明すること

実 Python の `bind_trial → _run → WindowsExecutor → wire` を通し、transport だけを偽物にした。
模擬ファイルはメモリ内の合成 bytes。old process の command は bare / 古い explicit ini /
未知の旧起動形式でも使用中 ini の証拠にせず、操作者の PID・生成時刻・exe を照合する。

成功時の送信順は厳密に以下を assertion で確認:

```text
lock, preflight, query, query, close, query,
snapshot, query, create, query, verify, start, query, query, dispose
```

- close で模擬 baseline を「通常終了が保存した新しい内容」に変更する。
  snapshot は process が残っていると失敗する。trial がその終了後 bytes の限定変換結果で、
  baseline は終了後 bytes のままであることを確認。
- 14回の各 exchange に失敗応答を注入し、後続 operation が一切ないことを確認。
  close ACK が成功しても process が残れば snapshot / create / start に進まない。
- 別 PID、別生成時刻、別 exe、複数/欠落/壊れた照会、close 前の PID 再利用を拒否。
  起動後 command / creation の不一致、transport cleanup 失敗でも ok=false。
- 不承認・モデルによる計画変更・未知操作・JSON 重複/余分なテキストは executor 構築前に拒否。
  ネストした承認も JSON で固定し、モデルには別コピーを渡す。
  CLI execute のモック LLM がコピーを書き換えても本来の束縛を変更できない。
  成功/失敗/不一致すべて単回で、復元・再試行はない。
- opaque CP932 bytes、BOM、コメント、空白、混在改行を保持。
  resume が既に false ならそのバイトは変えず、3キーが目標値でも新規 trial lifecycle は実行する。
- PS 本文には元の Kill / File.Replace dispatcher を含めない。
  既存の Query / AssertProcess / CheckPath / Snapshot / NewFile、byte wire、transport exchange
  を再利用。transport の初期化は private namespace に新 PS_SERVER を束縛し、既存 globals は不変。

PS 契約の文字列 assertion は API 実動作の検証ではない。
`completed` は成功応答を受けた operation の記録であり、後続の内容検査が成功した証明ではない。
失敗した操作が部分的に作成/起動した可能性は stage と trial パスから調査する。
例外原文、旧 command、ini 原文は出力しない。生成済み trial や起動済み process の自動清掃はしない。

## PowerShell の構文解析だけを試した結果

```bash
python3 -B tools/tests/test_np21w_trial.py --windows-parser PowerShellParser -v
```

実際の生成 PS_SERVER を `/tmp` の TemporaryDirectory 内 `source.txt` に書き、stdin の
**データ**として渡す。実行する PS は `Parser.ParseInput` と error count 出力だけ。
ライフサイクル本文の実行、CIM、実ファイル操作をする経路はない。一時 fixture は清掃済み。

実結果は exit1、Ran 1 test / FAILED (failures=1):

```text
WSL (5 - ) ERROR: UtilBindVsockAnyPort:309: socket failed 1
```

PowerShell 起動前の環境失敗であり TDD RED には数えない。構文解析、埋め込み C#、
CloseMainWindow / CIM / NTFS API の実検証は未実施。制限回避や再試行はしなかった。

その他の実施結果:

```bash
python3 -B -m unittest discover -s tools/tests -p 'test_*.py' -v
# Ran 120 tests / OK (skipped=2)
python3 -B ~/.codex/skills/.system/skill-creator/scripts/quick_validate.py .claude/skills/os32-emu-config
# Skill is valid!
python3 -B tools/np21w_trial.py --help
# exit0
```

新規 Python 2ファイルは `py_compile.compile(..., doraise=True)`、cfile を
/tmp TemporaryDirectory に置いて検査・清掃。`py_compile: OK (2 files)`、exit0。
全体の skip2 は既存 live の Windows fixture と新 trial の Windows parser。

## 操作者の具体的実行コマンド（今回は未実行）

候補 exe/baseline はユーザー指定。実プロセスを照会していないため PID・生成時刻を
捏造しない。下の `SELECTED_PID`、`SELECTED_CREATION_UTC`、`LOCAL_MODEL_ID` は、
操作者が選んだ値に置換する。生成時刻は CIM の UTC `ToString('o')` と完全一致が必要。
選んだ exe が確認したソースに対応することと専有を操作者が確認する。

```bash
python3 -B tools/np21w_trial.py \
  --exe '$NP21W_DIR\np21x64w.exe' \
  --baseline '$NP21W_DIR\np21x64w.ini' \
  --cwd '$NP21W_DIR' \
  --pid SELECTED_PID --created 'SELECTED_CREATION_UTC' \
  --model 'LOCAL_MODEL_ID' \
  --llm-url 'http://127.0.0.1:1234/v1/chat/completions' \
  --execute --exclusive-operator
```

`--execute` を外すと外部呼出しゼロの dry-run。実行時は新しい固有 trial 名を含む計画を
CLI ホストが固定し、local LLM の完全一致提案を得てから executor を構築する。
URL/モデルはローカル OpenAI 互換サービスの実設定に合わせる。API key や env は使わない。
正常0、拒否/失敗2、CLI引数の誤りは argparse の2。出力 JSON に計画・限定差分・cwd・
新しい process identity・失敗 stage を記録する。ゲストの Cirrus 反映試験は別途必要。

プログラム経由では操作者が `make_plan()` の結果そのものを承認して保持し、
`bind_trial(plan, WindowsExecutor, authorized=True, exclusive=True)` の返す callable だけを
公開する。モデルから受け取る strict JSON は1つの全計画のみ。任意の shell/action registry、
既存 `emu_agent` への自動登録、別工程の NHD 配備は追加していない。

## 2026-09-14 追記 — 票 S3I2-T: 変更集合を disk 側へ (`hdd` / `fdd_eject` / `fdd_arg`)

trial が扱うキーは **Cirrus 2 キー → `HDD1FILE` (+ `FDD1FILE` / `FDD2FILE`)** に変わった。
`e_resume=false` の強制と、欠落 / 重複 / 未知値の拒否は**そのまま**で、承認計画にも残る
(`changes` に `e_resume: 'false'` が入り、`transform_trial(raw, changes)` が要求する)。
Cirrus 系キーは計画に含めない = **変更集合は明示したキーだけ**なので、baseline の
`USEGD5430` / `GD5430TYPE` / `ExMemory` が何であっても trial は値を検査せず触らない。

- `make_plan(..., hdd, fdd_eject, fdd_arg=None)`。`hdd` と `fdd_arg` は `NP21W_DIR` 直下の
  名前で受け、計画には `hdd` = 名前、`fdd_arg` = 起動引数に使う絶対パスとして載る
  (`action` は `cirrus-trial` → `disk-trial`)。`_validate_plan()` は `fdd_arg` の basename から
  同じ計画を作り直して照合するので、別ディレクトリの `.d88` は一致しない。
- 起動コマンドは `"<exe>" "/i<trial ini>"` (+ `" <d88>"` を引用付きで追加)。
  `launch_command(plan)` が唯一の組み立てで、PowerShell 側の `$arguments` と
  `$rows[0].command -cne ('"' + $plan.exe + '" ' + $arguments)` も同じ形。
  根拠は `np2arg.cpp` `Np2Arg::Parse` の `case 'i': lpIniFile = &lpArg[2];` と、
  拡張子で判る `.d88` をディスクとして装着する分岐。`preflight` と `start` で
  `CheckPath $plan.fdd_arg` (存在 + reparse point 拒否) を通す。
- 「稼働中プロセスを選んで trial 自身が通常終了 → 終了確認 → 新 ini 作成 → 起動」の
  流れ、単回 callable、strict JSON の完全一致、kill fallback 無し、restore 無しは不変。
  `np21w_ini_live.py` には HDD / FDD キーを足していない。

試験は `tools/tests/test_np21w_trial.py` (19 件、新規 2 件 + 既存の書き換え) と
`tools/tests/test_np21w_ini.py` の `PathFields` (11 件)。`NP21W_DIR` と `wslpath` は
`image_fixture()` の贋物、イメージの存在確認は temp dir の空ファイル。
`tools/tests/test_np21w_transport.py` の trial CLI 2 件も同じ fixture と `--hdd` を使う。
RED→GREEN の実出力は [`s3i2_tdd.md`](s3i2_tdd.md) §T。実 ini・実プロセスは未検証 [V4]。

## 2026-09-14 追記 2 — 実装レビュー往復 1 (B2 / B5)

- 計画に **解決済みパスを束縛**する: `hdd_path` (Windows) / `hdd_host` (ホスト側)、
  `fdd_arg` (Windows) / `fdd_arg_host`。`changes['HDD1FILE']` も解決済みパス。
  `NP21W_DIR` を読むのは `make_plan()` の `resolve_image()` だけで、`_validate_plan()` は
  束縛済みの 3 つ組の整合 (同じ名前・同じディレクトリ・ホスト側が通常ファイル) だけを
  見る。環境を A → B に変えて同じ JSON を dispatch しても A の HDD のまま。
- PowerShell に `CheckFile` (`CheckPath` + `PSIsContainer` 拒否) を足し、`preflight` と
  `start` で `$plan.hdd_path` / `$plan.fdd_arg` を検査する (`.nhd` 名のディレクトリ対策)。
- 反例と RED→GREEN は [`s3i2_tdd.md`](s3i2_tdd.md) §T 往復 1。実 ini・実プロセスは未検証 [V4]。

## 2026-09-14 追記 3 — 実装レビュー往復 2

- `_bound_image()` の説明を実装に合わせた (名前一致 + ホスト側が通常ファイルまで。
  Windows 表記とホスト側の対応は `make_plan()` の `resolve_image()` が決める)。
- `test_generated_ps_is_narrow_...` は **PowerShell のコード文字列の検査だけ**であり、
  `CheckFile` が Windows 上でディレクトリを実際に拒否することの実証ではない (未実施 [V4])。
- ini CLI の絶対パス迂回の直し (名前だけ受ける) は [`s3i2_tdd.md`](s3i2_tdd.md) §T 往復 2。

## 2026-09-14 追記 4 — 実走 (受入 F1) で判った start 段の過検査

起動には成功していたのに `stage: start` で失敗していた。原因は 3 つ:
exe の比較が start 段だけ大小文字を区別していた (CIM は実体の綴りを返す)、
PowerShell が CIM の `CreationDate` (マイクロ秒) と `Process.StartTime` (100ns) の
文字列一致を要求していた、そして失敗理由と起動した PID が結果に残らなかった。
`identity_mismatch()` (Python) と `$mismatch` / `$code` / `$startedPid` (PowerShell) に直し、
起動後の失敗では `process` か `started_pid` を結果 JSON に残す。停止・復旧はしない。
詳細と RED→GREEN は [`s3i2_tdd.md`](s3i2_tdd.md) の実走 F1 の節。PowerShell は未実行 [V4]。

## 2026-09-14 追記 5 — 実走 (受入 F3) の dispose 段

start 段は通過し `result['process']` も入ったが `stage: dispose` で失敗した。原因は
`PowerShellTransport.close()` の `_close_reader()`: **起動した NP21/W が PowerShell の
stdout ハンドルを継承する**ため reader が EOF に届かない (PS 本体は終了 = `wait` は成功)。
既存の `test_parent_exit_with_inherited_pipe_still_reports_failure` が示すとおり
「EOF 未到達は失敗」は意図された不変条件なので、**成功には変えていない**。
代わりに (1) `close()` の失敗へ固定語彙の印 (`cleanup: inherited pipe still open` /
`cleanup: executor exit timeout`) を付け、(2) 起動確認の揺れを `identity_unstable()` で
項目名つきに、(3) 起動後の失敗では常に `started_pid` を残すようにした。
**継承パイプがある限り trial は実走で ok:True を返せない**ので、緩和するか
`UseShellExecute=$true` にするかは PM / レビュー判断 ([`s3i2_tdd.md`](s3i2_tdd.md) の F3 の節)。

## 2026-09-14 追記 6 — 起動を ShellExecute に (PM 判断 ③)

`'start'` 段だけ `$si.UseShellExecute = $true` (リダイレクト無し) にして、起動した
NP21/W に PowerShell の stdout パイプを継承させない。exe・引数・作業ディレクトリ・
`launch_command()` との照合は不変。ShellExecute では `$p.Handle` が取れないことが
あるので同一性の要は `$p.Id`、生存確認は `Start-Sleep` + `$p.HasExited`、`$p.StartTime`
は try/catch で保護し読めたときだけ CIM の created と 2 秒の許容で比べる。
`_close_reader` の「EOF 未到達は失敗」と `cleanup:` の印は**そのまま**維持した
(継承が無ければ EOF に届いて dispose は成功するはず)。
ホストで固定できるのは生成コード文字列までで、継承が実際に断たれるか・
`ok: true` になるかは PM の実走待ち [V4]。詳細は [`s3i2_tdd.md`](s3i2_tdd.md) §T の該当節。
