# hwdev/gf/display/pll_program.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/pll_program.c`

実 ROM の解は N1/M1=26/7、N2=M2=1、P=2、actual=25,071 kHz。要求値との差 104 kHz、約 0.41%。36 MHz 診断値を使わない。
| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| PLL `pll_program` | solver と 5 書込み、選択 50C・524 | `nvkm/subdev/devinit/nv04.c:200`、`dispnv04/hw.c:468` | 4F0B p.59／02 の内部 |

## 移し替えた設計

C040切離し→必要時580→578→508→C040復元

C040=r&~00030000→680580=(r&~100)\|single*100→680578=(r&7FFF0000)\|80000000\|NM2\|(single?011F:0)→680508=(r&FFF80000)\|(P<<16)\|NM1→C040=退避値

PLLは4aのparser/整数solverを**再利用して25,175 kHzを解き直す**。limitsのvco1.max/2以下ならvco2=0、単段N2=M2=1。36MHz係数は使わない。

- 実 ROM での 25.175MHz の係数 (2026-10-29、ノートで tools/gfinit_rom_decode.py、24b00c7a、ROM 63488B sha256 db7457d8…、rom_crc=3919973859、証拠はノートの ~/os32-tmp/evidence/2026-10-29/gf-4c-rom/DECODE.txt): VIDEO の行は先頭の行への fallback (前と同じ limits)。**単段 N1/M1=26/7・N2=M2=1・P=2、actual 25,071kHz (誤差 104kHz・0.41%、±0.5% の内)**、vco1 100,285kHz (下限 100,000 のすぐ上)。5 本: C040h←(keep FFFCFFFF)、680580h bit8=1、680578h=8000011Fh、680508h=00021A07h、C040h 復元。
