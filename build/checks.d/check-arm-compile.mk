# ARM コンパイル計測 (移植性準備の順序 1)。カーネル側の C ソースを 1 本ずつ
# arm-none-eabi-gcc に通し、通った本数と失敗の分類を出す。
#
# **`check` の列にはわざと入れていない。** これは合否の門ではなく計測器で、
# 今 ARM で通らないのは当たり前 (x86 前提でよい、と決めて書いてある)。
# 門にすると「直さないと緑にならない」圧力がかかり、まだ設計の決まっていない
# arch/ の分離を急がせてしまう。io.h 経由への統一 (順序 2) や arch/ 導入
# (順序 3) の効果を同じ物差しで見るために、独立したターゲットとして呼ぶ。
#
# arm-none-eabi-gcc が無い環境では SKIP して終了コード 0。
check-arm-compile:
	@python3 tools/check_arm_compile.py

.PHONY: check-arm-compile
