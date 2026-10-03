CHECK_PAR_ORDER += 033:check-gui-proto
# GUI 共有プロトコル (os32_gui_shared.h ⇄ proto.rs) の定数・構造体の照合。
# GUI v1.2 の G0 (契約凍結) のゲート。PM 所有 (docs/archive/gui_v12/TASKS.md §5)。
check-gui-proto:
	@python3 tools/check_gui_proto.py

.PHONY: check-gui-proto
