# tools/accept/gf_accel.py — 保存済み計測の照合と統計

> 状態: **現行 (2026-11-01)**

実機に接続しない。3方式のシリアルファイルと `--build-id`、`--pair-crc` を受け、
GFA1/GFW1 行だけを読む。他の通常のシリアル行は無視する。Image CRC は kernel の
VK32 の CRC と部品の PAIR_CRC が同じ値。ID/CRC/方式、CPU/PCI、元のMSR/PTEが
一致しなければ rc=1。PTEの accessed/dirty bit だけは照合から除く。

GFA1 の先頭は `GFA1 ID MODE KIND`。KIND の後は次の通り。

| KIND | 内容 |
|---|---|
| pair | kernel Image CRC (十進u32) |
| state | UC/WC/unknown、OK/SKIP、理由 |
| cpu | leaf/subleaf/EAX/EBX/ECX/EDX (十進u32) |
| msr | phase(0=取得前/1=実効設定後)、MSR番号、lo、hi |
| pte | phase、表示頁番号0..74、raw |
| pci | Command、config 0x0c dword、BAR0/1/2 raw |
| pattern | `0..255` (カメラで見る期間の開始) |
| sample | case、run、1回のbyte、100回のpoint合計、転送失敗、不一致数、100個のµs |
| done | `OK` (合成転送の全ケース完了、実機受入の合格宣言ではない) |

0番は空測定、1〜8番は全面/320×200/640×16/32×32/16×16/(1,0,63,32)/
1×480/(639,479,1,1)。時間の 0xffffffff は失敗で正常群へ混ぜず数える。
欠落/重複、転送/readback失敗、設定に反する実効型、未完了列を拒否する。
各100個と合算300個に nearest-rank p50/p95/最大を出す。時計の空測定は引かず別欄。
CPU cache line は leaf4、なければ P6 leaf2 の既知の L1 descriptor、解読不能は unknown。
leaf1 EBX を CLFLUSH 非対応の P6 の line size として流用しない。

GFW1 は `GFW1 ID MODE sample seq total event composite present`、µsは十進u32。
300個揃った場合だけ内訳を集計し、正常値の和がtotalに等しいことを確かめる。
`--require-wm` は GUI 列の欠落も拒否する。BIOS WC 等の理由付き SKIP には比較値を
作らない。出力の JSON は数値と生観測であり、カメラ・WC復元・既存回帰を含む
合否は PM が判定する。
