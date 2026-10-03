CHECK_PAR_ORDER += 087:check-pci-decode-host
# PCI コンフィギュレーションの復号 (drivers/pci_decode.c)。実機 PC-9821Ra266 の
# 内蔵 LAN (Intel 82557) を `lspci` で見つけるための土台 (票 TASK_LAN_82557 L-A)。
# **NP21/W は PCI を実装していない** (`0CF8h` が無い) ので、ここはエミュレータ
# では 1 ビットも踏めない — 走らせて確かめられるのは実機だけで、その 1 回は
# シリアル 115200 での会話。読み違いは全部ここで潰す。
# 見るのは 6 つ: 0CF8h のアドレス語の組み立て (欄をマスクしないと範囲外が隣の
# 欄へ溢れて別のデバイスを読む)、有無を探る値が**本物が保持できる形**であること
# (bit1〜0 を立てると PCI があっても「無い」と答える)、BAR の種別
# (**生値 1 = 番地未割り当ての I/O BAR は「無い」ではない** — 票 R1 が消える)、
# 番地のマスク (I/O は ~3。~0xF で切ると 0xE808 が 0xE800 に化ける)、
# Header Type の bit7 (落とさないとマルチファンクションのブリッヂを見落とす)、
# DWORD からの 8/16 ビットの切り出し。
# --target はカーネルと同じ i386-elf で drivers/pci.c ごと通す
# (列挙は I/O を触るのでホストでは回せないが、型のずれは手元で捕まえる)。
# --mutate は写しの上で変異させるので並列 (check-par) で回せる。
# 記録は tools/tests/pci_decode_tdd.md。
check-pci-decode-host:
	python3 -B tools/tests/test_pci_decode.py --target $(MUT)

.PHONY: check-pci-decode-host
