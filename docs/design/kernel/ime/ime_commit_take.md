# 確定バイトの取得

`kernel/ime/ime_commit_take.inc` は未読の確定バイトを符号なし整数で返し、
読出し位置を 1 バイト進める。未読がなければ位置を変えず -1 を返す。

CUI の配送、Shift+Space による確定、GUI の確定列の回収で共用する。
キー取得・モード切替・services_public・owner 記録は呼出し元に残す。
GUI は CUI の保留キーを取得しない。配送順は `ime_delivery.inc` が扱う。
検証は `tools/tests/test_x11_shared.py` の通常対照と実行時変異。
