CHECK_PAR_ORDER += 045:check-gui-desc-host
# 実物の gshell host メモリ像と Python 読み手の往復。変異は正常の対照の後。
check-gui-desc-host:
	python3 -B tools/tests/test_gui_desc.py $(MUT)

.PHONY: check-gui-desc-host
