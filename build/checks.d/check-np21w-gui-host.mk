CHECK_PAR_ORDER += 127:check-np21w-gui-host
# GUI transport/observations/OCR/ring and MCP, entirely offline.
check-np21w-gui-host: assets/fonts/ipaexg16.kcgfont
	python3 -B tools/tests/test_np21w_gui.py $(MUT)
	python3 -B tools/tests/test_np21w_gui_mcp.py

.PHONY: check-np21w-gui-host
