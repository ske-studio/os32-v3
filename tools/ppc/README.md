
## PowerPC の像と層の検査

PowerPC の製品の像は `make ARCH=ppc PLATFORM=powermac [IMAGE=kboot-*]` (または
`make ARCH=ppc PLATFORM=powermac` に `KBOOT_LINK`・`KBOOT_SH`・`KBOOT_SHELL` の旗) で組む。
ソースの一覧は x86 と同じ所有者集合 (`build/select.mk`) から引く
(設計は docs/design/build/select.md)。PPC_CROSS_DIR の既定は `/home/hight/opt/cross-ppc`。
CI が組む 4 像は `tools/ppc/compile_ci.py` の `IMAGES`。

共通のソースが機種のヘッダを読んでいないかは、組んだ像の `.d` から
`python3 -B tools/check_layers.py --image ppc-powermac` が数える
(基準は `tools/layers_baseline.json`、設計は docs/design/tools/check_layers.md)。
旧 x86 一覧による ARM 計測は別系列として維持する。
