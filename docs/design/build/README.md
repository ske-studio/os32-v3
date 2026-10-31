# build/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_VPLL_PROBE・HWDEV_CORE_API・HWDEV_DESIGN・SLIM_HWDEV_DESIGN・GFX_MODES_DESIGN・VER_INFO_DESIGN (D1 で移した)
ソース: `build/`

gfinit*.c/.h の全関数・データ、recover_gf.c の template/許可表/式/固定 plan
ソース位置を維持して HWDEV 像へリンク。

### hwdev

有効化はビルド定義 `GF_VPLL_PROBE=1` のみ（既定0、追加経路は条件コンパイルで除外）；有効版のGF profileに専用version=56504C03hを割り当て整合検査も対応

- 入口: `make GF_VPLL_PROBE=1 gf-vpll-probe` → `$(BUILD_OUT)/gf-vpll-probe/{kdebug.bin,hwdev.mod}`。診断の object・ELF・map・pair.h はその下に分け、-DGF_VPLL_PROBE=1 は診断の対象のレシピだけ。既定の all と成果物は変えない。診断の core は既定の vmkernel.lz4 に対して pack し、gen_hwdev_pair.py で組を作る (カーネルは組み直さない)。世代は据置き、GF profile の version=56504C03h と profile CRC・image/pair の CRC は既存の生成器で出す (build/kdebug.mk:34-58、build/hwdev.mk:11-38、tools/gen_hwdev_pair.py:11-26)。

- gfinit.c・gfinit_resources.c は `kdebug/generated/kdebug_import_macros.h` を取り込むが getter マクロは使っていない — 取り込みを HWDEV の生成物に差し替える。

> [要確認: 計画の時制?]

- `build/hwdev.mk` は同じ machine/ABI flags で GF 部品オブジェクトを別にコンパイルし、`hwdev_main.o` と専用 imports を `build/out/hwdev/hwdev.elf` (map 同居) へリンク、`build/out/hwdev.mod` へ pack する。`build/hwdev.ld` は HWDEV slot 配列を data 先頭に置く。

- kdebug.binのrecover.o（build/kdebug.mk:13）は無変更。表と展開コードはbuild/hwdev.mk:3–8のhwdev側だけ。

gfinit_decode.oだけ末尾に-Osを付けるtarget-specific KERNEL_CFLAGSをbuild/hwdev.mkへ追加。
- R1は通常版と同じ.oを使う（build/hwdev.mk:27）ため、指定は$(HWDEV_OUT)/gfinit_decode.oの1か所で足りる。

> [要確認: 計画の時制?]

- decode の target-specific KERNEL_CFLAGS は private とし、末尾の -Os が共有の契約生成などの前提ターゲットへ継承されないようにする；通常版と R1 は従来どおり同じ gfinit_decode.o を使う。

### kdebug

gcap/math、NIC trace、selftest、CRT、main、peek/allow、window、recover/profile、
imports を同じ像へリンク。kexports は kernel 側

### cirrus-module

`build/cirrus-module.mk` の Cirrus の ABI の値の生成の依存一覧に `include/gfx_mode.h` を足す

> [要確認: 計画の時制?]

### kernel

- build/kernel.mk:57–65 は .FORCE で tools/gen_build_id.py を実行し、内容変更時だけ build_id.c を更新。
