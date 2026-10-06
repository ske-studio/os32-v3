---
name: os32-emu-config
description: NP21/W ini の限定変更。変更権限は PM (Claude Code) だけで、コーダー・テスター・レビュアーには無い。通常終了後の新規 trial ini、オフライン変換、既存ライブ変更を依頼範囲で使い分ける。ini に触る前に必ず読む。
---

## 0. 権限 — ini を変更してよいのは PM だけ

**ini の変更権限は PM (Claude Code の対話セッション) にのみ与えられている。**
他の役は読むことすら原則しない ([`ROLES.md`](../../../docs/tasks/agents/ROLES.md))。

| 役 | ini |
|---|---|
| **PM** (Claude Code) | **変更してよい唯一の役**。ユーザーが権限を付与済み (2026-10-01、`ROLES.md` §0 の「決裁」) — 個別の承認は要らないが、原本を残す経路を既定にし、終わったら戻す。NHD・イメージ原本の上書きは [D2] のまま |
| コーダー (サブエージェント) | 不可。ini はスコープ外。必要になったら PM に戻す |
| ローカル AI (`tools/emu_agent/`) | 不可。`tools/emu_agent/` の `ACTIONS` に ini 操作は存在しない |
| レビュアー | 不可 (読み取りのみの役) |

機械的な裏付けは `.claude/settings.json` の `ask`:
`Edit(**/*.ini)` / `Write(**/*.ini)` と、`np21w_ini_live.py` / `np21w_trial.py` /
`np21w_ini.py` の起動。**ただし包括的な保護ではない** — 別の書き方や別コマンド経由は
文字列一致しない。規則の方が正典であり、`.claude/settings.json` は再確認の網に過ぎない。

**ini に触る前に、本当に ini が要るかを先に確かめる。** 順序はゲスト側が先
(`gfxmode pc98|pegc|cirrus|auto` → `/etc/system.cfg` 読み戻し → リセット)。
ini が要るのは、そのバックエンドをエミュレータが提供していないときだけ。

**要求した設定と実際のバックエンドは別物**。`ver` の `GFX:` 行は当てにならない
(起動時に固定された文字列)。実際のバックエンドは `hal_test` の 1 行目で確認する。

`hal_test` の読み方に注意がある。**`hal_test` は `gfx_init()` を呼ばず
`gfx_screen_info()` を読むだけ** (HW 塗りが立つ機種でだけ init する)。つまり
**起動時に選ばれたバックエンドを報告している**。`gfx_prepare_backend()` が
boot_splash の直後にカーネル文脈で選択・初期化するので現在はこれが正しい値だが、
`gfxmode` を変えたら**リセットするまで `hal_test` の値は変わらない**。
2026-09-09 に、リセット前の `hal_test` を見て「PEGC が使えない」と誤診した。

probe が本当に走ったかまで見たいときは、`kernel.map` の `s_probed` / `s_probe_ok` と
`sys_top_reserved` を `/api/mem?space=phys` で読む (バックバッファ予約が取れたかが分かる)。

## 1. 扱えるキーの範囲

現在ツールが扱うのは `[NekoProject21]` の **`USEGD5430` / `GD5430TYPE` /
`USEPEGCP` / `ExMemory` / `e_resume`** だけ。`pc_model` `MEMswtch` `DIPswtch` などは
**未対応**で、推測して追記・変更しない。ほかのキーが要る検証が出たら、
ツールの拡張を別タスクとして起こし、その実装と実適用の承認を分ける。

キーの照合は**大文字化して**行う。ini 側の綴り (`ExMemory`) は書き換えず、値だけ直す。

`transform()` は**変更するキーが 1 つでも全キーの存在を要求して fail closed** する。
NP21/W の `initsave` は `s_IniItems[]` 表を丸ごと書くので、このエミュレータが
書いた ini には常に揃っている。

