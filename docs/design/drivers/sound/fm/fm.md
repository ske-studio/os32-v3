# sys/dev/sound/fm/fm.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (T2e の KAPI の範囲検査の欠落の修正) の契約を今のソースと照らして要約
ソース: `sys/dev/sound/fm/fm.c`

## 引数の範囲

- `fm_set_tone_num(ch, tone_num)`: `0 <= tone_num < NUM_TONES` のときだけ音色表を引く。負の値・上限以上は何もしない。
- `fm_note_on(ch, note)`: ch が `0 <= ch < OPN_CHANNEL_COUNT` の外、または `note < 0` なら OPN に書かずに戻る。オクターブ (block) は 7 で頭打ち。

検査は `tools/tests/test_kapi_bounds.py` (実 `fm.c` を組み、負の音色・音階で表の外を読まないこと)。
