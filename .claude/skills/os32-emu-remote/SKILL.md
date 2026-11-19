---
name: os32-emu-remote
description: NP21/W の受入 (エミュレータの試験) を、別の機械のエミュレータ専用のセッション (pc98emu) に頼む。CI の配布物の SHA と受入の項目を渡し、pc98emu が版ごとのサブエージェントに NP21/W の枠を 1 つずつ割り当てて回し、ログの実物で照合して返す。統合の枝の受入・持越しの NP21/W の確かめ・複数の版の同時の受入で使う。本線の機械の NP21/W を直に叩く時は run-os32、実機は os32-realhw-update。
---

# os32-emu-remote — NP21/W の受入を別の機械 (pc98emu) に頼む

ユーザー決定 2026-11-19 (「同じネットワーク内の別マシンに np21/w を動かしてエミュレータのテストはそちらに」「そちらにもエミュレータエージェントを用意してそちらで確認させる」「各版ごとにサブエージェントを立ち上げて実施させる」)。
目的は本線の機械 (codex とビルド) と NP21/W の CPU の取り合いを無くすことと、版ごとの受入を混ぜずに同時に回すこと。

## 0. 構成 (2026-11-19 に作った)

| 物 | 所在 |
|---|---|
| セッション | Remote Control の `pc98emu` (名前は変わり得るので毎回 `ListAgents`)。役は受付・割り振り・照合。受入の本体は版ごとのサブエージェント |
| 機械 | Windows 11 + WSL2 (mirrored)、16 コア。Smart App Control は Off (自前ビルドの無署名の NP21/W を動かすため、ユーザーが 2026-11-19 に切った) |
| リポジトリ | 向こうの `~/os32-v3` (読むだけ、push しない)。GitHub は**読み取りだけの権限** (Contents・Actions を Read-only の fine-grained token。push・PR・CI の起動はしない) |
| 像 | **CI の build の配布物** (`os32-<枝>-<SHA7>`) を `gh run download` で取る。向こうではビルドしない (クロスコンパイラは入れない) |
| NP21/W | `C:\np21w` (本線の機械から写した ai-debug 版の exe・ROM・ini)。ROM と NHD は著作権物・作業物なので**外へ出さない** (GitHub にも上げない) |
| 枠 | 枠 N (1〜4) = ポート `802(4+N)`・ini `np21x64w-iN.ini`・NHD `C:\np21w\iN\os32.nhd`・HostDrv `C:\os32-N`。枠 1 は当面 `np21x64w.ini`・`C:\np21w\os32.nhd`・`C:\os32` (8025) のまま。占有は `C:\np21w\slot-N.lock` (SHA・開始の時刻・頼んだ人) |
| 元の NHD | `os32.nhd` の写し (sha256 先頭 `b81e4d4254cc6eb6`、2026-11-19 の main 225c257 の手前)。枠の NHD はここから戻す |
| 本線からの ssh | `ssh pc98emu` (`~/.ssh/config`、鍵 `~/.ssh/id_pc98emu`、`hight@192.168.1.23:2222` の WSL の sshd、鍵だけ) |

## 1. PM がしてよいこと・しないこと

- **してよい**: `SendMessage` で依頼する。ssh で**ファイルを置く** (NP21/W の更新・NHD の元の写しの差し替え・依頼の文面を `~/os32pm-request-<N>.md` に置く) と**状態を見る** (ファイルの有無・ハッシュ)。
- **しない**: ssh で配備・`hsync`・再起動・受入の台本を直に流すこと。依頼の記録が向こうに残らず、何を確かめたかが追えなくなる (実機ノートで 2026-10-30 に起きた失敗と同じ、[os32-realhw-update](../os32-realhw-update/SKILL.md))。
- 承認: NP21/W の上の試験は承認が要らない ([ROLES §6](../../../docs/tasks/agents/ROLES.md))。枠の NHD の上書き・戻しも同じ。向こうの機械の設定 (sshd・ファイアウォール・Smart App Control) を変えるのはユーザーの操作。

