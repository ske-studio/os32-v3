# [P6] その他

> 索引: [`00_INDEX.md`](00_INDEX.md)
> 優先度: **低**

[`FEP_FUTURE.md`](FEP_FUTURE.md)「その他」のうち、変換キー対応と kprintf va_args 調査。

---

## 1. 変換キー (XFER) 対応

`KEY_XFER` (0x35) を変換操作に割り当てる。[`01_UI_CANDIDATE.md` §3](01_UI_CANDIDATE.md) のキーバインド表で定義済み:
- 通常入力中の XFER = Space と同等 (変換開始)。
- 候補選択中の XFER = リスト展開 / 次ページ。

`ime_process_key()` で `scancode == KEY_XFER` を `KEY_SPACE` と同じ分岐へ
合流させる (`KEY_NFER` 0x51 は「無変換確定」= かな直接確定に割当も検討)。

```c
/* 変換開始判定を XFER にも拡張 */
if ((scancode == KEY_SPACE || scancode == KEY_XFER) && g_ime.kana_len > 0) {
    ... /* 既存の最長一致検索 */
}
```

| キー | scancode | 割り当て |
|------|----------|---------|
| `KEY_XFER` (変換) | 0x35 | 変換開始 / 候補送り (Space 相当) |
| `KEY_NFER` (無変換) | 0x51 | かな直接確定 (Enter 相当, 任意) |

> P1 着手時にキーバインド表へ織り込むのが効率的だが、本項目自体は独立して後付け可能。

---

## 2. kprintf va_args 問題の調査

[`FEP_FUTURE.md`](FEP_FUTURE.md)「`.sqlite_text` セクションからの `kprintf` 呼び出しで Page Fault」。

調査タスク (設計というより原因究明):
1. `ime_dict.c` の `kprintf(ATTR_RED, "... %s ...", path, rc)` 等が
   `.sqlite_text` セクション配置時に可変長引数スタックを正しく辿れているか。
2. リンカスクリプトで `.sqlite_text` のアライメント/配置を確認。
3. 暫定回避: 辞書エラー出力を可変長引数なしの固定文字列 `kputs()` に置換し、
   Page Fault が消えるか切り分け。
4. 根本原因が va_args ABI なら、該当呼び出しを `kputs` + 数値専用 `kput_hex` に分解。

→ [`03_DICT_COMMAND.md`](03_DICT_COMMAND.md) (辞書管理) でエラー表示を増やす前に切り分けておくこと。
詳細は [`../sqlite/07_OBSTACLES.md`](../sqlite/07_OBSTACLES.md) と突き合わせる。

---

## 3. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `kernel/ime.c` | `ime_process_key` に XFER/NFER 分岐 |
| `kernel/ime_dict.c` | (調査次第) `kprintf` → `kputs` 置換 |

**テスト:** XFER キーで変換が起動すること。辞書ロード失敗時のエラー出力で
Page Fault が発生しないこと。

---

*前: [`05_GFX_MODE.md`](05_GFX_MODE.md) ／ 次: [`07_BUNSETSU.md`](07_BUNSETSU.md) ／ 索引: [`00_INDEX.md`](00_INDEX.md)*