| 操作 | 変えるキー | 根拠 |
|---|---|---|
| `cirrus-on` / `cirrus-off` | `USEGD5430` (+ `GD5430TYPE=91` 維持) | TASK_H3_cirrus §0 |
| `pegc-on` / `pegc-off` | `USEPEGCP` のみ | `win9x/ini.cpp:687` が `np2cfg.usepegcplane` に束縛し、`io/pegc.c:375` が `pegc.enable` に写す。`mem/memvga.c` が PEGC の VRAM 経路ごとに見る |
| `ram-8mb` / `ram-15mb` / `ram-32mb` / `ram-128mb` | `ExMemory` のみ (7 / 16 / 33 / 129。16 以上は 16MB システム空間の 1MB が抜けるのでゲスト報告量 + 1) | `win9x/ini.cpp:477` (`PFTYPE_UINT16`、MB 単位)。ブートローダが 1MB から 512KB 刻みで実測する (`boot/loader_fat.asm:248`)。**8MB は CUI の最低動作環境**で `memory_boot` の legacy フォールバックを通す構成。**GUI の最低要件ではない** (INSTALL.md / docs/02_memory.md / tasks/gui/DESIGN.md) |

**`pc_model` は PEGC の gate ではない。** OS32 の `pegc_probe()` が見るのは BIOS
ワークエリア 0x045C bit6 と 0x0597 bit2 で、2026-09-09 の実測では `pc_model=VX` の
まま両方立っていた (`0x40` / `0x84`)。`pc_model` は VM/VX の 2 値 (CPU 世代) しか取らない。
PEGC と Cirrus は独立に選べる (操作が触るキーが重ならない)。

## 承認済み disk trial（通常終了・新規 ini、2026-09-14 に Cirrus trial から改訂）

exe 隣接 baseline をコピーし、**稼働中の NP21/W を trial 自身が通常終了 → 終了確認 → 新 ini 作成 → 起動**する依頼は
[tools/np21w_trial.py](../../../tools/np21w_trial.py) を使う (票 S3-I2)。trial が扱う変更集合は
**`HDD1FILE` (`--hdd <name>` = `NP21W_DIR` 直下の `*.nhd`)、`FDD1FILE` / `FDD2FILE` の空化 (`--fdd-eject`)、
`e_resume=false` の強制**だけで、Cirrus / PEGC / ExMemory は触らない (それらはライブ変更ツールの領分)。
起動は `"<exe>" "/i<trial ini>" ["<d88>"]` (`--fdd-arg <name>` で `NP21W_DIR` 直下の `.d88` を FDD 引数に付ける)。
**PM が先に NP21/W を終了しない** (稼働中プロセスの PID / 生成時刻を計画に束縛して trial に終了させる。
taskkill は ini を書き戻さないので使わない)。使い捨て NHD は `tools/mk_blank_nhd.py --out <NP21W_DIR>/<name>.nhd` で作る。
`--exe` / `--baseline` / `--cwd` は **Windows 表記** (`C:\...\np21x64w.ini`) で渡す (WSL パスは `path_key` が拒否し、CLI は理由を出さず `invalid setup` と言う)。
`--llm-url` の既定は `127.0.0.1:1234` なので、FLM を使うなら `--llm-url http://127.0.0.1:52625/v1/chat/completions --model <ロード済みモデル>` を明示する。
既存ライブツールの強制終了・原本置換・restore はこの承認に含めない。
ホスト限定実装依頼では実プロセス・実 ini・ネットワークに触れない。
試験・ソース根拠・実行例は [trial TDD 記録](../../../tools/tests/np21w_trial_tdd.md)。

- 操作者が PID、CIM の UTC 生成時刻（`2026-09-09T01:02:03.0000000Z` 形式）、
  exe、exe 隣接の同名 `.ini`、exe ディレクトリの cwd を選ぶ。
  baseline は操作者が選んだコピー元であり、以前の active ini の証明ではない。
  古いコマンド行から現在の設定選択を推定しない。
- `make_plan()` の全フィールドを承認対象として `bind_trial()` に束縛する。
  モデルにはコピーした JSON と単回 callable のみを渡す。ローカル LLM の提案は
  strict JSON の完全一致が必須。変更・追加・重複フィールド・複数提案・tool calls は拒否。
  不一致でも束縛を消費し、executor 構築・照会・ロック・書き込みに進まない。
  CLI は自由シェルや既存 action registry を使わず、loopback LLM へ1回だけ要求する。
