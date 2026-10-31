# kdebug/gfinit_resources/bars.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: BARLEN_DESIGN.md (D1 で移した)
ソース: `kdebug/gfinit_resources/bars.inc`

## BARLEN_DESIGN

| vendor:device / BAR | 長さ・属性 | 根拠と確度 |
| --- | --- | --- |
| 1023:9660 / 0 | 正確値未確定、基準構成の上限 **4 MiB** | MIT 実装は BAR0 を framebuffer として使うだけ (`/home/hight/os32-tmp/run/barlen/sources/netbsd-trident.c:2294`) |
| 1023:9660 / 1 | 正確値未確定、基準構成の上限 **64 KiB** | BAR1 選択 `/home/hight/os32-tmp/run/barlen/sources/netbsd-trident.c:2313`、64 KiB map は使用量 (`/home/hight/os32-tmp/run/barlen/sources/netbsd-trident.c:1568`) |
| 1023:9660 / 2 | 正確値未確定、基準構成の上限 **8 MiB** | 上記ドライバには本 BAR の長さを保証する根拠なし |

- 82557の上限はBAR0が64KiB、BAR2が1MiB。既知長4KiB/1MiBを優先。表長>capなら割当/表の矛盾として拒否する。

helper の処理順:

1. memory32 以外を `memory_bar_type` で拒否。64bit の上位 DWORD を別 BAR として扱わない (下位で即終了)。

2. endpointのID:BAR表を検索。未登録でも§2のTrident許可条件に合えば上限を使う。それ以外は `memory_bar_length`。

3. 下位nibbleが表/上限許可条件のattrと異なれば `memory_bar_attr`。base非零を確認してcapを算出する。

4. 表長は非零・2冪・base整列を検査し、表長>capは `memory_bar_bound`。表なしはlen=cap (距離由来のcap自体に2冪/整列は要求しない)。非零と64bit整数のbase+len<=2^32を検査、不一致は `memory_bar_range`。

5. used >= RECOVER_RANGE_MAX は `memory_bar_count`。合格した `{base,len}` を1本ずつ append。

- 未知装置は「有効な非零 memory BAR がある」場合に拒否。BAR 全零・I/O だけ・Mem無効まで装置IDだけで拒否しない。 既知装置でも表/上限許可にないmemory BARは拒否。表にないI/O BARは従来どおり読み飛ばす。

- 長さとアドレスを分離し、82557 の現配置を定数にしない。正しく整列した移動先にも同じ長さを適用する。
