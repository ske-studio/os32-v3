CHECK_PAR_ORDER += 012:check-kprintf-attr-host
# kprintf の属性変換 (lib/kprintf_attr.c)。呼び出し側の 70 か所以上が渡す
# PC/AT (CGA) 流の 0x07 などを、PC-98 のテキスト属性 (bit0 = 表示 /
# bit5,6,7 = 色) へ直す。直さないと属性 VRAM へ「色無し + リバース +
# ブリンク」が書かれ、**診断行が画面に 1 文字も出ない** (実機
# PC-9821Ra266 の FD 起動を追えなかった原因、2026-09-22)。
# **エミュレータでは踏めない** — /api/tvram は文字コードしか返さないので、
# 属性が壊れていても「出ている」ように読める。
# --target はカーネルと同じ i386-elf で lib/kprintf.c ごと通す。
# 記録は tools/tests/kprintf_attr_tdd.md。
check-kprintf-attr-host:
	python3 -B tools/tests/test_kprintf_attr.py --target $(MUT)

.PHONY: check-kprintf-attr-host