- `--execute` なしは純粋 dry-run（実 ini の差分プレビューではない）。
  `--execute --exclusive-operator` は、その呼出しで作成する固有 trial 名を含む計画を
  ホスト側で承認し、LLM 照合後に通常終了→終了確認→コピー変換→明示起動を1回実行する。
  計画を外部で事前承認するホストは `make_plan()` の戻り値を保持して `bind_trial()` を使う。
  dry-run を別途実行したときの trial 名は次回 CLI 呼出しには引き継がない。
- 同じ Global mutex と専有前提で、唯一の NP2/NP21 プロセスの PID・生成時刻・exe を
  再照合する。ハンドル確保後の `CloseMainWindow()` のみで閉じ、実際の
  `WaitForExit(10000)` と成功した空の CIM 照会で終了を確認する。
  確認ダイアログの拒否・放置・ハングは失敗。kill fallback、自動再試行はない。
- baseline は終了確認後に初めて読み、read-only snapshot と限定変換から
  `CreateNew` で隣接する `np21w-trial-<UUID>.ini` を作る。原本へ直接書かない。
  限定変換は `HDD1FILE` の差し替え、`FDD1FILE` / `FDD2FILE` の空化 (`--fdd-eject`)、
  必要なら `e_resume=true→false` だけで、Cirrus / PEGC / ExMemory は触らない。
  resume 欠落・重複・未知値も追記せず拒否する（この場合は終了後に停止する）。
  無関係な全バイトを保持。通常終了が baseline を保存し直すことは許容する。
- 起動直前に原本の内容・識別情報と trial 全バイトを再確認する。
  起動引数は固有 trial の絶対パスだけ、cwd も明示して結果に記録する。
  新プロセスのハンドル・PID・生成時刻・exe・明示コマンド行を照合し、再照会する。
  これは起動検証であり、起動後の UI 設定切替やゲスト backend の検証ではない。
- 許容するライフサイクル差は、通常終了に伴う設定・状態保存、新しい ini のファイル名、
  上記の限定変換、明示 cwd、新しいプロセスと cold start。
  VM RAM の完全保存は約束しない。共有ディスクのゲスト通常動作も隔離されない。
  NHD のコピー・配備は行わない。resume state の名前も ini stem に従う。
  原本の保持とはツールが終了後の baseline を上書きしない意味で、終了前の完全保存ではない。
- 失敗は stage / completed / trial / plan を JSON に残す。部分作成ファイルや
  起動済みプロセスを自動削除・停止・復元しない。元のコマンド行や ini 原文は表示しない。
  ローカル NTFS、reparse/hardlink 拒否、専有を前提とする。非協調プロセスの競合を
  完全には排除しない。既存承認が当該通常終了と trial 差分を含むなら再承認は不要 [D2]。

## Transport cleanup の限定回帰試験

- stdout の BufferedReader は読み取りスレッドだけが閉じる。他スレッドの
  `close()` は read lock を待って停止し得るため、終了側は bounded join に留める。
  EOF 未到達時は失敗を返し、最終的な stream close は reader が EOF 後に行う。
- wait timeout を成功に変換しない。trial は kill/retry なし、共有 live は元の
  transport kill fallback だけを維持する。cleanup で既存の wait 例外を隠さない。
- `python3 -B tools/tests/test_np21w_transport.py -v` で実ローカル pipe と偽 process の
  timeout・通常 EOF・遅延 EOF・CLI failure JSON の stage/process 保持を検査する。
  live の CLI は、`start` で孫を起動する無害なホスト子プロセスで「孫が stdout を継承すると
  receipt / PID を出さず cleanup timeout、継承しなければ receipt と PID を出して rc=0」を検査する
  (孫は 1.5 秒で自然に終わる。kill しない)。EOF 未到達を失敗とする不変条件は変えず、
  直すのは起動側 (PS の `start` を ShellExecute に) である。
  実エミュレータ、CIM、実 ini を使わない。Windows parser 試験は PS 本文をデータとして
  ParseInput に渡すだけであり、実ライフサイクルの合格とは区別する。

## 従来のライブ変更・オフライン変換

以下は既存ツール固有の手順。trial の通常終了承認から切り替えない。

