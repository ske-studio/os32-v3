---
name: os32-emu-remote
description: NP21/W の受入 (エミュレータの試験) を、別の機械のエミュレータ専用のセッション (pc98emu) に頼む。CI の配布物の SHA と受入の項目を渡し、pc98emu が依頼を 1 件ずつ使い捨ての枠で回し、ログの実物で照合して返す。統合の枝の受入・持越しの NP21/W の確かめで使う。本線の機械の NP21/W を直に叩く時は run-os32、実機は os32-realhw-update。
---

# os32-emu-remote — NP21/W の受入を別の機械 (pc98emu) に頼む

ユーザー決定 2026-11-19 (「同じネットワーク内の別マシンに np21/w を動かしてエミュレータのテストはそちらに」「そちらにもエミュレータエージェントを用意してそちらで確認させる」「各版ごとにサブエージェントを立ち上げて実施させる」)。
目的は本線の機械 (codex とビルド) と NP21/W の CPU の取り合いを無くすことと、版ごとの受入を使い捨ての枠で確かめること。

## 0. 構成 (2026-11-19 に作った)

| 物 | 所在 |
|---|---|
| セッション | Remote Control の `pc98emu` (名前は変わり得るので毎回 `ListAgents`)。役は受付・受入・ログの照合 |
| 機械 | Windows 11 + WSL2 (mirrored)、16 コア。Smart App Control は Off (自前ビルドの無署名の NP21/W を動かすため、ユーザーが 2026-11-19 に切った) |
| リポジトリ | 向こうの `~/os32-v3` (読むだけ、push しない)。GitHub は**読み取りだけの権限** (Contents・Actions を Read-only の fine-grained token。push・PR・CI の起動はしない) |
| 像 | **restructure-build の配布物** (`os32-<枝>-<SHA7>-a<attempt>`) を `gh run download` で取る。向こうではビルドしない (クロスコンパイラは入れない) |
| NP21/W | `C:\np21w` (本線の機械から写した ai-debug 版の exe・ROM・ini)。ROM と NHD は著作権物・作業物なので**外へ出さない** (GitHub にも上げない) |
| 枠 | 枠 N (1〜8、ユーザー指示 2026-11-19「4 本でも軽いなら最大 8 枠」) = デバッグの API `aidbport=8025+100(N-1)` (8025・8125・…・8725)、LAN の口 `NP2NETSOCK=127.0.0.1:8026+100(N-1)` (8026・8126・…、host agent の待ち受けも同じ番号)、ini `np21x64w-iN.ini`、NHD・起動 FD `C:\np21w\iN\`、HostDrv `C:\os32-N`。枠 1 は `np21x64w.ini`・`C:\np21w\os32.nhd`・`C:\os32` のまま。**8025〜8028 の連番にしない** — mirrored の WSL は loopback を共有するので、枠 2 の API が 8026 で待つと枠 1 の LAN の口がそこへつながる (2026-11-19 の試しで判明)。 |
| 元の NHD | `C:\np21w\base\os32.nhd` (読むだけ、sha256 先頭 `b81e4d4254cc6eb6`、2026-11-19 の main 225c257 の手前)。枠の NHD はここから戻す |
| 本線からの ssh | `ssh pc98emu` (`~/.ssh/config`、鍵 `~/.ssh/id_pc98emu`、`hight@192.168.1.23:2222` の WSL の sshd、鍵だけ) |

## 1. PM がしてよいこと・しないこと

- **してよい**: `SendMessage` で依頼する。ssh で**ファイルを置く** (NP21/W の更新・NHD の元の写しの差し替え・依頼の文面を `~/os32pm-request-<N>.md` に置く) と**状態を見る** (ファイルの有無・ハッシュ)。
- **しない**: ssh で配備・`hsync`・再起動・受入の台本を直に流すこと。依頼の記録が向こうに残らず、何を確かめたかが追えなくなる (実機ノートで 2026-10-30 に起きた失敗と同じ、[os32-realhw-update](../os32-realhw-update/SKILL.md))。
- 承認: NP21/W の上の試験は承認が要らない (ROLES §6)。枠の NHD の上書き・戻しも同じ。向こうの機械の設定 (sshd・ファイアウォール・Smart App Control) を変えるのはユーザーの操作。

## 2. 依頼の形 (この順で書く)

> 受入の手順は利用者の入口 (シェルのコマンド・GUI の操作) で書く。他の機種と同じ機能は、同じ手順・同じ期待で頼む — 入口が無くて頼めない時は受入を飛ばさず PM に上げる ([POLICY_DEV](../../../docs/POLICY_DEV.md) §10「機種の間で UX/UI を揃える」)。

```
[os32pm → pc98emu] 依頼 <N>: <何の受入か>
版: <枝> <SHA> (restructure-build の run id <id>、最新 attempt <番号>、配布物 os32-<枝>-<SHA7>-a<attempt>)。使い捨ての枠を使う。
起動 FD だけの場合 (BUILD_INFO が (boot FD)): 候補の FD で起動し、ver だけを実行する。
配備 (HDD 向け): 配布物の hostdrv.tar.gz を枠の HostDrv に置く → ゲストで hsync --no-backup --fep-off → "copied=0 errors=0" まで hsync → 要れば hsync sys → reset → ver。
      ブート領域 (deploy-kernel 相当) が要る時だけ: 枠の NP21/W を止める → 枠の NHD に配備 → 起動 ([D1] の順)。
