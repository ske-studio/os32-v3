CHECK_PAR_ORDER += 096:check-edit-doc-host
# エディタ GUI 版の本文と libos32gui の桁・折り返し (票 TASK_EDIT_GUI 受入 E8 / E10)。
# 実物の userland/rust/edit_gui/src/doc.rs と
# userland/rust/libos32gui/src/textcore.rs を `#[path]` でそのまま取り込み、
# 保存の経路だけ KAPI を差し替えて回す (ゲストもエミュレータも要らない)。
# 見るのは 3 つ: (a) 桁で折り返すときに UTF-8 の途中で切らない (§4-27)、
# (b) 「見える範囲」が行を飛ばさない / 重ねない、(c) 書けなかったのに成功と
# 答えない。加えて **WK_TEXTBOX の振る舞いが変わっていない** ことを、
# textbox が通っている共通の下請け (textcore) の変異が RED になることで見る
# (受入 E10。textbox が下請けを通っていること自体は静的に突き合わせる)。
# --mutate は否定側。記録は tools/tests/edit_gui_tdd.md。
check-edit-doc-host:
	python3 -B tools/tests/test_edit_doc.py $(MUT)

.PHONY: check-edit-doc-host