[tools/np21w_ini_live.py](../../../tools/np21w_ini_live.py) がライブ制御、
[tools/np21w_ini.py](../../../tools/np21w_ini.py) が既存の純粋変換・オフライン準備。
ライブ制御は注入した偽 Windows executor でホスト試験済み。
**実機動作の合格を意味しない**。試験と未実施事項は
[TDD 記録](../../../tools/tests/np21w_ini_live_tdd.md) を参照。

## 範囲と根拠

[TASK_H3_cirrus.md §0](../../../docs/archive/gui_v11/TASK_H3_cirrus.md) と
[GUI TASKS.md の 2026-09-06 WAB OFF 記録](../../../docs/tasks/gui/TASKS.md) に従い、
`[NekoProject21]` の `USEGD5430=true/false`、`GD5430TYPE=91` のみ扱う。
91 は十進 Xe10 固定。欠落・重複・未知の既存値は追記や推測をせず拒否する。
無関係なバイト・文字コード・コメント・空白・混在改行・BOM は既存 `transform()`
で保持する。表示は変更フィールドだけ。原文や全設定を出力しない。

ホスト限定の依頼では合成データと偽 executor のみを使う。
実 ini 取得、エミュレータ照会・接続・停止・起動、配備を行わない。
`.env`、資格情報、`docs/hw/` は読まず、環境変数をロードするヘルパーを import しない [D3]。
既存 `os32-cycle`、HTTP status、tasklist の文字列一致で検証を迂回しない。
`stopped=True`、pause、breakpoint、HTTP 不在、操作者の停止宣言は終了証明にならない。

## 実機で使うための前提

- PM/ローカル操作者が非機密の起動情報と対応ソースを照合して exe と ini を選ぶ。
  WSL Python 3 と Windows PowerShell 5.1/.NET Framework、CIM のプロセス情報参照、
  対象 PID の終了、ローカル NTFS の読書き・ACL 設定、Global mutex の権限が必要。
- 対応ソースの `src/win9x/np2arg.cpp` (`Np2Arg::Parse`)、`ini.cpp`
  (`initgetfile`)、`common/milstr.c` (`milstr_getarg`)、`np2.cpp` (コマンド行の
  ディスクを FDD1〜4 へ) を根拠に、現在サポートするコマンド行は次の **3 形だけ**
  (各トークンは引用されたドライブ絶対パス、末尾の空白は無視):
  1. `"C:\...\np21x64w.exe" "C:\...\selected.ini"` (位置引数の ini)
  2. `"C:\...\np21x64w.exe" "/iC:\...\selected.ini"` (`tools/np21w_ctl.py start --ini`)
  3. `"C:\...\np21x64w.exe" "/iC:\...\selected.ini" "C:\...\os32_boot.d88"`
     (`start --ini --fd`。3 語目は FD イメージの拡張子 `.d88/.88d/.d98/.98d/.fdi/.nfd/.hdm/.img` だけ)
  **再起動 (適用・restore とも) は止めたプロセスと同じ形で起動し直す** (FD 引数付きなら同じ FD 引数も付ける)。
  起動後のコマンド行の形が元と違えば失敗にする。
  NP21/W 自体は受け付けても未対応として拒否するもの: `-i`・`/I`・引用の無い `/i`、`/i` の 2 回、
  相対パス、`/f` など他のスイッチ・未知の引数、CD / cfg / 2 つ目の ini、FD 引数 2 個、
  位置引数の ini + FD、UNC、デバイスパス。
  別名 exe や設定探索を推測しない。同名/別 checkout を含め、NP2/NP21 系プロセスが
  複数あれば拒否する。選んだバイナリがこのソースに対応することは PM が確認する。
- 操作者はプレビューから再起動確認まで利用を専有し、他の起動・設定編集を防ぐ。
  `--exclusive-operator` はその前提の表明であり、終了証明ではない。
  Global named mutex は本スクリプト同士を排他する。任意の外部プロセスの起動や
  最終検査直後の書き込みまで絶対に防ぐ保証はない。書き込み前後にも再照会・再比較する。
- **停止は対象 PID の強制終了 (`Process.Kill`)**。未保存のゲスト状態を失うため、
  操作者がゲスト作業を保存し、この停止を含む変更を承認する。
  [D2](../../../docs/CONSTRAINTS.md) に従い具体的な対象と差分を提示する。
  当該操作の SCRIPT AND SKILL 経由での承認が既にあれば再質問しない。