受入の項目: 1 行に 1 つ。何を打ち・何が出れば合格か (画面の文字・ログの行・数)。変異や比べる基点があれば書く。
報告: 各項目の合否と根拠 (ログの実物のパス + 最後の行、画面の文字そのまま)、ver の Commit・Image CRC と配布物の BUILD_INFO・manifest の build= の照合、確かめられなかった物は「未確認」([V4])。
後片付け: 使った枠を止め、NHD を base に戻す。
```

- 受入の項目は、段の票・持越し台帳 (docs/tasks/DEFERRED_TESTS.md) の行から写す。PM の推測で増やさない。
- 候補 SHA を `wt/int-*` に push し、`restructure-build` の同じ run の最新 attempt が選択計画の集計まで成功したことを確かめる。文書だけの変更も、その候補の PC-98 と PowerMac の起動記録を揃えて `python3 -B tools/ci/land_gate.py <SHA> --evidence <JSON>` を回す。 依頼の run 番号と成果物名を指定して gh run download で取得し、同じ run の verified-<SHA>-a<n> の札の有無と、配布物の artifact_id・媒体名・sha256・到達点とログを報告する。

req62〜req64 で使った最小の受入項目。毎回起動を記録し、製品ソースを変更した時は該当する行を加える。

| 変更箇所 | pc98emu に頼む項目と合格の目印 |
|---|---|
| 起動 FD だけ（BUILD_INFO の `(boot FD)`） | 候補の os32_boot.d88 を FD 起動し、`ver` だけを実行。Commit・Image CRC・FD loader をログに残し、媒体の sha256・artifact_id を記録する。 |
| PC-98 配布の HDD 起動 | 候補媒体で起動し、`ver` の Commit・Image CRC を照合。媒体の sha256・artifact_id、`SERVICES_READY public=1`、selftest の成功数と `selftest_fail=0` をログに残す。 |
| FEP / SQLite | `ime info`、Shift+Space の候補表示と復元。sqlite.mod・fep.mod をそれぞれ欠いた時は利用不能の表示と `ver` の応答を確認。SQLite は `db_test` 9/9。 |
| V86 | `v86 -t` は OK。`v86 -g -t` は kdebug on で OK、off で `could not start`。 |
| kdebug | on・off の各設定が再起動後も保たれ、on で pair の CRC が像と一致し、mem・peek が応答する。 |
| exec | `math_test` 110/110 の後にシェルへ戻り `ver` が応答。t5a_display から kbd_echo・kbd_echo.bin を起動し、打鍵が表示され、q で戻る。 |
| FAT | ファイルを作成・更新し、各時刻と操作時刻の差が 2 秒以内。 |
| GUI / 端末 | 打鍵、GUI の出入り、端末 2 つの起動・切替・終了を確認し、終了後の leftover・audit_fail がともに 0。 |

## 3. pc98emu の中の流れ (向こうの型。依頼に毎回は書かない)

1. 依頼は 1 件ずつ受ける。使い捨ての 2 枠を使い、枠 1 は ef00a5f のまま保つ。
2. 使う枠の NP21W_AIDEBUG_URL と HOSTDRV_DIR を設定し、候補媒体を起動して依頼の項目を確かめる。
3. pc98emu が各項目をログの実物と照合し、ログのパスと結果を本線に返す。
4. 使った枠を止め、NHD を base の写しに戻してから次の依頼を受ける。

## 4. 結果を受けた後 (PM)

- 合格: 統合の枝の取り込み (os32-land) に進み、持越しの行は台帳から消す。受入の記録は取り込みのコミット本文か票の状態行に書く (記録だけの別のコミットは作らない、PARALLEL_BRANCHES §3)。
- 不合格: 落ちた項目の画面・ログを根拠に、段の実装者に直しを頼む。NP21/W の側の問題 (枠の不調・道具) なら pc98emu に枠の立て直しを頼み、同じ版で回し直す。
- 未確認が残った: 持越し台帳に行を立てる (未確認のまま合格にしない)。

## 5. 今わかっている落とし穴

- 1 回目の `hsync` は出力が途中で切れることがある (`[aidebug: timeout waiting for EOT]`)。2 回目の `copied=0 errors=0` で揃ったと見る (本線の機械でも同じ)。
- 自前ビルドの NP21/W は署名が無い。Windows の Smart App Control が On だと起動できない (CodeIntegrity 3077)。
- NP21/W の ai-debug 版を更新したら、本線の `~/np21w-src` で組んだ exe を ssh で `C:\np21w` に置き、pc98emu に枠の立て直しを頼む。
  - **ssh (pc98emu、192.168.1.23:2222) がつながらない時** (2026-11-24 は時間切れ): 本線で組んだ exe を非公開のリポジトリ `ske-studio/np21w` の pre-release に添付し、pc98emu が `gh release download <tag> -R ske-studio/np21w` で取って sha256 を照らし、**別名** (例 `C:\np21w\np21x64w-keyedge.exe`) で置いて使い捨ての枠にだけ `--exe` で使う (ユーザー承認 2026-11-24「非公開 release に置く」)。pc98emu の機械には NP21/W を組む道具 (VS Build Tools・yasm・nasm) が無い。受入の台本は `np21w_ctl.py start/stop` に同じ `--exe` を渡す。一時の ini は書いて閉じてから渡す (開いたままの一時の ini は drvfs 越しに NP21/W が読めず aidebug 無しで上がる、依頼 48)。
- 新しく写した HostDrv の置き場は mtime がゲストと合わないので、最初の `hsync` は内容の比べで長くなる (1 回で揃う)。base の NHD (b81e…) は 225c257 の手前の中身なので、使う前に `hsync --no-backup` が要る。
- 負荷 (2026-11-19 の実測、Ryzen 7 7730U・16 論理): 待機中は 1 本で 1 コアの 11〜21%、`hsync -n` 中は 40〜78% を数秒。2 本同時で全体の CPU は最大 16.8%。重い試験と NHD の同時書き込みは未測定。
- 枠を切り替える時は NP21W_AIDEBUG_TOKEN_FILE を外すか、その枠の token に書き換える。
- 起動中の 1 本の写しを取ると壊れた写しになる。NHD を写す時はその枠を止める。
