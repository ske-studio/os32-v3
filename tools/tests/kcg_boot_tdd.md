# KCG の旧 font 撤去 (T7a-1b)

`check-kcg-boot-host` は実物 drivers/kcg.c を ILP32 足場へ取り込み、path/NULL の旧 load slot が起動時も NOSYS、VFS open 0、低位640KiB不変、ANK/漢字は ROM facade の字形であることを確認する。変異は scale 初期化の削除・旧 file fallback 再導入・旧低位 cache 再導入を実行時 rc=1 で検出する。

`check-kcg-module-host` は実物 body/facade/runtime を使い、満杯までの valid JIS 読みと衝突置換、module の頁確保失敗・欠落・init 失敗時の stub と回収を確認する。固定 corpus は JIS 3021..307e/3121..3126 の100字を10回、同じ画面を2回。cold/warm の hit/miss と IN/OUT/WAIT を出力する。ROM は模擬 port のため性能・可読性・実 ROM hash の証拠にはしない。変異はタグ照合欠落、確保失敗後の非空白 stub を含む。ビルド失敗や signal は RED と数えない。

GUI/CUI 操作・実 ROM の1000字計測・配備予行は PM の NP21/W 受入で行う。CODE復帰/cache失効は T7a-2c に残す。