## 操作者向け CLI

以下のパスは例。PM が選んだ実際の絶対パスに置き換える。
**既定は実対象の読取プレビュー**なので、ホスト限定作業中には実行しない。

```bash
python3 tools/np21w_ini_live.py cirrus-on --exe 'C:\NP21\np21x64w.exe' --ini 'C:\NP21\selected.ini'
# 差分確認と当該変更の承認後のみ。停止→保存→起動まで行う。
python3 tools/np21w_ini_live.py cirrus-on --exe 'C:\NP21\np21x64w.exe' --ini 'C:\NP21\selected.ini' --live-apply --exclusive-operator
# OFF は cirrus-off。GD5430TYPE=91 を維持する。
# RECEIPT_ID は適用結果の32文字ID。任意のファイルパスではない。
python3 tools/np21w_ini_live.py restore --exe 'C:\NP21\np21x64w.exe' --ini 'C:\NP21\selected.ini' --receipt RECEIPT_ID
python3 tools/np21w_ini_live.py restore --exe 'C:\NP21\np21x64w.exe' --ini 'C:\NP21\selected.ini' --receipt RECEIPT_ID --live-apply --exclusive-operator
```

変更なしは停止・保存をしない。正常終了 0、拒否/失敗 2。

**起動の形** (2026-10-01〜): `powershell.exe -EncodedCommand` に載せるのは固定のブートストラップ (約 1,000 字) だけ。
本文 (PS_SERVER) は stdin の 1 行目で渡し、ブートストラップに焼き込んだ SHA-256 と一致したときだけ dot-source で
実行する (違えば出力なしで exit 3)。本文を直接載せる旧形式は、2cd1156 で Windows のコマンド行の上限 32,767 字を超え、
`powershell.exe: Invalid argument` で全部失敗した。起動の段だけの確認は、不正な JSON を 1 行送る形で
`python3 -B tools/tests/test_np21w_ini_live.py --windows-fixtures RealPowerShellFixture.test_bootstrap_runs_the_fixed_body_up_to_request_parse`
(`{"ok":false,"step":"request:parse",…}` が返る。ini・CIM・プロセス・mutex に触れない)。

**失敗の読み方** (2026-10-01〜): PS 側の失敗は
`Windows operation failed: <op> [<op>:<段>[.<手順>] <例外の型> win32=<N> hresult=0x<8 桁>]` の形で出る。
例外の文言・パス・内容は出さない (出るのは固定の段名・.NET の型名・数値だけ)。段の例:
`snapshot:ini.open` (ini を開けない)、`replace:candidate.*` (一時ファイルの作成と読み戻し)、
`replace:expected.*` (置換直前の再確認)、`replace:File.Replace`、`replace:readback.open|fileid|read|changed|compare`、
`replace:identity` (候補と別のファイル)、`replace:receipt.backup.*` / `replace:receipt.json.*` (レシート)、
`replace:absent.after`、`start:launch` / `start:alive`、`request:parse`。手順は
`checkpath` (reparse の検査)・`open`・`fileid`・`size`・`read`・`changed` (読む間の変化)・`compare`・
`create`・`write`・`verify`。`win32=32` / `33` は共有違反・ロック違反 (4 秒まで待った後)、`5` は拒否、
`2` / `3` はファイル / パスが無い。`RuntimeException win32=5377 hresult=0x80131501` はツール自身の検査
(`throw '...'`) が止めたもので、段名が理由を表す。角括弧が無いのは PS が診断を返さなかったか、
形が固定の語彙から外れたので捨てたとき。
`...fileid.links` / `...reread.links` は **ファイルに二つ目の名前 (ハードリンク) がある**ので拒否した、という意味。
これは正しい拒否で、道具では緩めない。2026-10-01 の実例は Google Drive for Desktop の一時リンク
`Documents\np21w\.tmp.driveupload\<番号>` だった (Documents が同期対象)。
名前の一覧は `fsutil hardlink list <ini>` で見られる (読むだけ)。
対処は環境の側で行う: Drive を一時停止または終了し、一時リンクが消えて一覧が ini 1 行だけになってから実行する。
恒久的には NP21/W のフォルダを同期の対象から外す (Documents の外へ移すなら、ctl・ini のパスの変更は [D2])。
`...fileid.handleinfo` は情報の取得そのものの失敗で、Win32 エラーが HResult (0x8007xxxx) に入る。
**NP21/W が ini を開く時間帯**: 起動直後の `initload` (GetPrivateProfileString の短い open/close の連続) と
通常終了の `initsave` だけ (fsrescfg の解像度別設定・一部のダイアログは別)。ツールの `stop` は強制終了なので
終了時の書き込みは無い。起動直後に実行すると、こちらの `FileShare.Read` の open と NP21/W の読み込みが
重なりうる (NP21/W 側の読み込みが失敗すると既定値で起動しうる)。**実行は `np21w_ctl.py start --wait-ready` が
返ってから**にする。`OpenShared` の 4 秒の再試行は NP21/W の読み込みの合間を待つには足りるが、逆向き
(こちらが開いている間の NP21/W の読み込み) は防げないので、待つ場所は起動側である。
初回適用は明示 ini で起動中の対象が必要。復元は、記録済みの起動情報と
成功した不在照会があれば、再起動失敗後の停止状態からも可能。

