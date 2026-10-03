CHECK_PAR_ORDER += 098:check-kstring-c-host
# kstring の C 版 (移植性準備の順序 4-b)。実物の lib/kstring_asm.asm (nasm) と
# 実物の lib/kstring_c.c を **同じ実行ファイルにリンク**し、13 本すべてを同じ
# 入力で突き合わせる (戻り値とバッファの全内容が一致すること)。x86 の既定
# ビルドはアセンブリのままなので、これは切り替えの門ではなく答え合わせ。
# --mutate は否定側 (kstrcmp を符号付きにすると日本語ファイル名の並び順が
# アセンブリ版と食い違って落ちる、など)。記録は tools/tests/kstring_c_host.c。
check-kstring-c-host:
	python3 -B tools/tests/test_kstring_c.py $(MUT)

.PHONY: check-kstring-c-host
