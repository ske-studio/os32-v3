# NP21/W ini: host-only TDD evidence (2026-09-08)

対象 4 パスは着手前に存在・symlink とも無いことを確認した。
既存ファイル・登録は変更せず、合成フィクスチャと一時ディレクトリのみで検証。

## RED → GREEN

1. テストを先に作成。
   `python3 -B -m unittest discover -s tools/tests -p test_np21w_ini.py -v`
   は exit 1、15 tests、`failures=1, skipped=14`。
   ファイル未実装を明示的な assertion で検出 (import error ではない)。
2. 未実装の最小 skeleton `transform(raw, changes): return raw, []` を置き、
   `python3 -B -m unittest discover -s tools/tests -p test_np21w_ini.py -k test_preserves_unrelated_bytes -v`
   を実行。exit 1、1 test、`FAILED (failures=1)`。
   `self.assertEqual(result, EXPECTED)` が `false` → `true` 未反映で失敗。
   **動作の assertion RED を確認してから本実装を開始した。**
3. 初回実装の全体試験: exit 1、15 tests、`errors=1`。
   非通常ファイルを `fdopen` 前に拒否できず `IsADirectoryError` になった。
   `fstat` による事前検査と FD 解放を追加し、15 tests がすべて成功。
4. 同一内容の inode 置換、改変レシート、復元 CLI dry-run、無効入力で
   成果物を作らないこと、最終 replace 直前の変更を加えて検証。
   最終コマンドは下記。exit 0、**Ran 20 tests / OK、skip なし**。

```bash
python3 -B -m unittest discover -s tools/tests -p test_np21w_ini.py -v
PYTHONPYCACHEPREFIX=/tmp/os32-np21w-ini-pycache python3 -m py_compile tools/np21w_ini.py tools/tests/test_np21w_ini.py
python3 -B ~/.codex/skills/.system/skill-creator/scripts/quick_validate.py .claude/skills/os32-emu-config
python3 -B tools/np21w_ini.py --help
```

構文検査 exit 0。スキル検査 exit 0、`Skill is valid!`。help exit 0。
全ホスト試験の名前・assertion は隣の `test_np21w_ini.py` にある。
主な検証: バイト保持、BOM/混在改行/最終改行なし、許可値、重複・欠落、
symlink (親も含む)/hardlink/非通常ファイル拒否、固有バックアップ、
atomic replace 失敗、readback 不一致、変更検知付き復元、
dry-run 無書き込み、実適用拒否時の入力未読、秘密フィールド非出力。

## 根拠と残課題

- 許可キー/値: `docs/archive/gui_v11/TASK_H3_cirrus.md` §0 と
  `docs/tasks/gui/TASKS.md` 2026-09-06 WAB OFF 再確認。
  `[NekoProject21] USEGD5430=true/false`, `GD5430TYPE=91` のみ。
  外部 NP21/W ソースの最初に試した `windows/ini.cpp`, `windows/ini.c`,
  `wab/cirrus_vga.c` パスは不存在だった。上記リポジトリ内文書を根拠とし、
  未確認のソースや実 ini を根拠としたとは扱わない。
- 既存ツールはテキストとしてのみ調査。`~/.local/bin/os32-cycle` は
  `.env` を source し、`emu_running()` は tasklist 照会エラーと不在を
  確実に区別しない。`tools/emu_agent/agent.py` の status は HTTP 照会。
  これらを信頼できる停止検証器として import/実行していない。
- **ライブ適用・ライブ復元は未実装。** `prepare --apply` は `--output` が
  無ければ入力を読む前に拒否。`--output` があれば新規オフラインバンドルだけを
  作り、入力スナップショットは変更しない。`restore` もバンドル内限定。
  停止証明を装う boolean 引数や任意シェル実行口はない。
- 残課題: 非機密で失敗時に確実に拒否するプロセス終了検証器、根拠付き実対象の
  解決、起動との排他、ローカル emu_agent/FLM の限定操作との統合。
  オフラインの flock は非協調プロセスとの競合を完全には防止しない。
- 実 ini の読書き、エミュレータ通信、プロセス停止/起動、`.env`/資格情報/
  `docs/hw` の読み込み、配備、ゲスト試験、commit、エージェント起動は未実施。
  OS32 本体の変更はなく、OS ビルドやゲスト試験は今回の範囲外。