追加のライブ前提: コマンド行は起動時の設定指定しか証明しない。
NP21/Wは起動後にもsetiniFilenameで選択iniを変更できるため、現在の選択パスの
信頼できる観測、または当該プロセスの生成時刻に結び付く管理された起動履歴が必要。
現在のツールはこの前提を自動検証しない。明示引数だけで使用中iniを確定したと
扱わず、前提を確認できない既存プロセスへ実適用しない。
根拠: np21w-src/src/win9x/np2.cppの設定切替、dialog/d_cfgsave.cpp、
ini.cppのinitload/initsave。bare起動時の既定ini導出と現在の選択は区別する。

ライブ処理の順序:

1. 排他ロックを取得。CIM の成功結果から PID・exe・コマンド行・生成時刻を照合。
   成功した空配列と照会失敗/権限不足/欠落フィールドを区別する。
2. 全バイトと Windows ファイル ID・作成/更新時刻・サイズを取得し、純粋変換で候補と差分を作る。
3. PID/生成時刻を再照合し、プロセスハンドルを確保してその対象を終了。
   `WaitForExit(10000)` と成功した CIM の不在照会で終了を確認。
4. 停止後スナップショットを比較。変化していれば拒否。
   ini に隣接する固有の `*.np21w-live-ID/original.bin` に原本を保存し、
   `Flush(true)` と読み戻しを完了してから置換へ進む。バックアップは所有者専用 ACL。
5. 同じディレクトリの一時ファイルを作成・flush・検証。プロセス不在と元の内容/識別情報を
   直前に再確認し、`File.Replace` で置換。全バイトを読み戻して一致し、**読み戻したファイルの
   volume / file ID が候補 (一時ファイル) のものと同じ** (`ReplaceFile` は候補の ID を残す) なら、
   **同じ `replace` 要求の中でただちにレシートを保存** (`applied` = その読み戻し)、それから不在を再確認する。
   ID が違えば (同じバイト列の別ファイル) レシートを書かずに失敗する
   (`readback identity differs from the candidate`、`original.bin` は残る)。PS は失敗した要求で
   セッションを閉じるので、置換後に別の要求で書く形だと、置換後のどの失敗でもレシートが残らない
   (2026-10-01 の実害: `snapshot` 段の失敗で `receipt.json` が無く `restore` 不能)。
   reparse point は親要素を含め拒否し、読み込むファイルの hardlink も拒否する。
   ファイルを開くときの共有違反・ロック違反 (Win32 32/33) だけは、1 回の open につき再試行の待ちの予算 4 秒まで (期限の直前に始めた検査と open そのものは越えうる)
   250ms 間隔で再試行する (強制終了や `File.Replace` の直後に走査ソフトなどが一時的に掴む。§4-60 と同種、
   主体は未確認)。それ以外の I/O エラーは再試行しない。待つ間にパスが差し替わりうるので、reparse point の
   検査 (親要素を含む) は試行のたびと open の成功後にやり直す。
