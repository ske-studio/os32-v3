# T5a / 検証ツールの途中保存

> 発行: PM (2026-09-09) / 状態: **完了記録 (2026-09-10)**
> v1.3 は 2026-09-14 に受入完了。本書は当時の途中経過。

T5a全体は未受入。以下は今回の追加確認であり、残試験を省略して合格とはしない。

## 配備内容

既存hotdeploy後、ローカルAIがゲスト `/usr/bin/t5a_display.bin` を新規HostDrv証跡へコピー。
ホスト成果物とのSHA-256とcmpが一致。SHA-256:
`67ac9a447d580fdd29b00472b35b076327c9bf3d68f271ba823f3a8e9dd6605a`

証跡: `tools/emu_agent/logs/20260908-234112/steps.jsonl`。
ホストコピー: `/mnt/c/os32/t5a_readback_007c7a983cb74f419d7c158db2a5a378.bin`。

## PC98部分検証

`gfxmode pc98` → reset → hal_testでpc98 planar4 / 640x400を確認。
fixture境界・通常復帰を目視確認。
旧ラッパーで未指示CTRL+STOPが送られた試験は不成立として破棄し、ラッパーを削除。
新strict playbookのホスト41試験と独立レビュー通過後に再開した。

- `playbook-20260909-003112-1788881472896364746`: 非整列遮蔽を目視確認。
  step02とstep06は全画面RGB一致。
- `playbook-20260909-003229-1788881549926556028`: j後top=1、e後top=36、g後top=0と本文復元。
- `playbook-20260909-003335-1788881615023325883`: q後desktop復帰。
- `/tmp/t5a-strict-pc98-cui-confirm-session.jsonl`: CUI/rshell復帰、grph_disp=0、fault_generation=0。

sessionは `tools/emu_agent/logs/` 以下。ログ・画像はコミット対象外のローカル証跡であり、
別環境に存在することを保証しない。実行担当はローカルgemma4-it:e4b、画面照合はPM。

## ツール検証と残作業

strict runnerは台本順序違反を実通信前に拒否した実例を確認。
`/tmp/t5a-strict-preflight-session.jsonl` にstatus期待/screenshot提案/拒否、実行0件を記録。
その後の読取専用台本・限定クリック・キー・ドラッグは実行と画面を照合済み。

iniツールはホスト試験と独立レビューを通過。Windows一時ファイル試験でPS構文解析、
NullString、File.Replaceと読戻しは成功。実ini適用・復元、実プロセス停止起動は未実施。
現在のNP21/Wはexeのみの起動で、ini明示引数なしと読取専用照会で確認。
ツールの明示ini対象条件との接続は未解決。ini変更成功とはしない。

CirrusのT5a実行、バックエンド横断の残受入項目、性能測定は未完了。
ゲスト設定はGUI=0 / GFX=pc98。元設定はGUI=0のみで、元状態への復元も残作業。
ユーザーのコミット・push指示により、ここで実験操作を中断した。
