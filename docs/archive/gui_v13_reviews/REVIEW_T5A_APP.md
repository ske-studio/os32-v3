# T5a — 初回独立レビューのPM照合

> 発行: PM (2026-09-09) / 状態: **完了記録 (2026-09-10)**

ホスト実装・独立レビュー通過、ゲスト未受入。
親: [TASK_T5A_APP.md](../gui_v13/TASK_T5A_APP.md)。原報告 `/tmp/os32-v13-t5a-review-final.txt`。

## PM確認済み

- host18試験成功。on_quitのreason引数欠落(E0050)をPMが既存traitに合わせ修正後、make t5a_display_rust成功。
- ELF text24858/data2704/bss20844、未解決シンボルなし。GNU-stack欠如/RWXのリンク警告あり。
- workspace/build/deploy登録済み。make check全体exit0。後続で既存hotdeployを実施（下記）。
- 事前ハッシュに対して既存モデル/rendererの26ファイルに変更なし。

## レビュー指摘の訂正

1. deploy未登録は誤検出。`userland/deploy.yaml:254` に存在することをPMが検索確認。
2. statusの64B超過は現ゲストでは成立しない。out[4]のusize32/u64最大値でも61B。
   現行rows64なら53B。host64でもrows64なら63B。未制約の全usize64最大を現在の入力と混同しない。
3. host_tests/targetはgit check-ignoreで無視対象と確認。
4. `programs: t5a_display_rust` の独立依存行はMakeの依存追加であり、流儀差だけでは不具合ではない。

## 補足確認

Session::select失敗後にdisplayがNoneのままPaintへ進むという指摘は一般的には注意を要する。
補足レビューで現行Session::newの容量検査と固定fixture寸法から失敗経路は到達不能と確認。
現不具合とは扱わない。将来可変寸法を導入する場合はselect失敗後のPaintを再検討する。

補足報告 `/tmp/os32-v13-t5a-review-supplement.txt` はpure部分のTDD不足なし・host合格。
S1〜S9のRED/GREEN対応、既存初期化の初回GREEN回帰とSession再借用のS9 REDを区別して確認。
ログは部分精読と全結果行照合であり、過去実行の真正性を証明したとは扱わない。
guest.rsの実行はこの合格に含まない。

## 後段へ残す項目

本文外と本文背景の色差、damage依存のprev_runs、状態欄描画呼出コストをREADME台本に追記済み。
check-t5a-hostをmake checkへ登録し全体exit0。ログ `/tmp/os32-t5a-final-check.log`。
最終ELFのnm確認: STORAGEはBSS、0x506d1c、サイズ0x5004。
BSS範囲は0x506bc0..0x50bd2c。実行時stack最大量・heap/guardとの動的関係は未測定。
PC98/PEGC/Cirrus全構成・配備内容の読戻し同一性・性能は残作業。
ユーザー承認によりローカルAIで配備・ゲスト操作を実施。T5a全体完了とはしない。

## ローカルAIによるゲスト部分検証（2026-09-08）

操作担当はemu_agentのgemma4-it:e4b。PMは保存スクリーンショットと操作ログを照合。
以下のsessionはすべて `tools/emu_agent/logs/<session>/` 以下にsteps.jsonlとshotsを持つ。
ホスト上の制限付き `/tmp/os32_local_gui.py` は既存gui_gateのkey/Mouseを利用。
ローカルAI自身が画像を判定したという意味ではない。

- `20260908-214240`: 既存hotdeployで `/usr/bin/t5a_display.bin` を新規配備。
  配備前の不存在、exit0/HOTDEPLOY OK、配備後27612 Bを確認。内容読戻し照合は未実施。
- `20260908-214906`: 絶対パスで起動。最終画面取得後のmax_stepsで終了。
  非許可cmd_nowaitは未知actionとして拒否され、実行されていない。
- `20260908-215002`: fixture 2→3→4→5→1の各画面を取得。
  TooWideのconsumed=3/pending=65E5、finish:Fullのconsumed=2562/pending=FFFD、
  通常へのcomplete/229 bytes/pendingなし復帰を目視確認。
- `20260908-215729`: j/k/e/gと撮影は実行済み。j後top=1、e後top=36、g後top=0を確認。
  最終doneのJSONエスケープ不正が3回続きunparseable終了。操作失敗と混同しない。
- `20260908-215847`: coverのみ閉じた後も本文表示。qでdesktop復帰。
- `20260908-215924`: 新プロセスで両窓・complete/229 bytes/top=0/pendingなしへ復帰。
- `20260908-220019`: 本文のみ閉じた後もcoverが残る。ESCでdesktop復帰。
  本文終了後のfixtureキー送信は記録したが、状態表示がないため切替成功とは判定しない。
- `20260908-220222`: coverを退避→本文に重ね→退避。step02とstep06はRGB全画面一致。
- `20260908-220328`: 非セル整列位置への追加遮蔽後に退避。
  基準220222/step02とstep04の本文矩形(18,116)-(338,340)は変更画素0。
  全画面差分bboxは(627,463)-(634,472)で時計部分のみ。step07でq後のdesktop復帰を確認。

現在観測した640×480構成での部分検証であり、全描画バックエンドの合格ではない。
実ROM字形の全ケース、細分化した各方向のclip、長時間停止保持、性能、配備同一性は未受入。

### 停止状態の20秒保持と復帰

- `20260908-232742` はfixture 3へのキー送信が抜けていたためFull保持の証跡には不採用。
  ローカルAIのdone/OKを台本の全操作完了とみなさず、操作ログで欠落を検出した。
- `20260908-232909` でfixture 3を別途選択・撮影後、`20260908-232934` で20秒待機。
  feed:Full、consumed=2561、unconsumed=1、pending=0058を保持。
- `20260908-233024` はfixture 5を選択し20秒待機。
  finish:Full、consumed=2562、unconsumed=0、pending=FFFDを保持。
- `20260908-233118` はfixture 4を選択し20秒待機。
  feed:TooWide、consumed=3、unconsumed=1、pending=65E5を保持。
- 各待機の実行をsteps.jsonlで確認。fixture 5/4のstep02とstep04は
  主窓矩形(8,8)-(360,360)のRGB差分なし。各試験後の1でcomplete/229 bytes/
  pendingなし/top=0へ戻ることを画面照合。最終233118/step08でq後のdesktop復帰を確認。

これは20秒間隔の表示安定性と復帰の確認であり、内部再試行回数やCPU負荷の測定、
長時間耐久試験の代わりにはしない。