6. 再起動前にも不在・スナップショットを比較。同じ exe と明示 ini を、止めたプロセスと同じ形
   (位置引数 / `/i`、FD 引数) で起動し、
   起動した PID が 1 秒後に終了していないこと (`Start-Sleep` + `HasExited`) と、CIM による同一対象の
   2 回の確認を行う。作業ディレクトリは exe の親。対応ソースも起動時に `file_setcd(modulefile)` を実行する。
   起動は **`UseShellExecute=$true`・リダイレクト無し** (trial の F3 と同じ)。`$false` だと .NET は
   `CreateProcess(bInheritHandles=TRUE)` で起動し、NP21/W が PS の stdout パイプを持ち続けるので
   reader が EOF に届かず、ini の置換・レシート・再起動が済んでいても最後が必ず
   `Windows executor cleanup timeout` になっていた (2026-10-01、PM の実プロセス)。

失敗時は自動巻き戻し・追加の起動をしない。置換後の失敗にはバックアップ ID を付ける。
`replace` が成功した後の失敗は `receipt ID for restore: <ID>` — レシートがあり、停止状態のまま
(再起動が失敗した場合も) `restore` を試せる (ini がその後変わっていれば restore は拒否する)。
**restore 自体**が置換後に失敗した場合は `restore-operation receipt ID (not restorable; investigate): <ID>`
— そのレシートは `operation=restore` なので restore できない。`replace` が成功を返す前の失敗は
`retained backup ID (receipt not confirmed): <ID>` — ini は置換前か、置換されたが読み戻しか
レシートの保存が済んでいない。
バックアップやレシートが未完成なら通常の復元は拒否し、原本を残して操作者が調査する
(`original.bin` はある。ini の現状を読み、原本と照合してから扱う)。
復元は元のターゲット、バックアップ、純粋変換の再計算、適用後の全バイトと識別情報を照合。
途中で ini を保存し直した場合も拒否する。復元処理自体のバックアップは調査用であり、
`restore` レシートをさらに `restore` する操作はサポートしない。
バンドルは原文を含むため表示・共有しない。プロセス検証成功とゲストでの反映確認は別。

## モデル/FLM 向けの限定入口

自由なシェル・任意パスをモデルに渡さない。信頼された操作者側のホストコードで
`bind_model_operation()` を束縛し、返った callable **だけ**を公開する。
リクエストは `{"operation":"cirrus-on"}`、`cirrus-off`、または
`{"operation":"restore","receipt":"32文字ID"}`。余分なフィールドは拒否する。

```python
# ホストの信頼されたセットアップ。target は PM が選択した2パス。
from np21w_ini_live import WindowsExecutor, bind_model_operation
preview = bind_model_operation(WindowsExecutor(target), target)
# D2 の当該差分の承認後、承認内容そのものを束縛する。
approved = {"operation": "cirrus-on"}
apply_once = bind_model_operation(
    WindowsExecutor(target), target, authorized_apply=True, exclusive=True,
    approved_request=approved)
# モデルへは apply_once(request) のみ公開。承認は1回の試行で消費する。
```

この入口と CLI は実装済み。既存 `emu_agent` の自由シェル許可リストを広げない。
今回の変更範囲には既存エージェントへの自動登録は含まれないため、PM のホストで
上の callable を限定アクションとして接続するか、操作者が CLI を直接使う。
停止検証器を別途実装する必要はない。

## オフライン準備と反映確認

従来のオフライン手順も利用可能。入力スナップショット自体は更新しない。

```bash
python3 tools/np21w_ini.py prepare INPUT.snapshot --set USEGD5430=true --set GD5430TYPE=91
python3 tools/np21w_ini.py prepare INPUT.snapshot --set USEGD5430=true --set GD5430TYPE=91 --output OUTPUT_DIR --apply
python3 tools/np21w_ini.py restore BUNDLE
python3 tools/np21w_ini.py restore BUNDLE --apply
```

オフライン `--apply` は新規専用バンドルを作るだけ。実 ini へコピーしてライブ検証を迂回しない。
実機で承認・実行した後は `ver`、`hal_test` の backend、画面、必要な `wab_relay` を観測する。
本スクリプトはゲスト試験・配備をしない。HostDrv だけでは反映検証にならない [V1]。
NHD/ブート配備は別の明示範囲に従い停止中のみ [D1]。未実施はそのまま報告する [V4]。