## 2. 依頼の形 (この順で書く)

```
[os32pm → pc98emu] 依頼 <N>: <何の受入か>
版: <枝> <SHA> (CI の build の run id <id>、配布物 os32-<枝>-<SHA7>)。枠の割り当ては任せる (同時に回す版があれば別の枠)。
配備: 配布物の hostdrv.tar.gz を枠の HostDrv に置く → ゲストで hsync --no-backup --fep-off → "copied=0 errors=0" まで hsync → 要れば hsync sys → reset → ver。
      ブート領域 (deploy-kernel 相当) が要る時だけ: 枠の NP21/W を止める → 枠の NHD に配備 → 起動 ([D1] の順)。
受入の項目: 1 行に 1 つ。何を打ち・何が出れば合格か (画面の文字・ログの行・数)。変異や比べる基点があれば書く。
報告: 各項目の合否と根拠 (ログの実物のパス + 最後の行、画面の文字そのまま)、ver の Commit・Image CRC と配布物の BUILD_INFO・manifest の build= の照合、確かめられなかった物は「未確認」([V4])。
後片付け: 枠の NP21/W は止めて lock を外す / 動かしたまま (どちらか書く)。
```

- 受入の項目は、段の票・持越し台帳 ([DEFERRED_TESTS](../../../docs/tasks/DEFERRED_TESTS.md)) の行から写す。PM の推測で増やさない。
- CI がまだ success でない SHA は頼まない (配布物の名前と run id を `gh run list` で確かめてから)。文書だけのコミットには build が走らないので、その前の配布物の SHA を使う。

## 3. pc98emu の中の流れ (向こうの型。依頼に毎回は書かない)

1. 空いている枠を選び、`slot-N.lock` を置く。無ければ待つ (待ちの行を本線に一言)。
2. 版ごとにサブエージェントを 1 つ立て、枠の環境 (`NP21W_DIR`・`NP21W_AIDEBUG_URL=http://127.0.0.1:802(4+N)`・`HOSTDRV_DIR`) と依頼の項目だけを渡す。MCP の np21w は 8025 に固定なので、枠 2〜4 では `tools/np21w_ctl.py`・`driver.py` を環境変数で使う。
3. サブエージェントの報告の「通った」を、ログの実物で pc98emu が照合する (実装者の報告の決まりと同じ: 根拠のログのパスと最後の行、無ければ未実行)。
4. 照合の済んだ結果だけを本線へ返す。枠が固まったら、その枠だけ NHD を元の写しから戻して立て直す。

## 4. 結果を受けた後 (PM)

- 合格: 統合の枝の取り込み (os32-land) に進み、持越しの行は台帳から消す。受入の記録は取り込みのコミット本文か票の状態行に書く (記録だけの別のコミットは作らない、[PARALLEL_BRANCHES](../../../docs/tasks/agents/PARALLEL_BRANCHES.md) §3)。
- 不合格: 落ちた項目の画面・ログを根拠に、段の実装者に直しを頼む。NP21/W の側の問題 (枠の不調・道具) なら pc98emu に枠の立て直しを頼み、同じ版で回し直す。
- 未確認が残った: 持越し台帳に行を立てる (未確認のまま合格にしない)。

## 5. 今わかっている落とし穴

- 1 回目の `hsync` は出力が途中で切れることがある (`[aidebug: timeout waiting for EOT]`)。2 回目の `copied=0 errors=0` で揃ったと見る (本線の機械でも同じ)。
- 自前ビルドの NP21/W は署名が無い。Windows の Smart App Control が On だと起動できない (CodeIntegrity 3077)。
- NP21/W の ai-debug 版を更新したら、本線の `~/np21w-src` で組んだ exe を ssh で `C:\np21w` に置き、pc98emu に枠の立て直しを頼む。
- 複数の枠を同時に立てた時の道具の詰まり所は、2026-11-19 の 2 本同時の試しの結果でここに足す。
