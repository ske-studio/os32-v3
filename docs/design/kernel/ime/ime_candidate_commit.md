# 候補確定後の入力表示への復帰

`kernel/ime/ime_candidate_commit.inc` は数字選択・Enter・その他のキーで
共通の候補確定と表示更新を行う。`ime_convert.inc` が取り込む。

候補コピー・学習・残りかなの移動の後、入力状態へ戻し、スクロール保護を
1 行に戻し、候補リストを復元する。残りかながあれば再描画し、なければ
プリエディットを消す。この順序を保つ。

選択する候補、通常文字の追記、透過キーの返値は呼出し元が決める。
検証は `tools/tests/ime_display_host.c` と `tools/tests/test_x11_shared.py`。
