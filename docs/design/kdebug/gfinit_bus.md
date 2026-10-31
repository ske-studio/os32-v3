# kdebug/gfinit_bus.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_bus.c`

baseline 値採取と局所復元だけ必要範囲を共有化。診断 recover 型の adapter を残す。

## GF6200_4A_DESIGN

- `pci_get` は列挙時の写し（`drivers/pci/pci.c:190–207`）。書きの直前には生 config を読み BDF・BAR・Command を再検証する。

A2はPSTRAPSから組を導いて3を確認し、冷間のCR3C=0では止めない。main0の0xdfa9でCR3C←0、0xdfafのCOPYで(CR3C&0Fh) OR 30hを書いた直後にCR3C=3xhを確認する。

## GF6200_4C_DESIGN

- CR/SR/GR は既存 target を共有し、各 READ/DATA の直前に同じ ctx/phase の SELECT を置く；C の DATA は共有 RW key でも local 保存を増やさず COLD とし、従来の index/lock 保存は保つ（nouveau hw.c:712–772,564–586、旧 timing を再生しない §4 の契約）。

- MISC は専用 VGA_DIRECT target とし、ログの書込み先 C03C2 を保ちつつ bus の読取りだけ C03CC 別名へ写す（nouveau dispnv04/nvreg.h:129,137）；AR/DAC をこの型や通常 MMIO に置換しない。

各行で 6013DA を読んで FF を reset、AR は 6013C0 に index→data、gate は同 port に 00 または 20 を書く。値・順は段 1 の生成表と mode state を使う。

6813C6=FF→6813C8=00→6813C9 に `gfinit_mode_palette` の RGB 768 byte を書く。操作ごとに gf_bus BEGIN/END 1 組、失敗は既存 trial_end に渡す。通常の read-safe MMIO target や汎用 bulk は追加しない。

## HWDEV_DESIGN

NV44A bus

## REALHW_RECOVER_ADDENDUM

- gfinit_busはrecoverの解決済みtupleをdescriptorと照合し、0:13.0の生ID・Mem・全BAR一致後に窓へ渡す

- 局所cleanupは保存済みCR11→CR1F→CR44 DATA→元indexの順とする；RUN許可列はCR1Fの解錠を保持する。LOCAL/SAVED SELECTには保存DATAの対象index選択だけを追加許可し、COLD DATAは再生しない。成功後は局所保存件数を0にして二重再生を防ぐ。

> [要確認] 後段の訂正は UNLOCK→CR11→CR44→LOCK→INDEX。現行の共有局所復元列との照合が必要。
