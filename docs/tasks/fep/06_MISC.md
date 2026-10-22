# [P6] その他

> 索引: [`00_INDEX.md`](00_INDEX.md)
> 優先度: **低**

[`FEP_FUTURE.md`](FEP_FUTURE.md)「その他」のうち、変換キー対応と kprintf va_args 調査。

---

## 1. 変換キー (XFER) 対応

XFER の候補操作は実装済み。インライン候補中はリスト展開、リスト中は次候補となる
(`kernel/ime.c:367`)。入力中の変換開始は Space のみ (`kernel/ime.c:484`) で、XFER 対応は残件。操作の表は [P1 §3](01_UI_CANDIDATE.md#3-キーバインド設計) を参照。
`KEY_NFER` のかな直接確定への割当は将来の検討事項として残す。

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
詳細は `docs/archive/v21/sqlite/07_OBSTACLES.md` と突き合わせる。

---

## 3. 影響範囲・テスト

| 対象 | 変更 |
|------|------|
| `kernel/ime.c` | `ime_process_key` に XFER/NFER 分岐 |
| `kernel/ime_dict.c` | (調査次第) `kprintf` → `kputs` 置換 |

**テスト:** 変換候補中の XFER でリストが開き、リスト中は次候補へ進むこと。
入力中の XFER による変換開始は、対応を実装した後に検証する。辞書ロード失敗時のエラー出力で
Page Fault が発生しないこと。

---

*前: [`05_GFX_MODE.md`](05_GFX_MODE.md) ／ 次: [`07_BUNSETSU.md`](07_BUNSETSU.md) ／ 索引: [`00_INDEX.md`](00_INDEX.md)*

## 4. 候補ゼロ障害の記録 (2026-08-07)

**解決済み (2026-08-07):** 「漢字変換が候補ゼロになり SPACE がかな確定に
フォールバックする」不具合は、複合原因を特定して修正した。
1. `ime_set_mode()` (`ime` コマンドの `ime on` 経路) が辞書を開いて
   いなかった — `dict->db == NULL` のまま `ime_dict_search` が即 0 を
   返していた。→ `ime_toggle()` と同じ遅延オープンを追加。
2. 辞書を開けても、外部プログラム終了時の `exec_exit()` が FD 3 以上を
   一括クローズするため、FEP 辞書の SQLite fd が回収され以後の検索が
   全て `SQLITE_IOERR_READ` になっていた。→ VFS に FD 保護フラグ
   (`vfs_fd_set_protect`) を追加し、FEP 常駐接続の fd を保護。
MEMSYS5 プール枯渇説は 400KB 拡張テストで棄却 (200KB で動作確認済み)。
`ime_dict_search` には step エラー時のみ出る診断ログを追加した。
変換・候補送り・確定・学習 (`dict_user` への UPSERT と 5.7MB DB 末尾=
二重間接ブロック領域への永続化) を NP21/W 実機で検証済み。


これは当時の検証記録である。現行も `kernel/ime.c` の `ime_set_mode()` に遅延オープン、
`kernel/ime_dict.c:25` に辞書 FD の保護処理がある。
