CHECK_PAR_ORDER += 106:check-cat-linenum-host
# `cat -n` の行番号は行の先頭でだけ出る。実物の cmd_fs_shared.c + cmd_file.c を
# #include し、sys_write(1, ...) に出た全バイトを試験側の素朴な参照実装と 1 バイト
# ずつ突き合わせる。(a) 改行で終わるファイルの後ろに空の行番号を出さない、
# (b) IO_BUF_SIZE (65536) の切れ目で行が終わったことにしない (行頭の状態を
# 読み取りをまたいで持つ) の 2 つ。§6 は継承バグ台帳の「内蔵 cat が標準入力を
# 読まない」— 引数が無ければ FD 0 を読み、FD 0 は閉じず、引数があれば読まない。
# --mutate は否定側で、(a) (b) と §6 の 3 つを壊した版が RED になることを見る。
# 記録は tools/tests/cat_linenum_tdd.md。
check-cat-linenum-host:
	python3 -B tools/tests/test_cat_linenum.py --target $(MUT)

.PHONY: check-cat-linenum-host
