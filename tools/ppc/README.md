
## PowerPC の像と層の検査

製品は `make ARCH=ppc PLATFORM=powermac`（既定の `IMAGE=powermac`）で組む。
診断像と旧旗の互換出力は [像の選択表](../../docs/design/build/select.md) に従う。
PPC_CROSS_DIR の既定は `/home/hight/opt/cross-ppc`。
層の検査は `python3 -B -m tests.infra.ci run --check include_layers` と
`python3 -B -m tests.infra.ci run --check link_layers` で実行する。

遠隔シェルの受入は `python3 -B -m tests.infra.ci run --check ppc_link_session`、製品変異は同じ入口の `--mutate` を使う。
未移行の外部手動観測には `link_sh_run.py` と `root_lz4_layout_run.py` を使う。
前者はシェル観測用の `tests/support/fixtures/ppc_link/flow.inc` を読み、後者は `--inventory` で渡された計測表を使う。生成手順は [root LZ4 設計](../../docs/decisions/ppc/PPC_ROOT_LZ4_DESIGN.md) の「手動観測用の計測表」を参照。

USB と RPC の受入は `run --check ppc_usb_lan`、半行中の遠隔要求は `run --check ppc_usb_lanq` を使う。
