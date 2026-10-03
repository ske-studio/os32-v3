CHECK_PAR_ORDER += 047:check-fdc-seek-host
# FDC のシーク判定 (drivers/fdc_decide.c)。実機 PC-9821Ra266 の FD 起動が
# MOUNT... の root panic になっていた件。見るのは **エミュレータでは踏めない**
# 2 つの分岐: pending 無しの SENSE INTERRUPT STATUS が ST0 だけの 1 バイト
# 応答になること (2 バイト読むと来ないバイトを空転して待つ) と、RECALIBRATE の
# EC (77 ステップでトラック 0 に届かない) が失敗ではなく再試行の合図であること。
# --target はカーネルと同じ i386-elf で drivers/fdc.c ごと通す。
# 記録は tools/tests/fdc_seek_tdd.md。
check-fdc-seek-host:
	python3 -B tools/tests/test_fdc_seek.py --target $(MUT)

.PHONY: check-fdc-seek-host
