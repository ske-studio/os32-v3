
## PowerPC の像と層の検査

PowerPC の製品の像は `make ARCH=ppc PLATFORM=powermac [IMAGE=kboot-*]` (または
`make ARCH=ppc PLATFORM=powermac` に `KBOOT_LINK`・`KBOOT_SH`・`KBOOT_SHELL` の旗) で組む。
ソースの一覧は x86 と同じ所有者集合 (`build/select.mk`) から引く
(設計は docs/design/build/select.md)。PPC_CROSS_DIR の既定は `/home/hight/opt/cross-ppc`。
CI の 6 指定は `.github/workflows/restructure-build.yml` の powerpc ジョブで確認できる。
層の検査は `python3 -B -m tests.infra.ci run --check include_layers` と
`python3 -B -m tests.infra.ci run --check link_layers` で実行する。

外部の手動観測には `link_sh_run.py` と `root_lz4_layout_run.py` を使う。
前者はシェル観測用の `ppc_shell_flow.inc` を読み、後者は `--inventory` で渡された計測表を使う。
