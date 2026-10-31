# hwdev/gf/bus.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/bus.c`

read/write の別名、AR の FF reset→index→data、DAC の write-only auto-increment、LIVE RMW の実行時読取り、既存の幅/範囲/有限待ちを保持する。新しい汎用 IR・計画 VM は作らない。
identity/BAR が変わった時は危険な GF MMIO を省いても可能な route00 を試す。cfg50/54・1850/1854・Command の legacy 解放未確認なら 98 VRAM に触れない。失敗を記録して再起動へ進み、ログ失敗で戻しを止めない。
表示中も Command=0102、cfg50/54・1850/1854=0、VGA は BAR0 別名を使い実 3Cxh I/O は使わない。98 の同期/31kHz を不要に送り直さない（`O/kdebug/gfinit_bus.c:91–99`）。CUI ログは 98 に残し、復帰後の文字/入力更新を確かめる。
baseline 値採取と局所復元だけ必要範囲を共有化。診断 recover 型の adapter を残す。

## 移し替えた設計

## 5. PC-98 の VGA 別名、待ち、PLL

| 対象 | 実装・根拠 |
|---|---|
| VGA I/O | 3xxhへin/outしない。3C2/3CC・3C3・3C4/5・3CE/FはBAR0+0C0000h+head×2000h+port、3D4/5はBAR0+601000h+head×2000h+port。`R/envytools/rnndb/display/nv3_prmvio.xml:11–15`、`nv3_pcrtc.xml:23–30`、`nv_vga.xml:336` |
| indexed処理 | indexへの書きもW権限が必要。条件評価・選択読取も書きop内だけ。CR44はhead0に書き、値3なら以後head1（`I:240–260`）。PRMVIOにもhead×2000hを加える（`R/envytools/docs/hw/mmio.rst:95`）。 |
| 除外ポート | 3C9・3DA/3BA等の副作用読取、未知port・indexは拒否。IO命令でread側が未定義のportは勝手に0を返さず、資料に基づく別名/意味が確定するまで止める |

差分：PC-98ではVGA実I/Oを使わずBAR0の8bit別名を使い、I/O/BM=0・Mem=1を保つ。

番地はBAR0相対hex、MMIOは32bit、CR/SR/GR/ARは8bit。`s(x)` はmain4後・Q前に採る値、`r(x)` はその操作直前値。CR=6013D4/D5、SR=C03C4/C5、GR=C03CE/CF、AR=6013C0/C1・FF reset=6013DA（`V/nvreg.h:129–139,177–187,298`）。

- MISC は専用 VGA_DIRECT target とし、ログの書込み先 C03C2 を保ちつつ bus の読取りだけ C03CC 別名へ写す（nouveau dispnv04/nvreg.h:129,137）；AR/DAC をこの型や通常 MMIO に置換しない。

- gfinit_busはrecoverの解決済みtupleをdescriptorと照合し、0:13.0の生ID・Mem・全BAR一致後に窓へ渡す。
