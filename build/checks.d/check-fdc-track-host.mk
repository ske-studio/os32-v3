CHECK_PAR_ORDER += 091:check-fdc-track-host
# FD のトラック単位の読み出し (drivers/fdc_track.c) と、同じシリンダでの
# シークの省略・まとめ読み (drivers/fdc.c)。実機 PC-9821Ra266 で既定フォント
# (188KB) の読み込みが 1 分以上止まった件 (1 セクタごとに SEEK + ほぼ 1 回転)。
# 本物の fdc.c を µPD765A の模型 (tools/tests/fdc_hostshim/io.h) の上で回す。
# 変異は一時の木の写しに当てるのでソースは書き換えない。
# 記録は tools/tests/fdc_track_tdd.md。
# font_replay (実物のフォントの読み方の再現) は FD イメージを読む。無いまま
# SKIP で通らないように --require-image で FAIL にする (ラリー 2 の Fable)。
# **イメージを前提 (prerequisite) にはしない** — d88 の規則は phony の boot に
# 依るので毎回作り直され、そのたびに kernel も組み直される (kapi_sys.o の
# __DATE__ __TIME__)。check-par の中でそれが起きると vmkernel.lz4 と 1.44MB の
# イメージ・ISO が食い違い、check-packages-host / check-vk32-crc-host が落ちた
# (2026-09-24、PM 判断で「無ければ失敗」に)。make all が先。
check-fdc-track-host:
	python3 -B tools/tests/test_fdc_track.py --target $(MUT) --require-image

.PHONY: check-fdc-track-host
