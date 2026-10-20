CHECK_PAR_ORDER += 093:check-gui-text-host
# T7b-b4 S5: libos32gui text() の字形 engine mask 経路 (画素一致・clip・透過・オフスクリーン全角・
# baseline・trim/EV_FONT 後の再生成)。製品の draw.rs/font.rs と実 font、KAPI と libos32gfx は贋物。
check-gui-text-host:
	python3 -B tools/tests/test_gui_text.py $(MUT)

.PHONY: check-gui-text-host
