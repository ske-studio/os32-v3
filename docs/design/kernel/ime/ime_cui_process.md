# CUI のキー 1 件の変換

`kernel/ime/ime_cui_process.inc` は取得済みの CUI キー 1 件を処理する。
Shift+Space は ON/OFF を切り替え、確定バイトがあれば 1 バイト返す。
それ以外は OFF なら元のキーを返し、ON なら既存の配送処理を呼ぶ。

待機・非待機、ASCII の抽出とゼロの扱い、services_public の判定、
con_sink の迂回、入力 owner の記録、中断後の取消は各入口に残す。
GUI は WM がモード切替と透過キーを保持するため、この関数を使わない。
確定列と保留キーの順序は `ime_delivery.inc` が扱う。
検証は `tools/tests/x11_shared_host_l45.inc` と既存の IMETAB 各経路。
