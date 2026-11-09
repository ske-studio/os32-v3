# 32 ビット OEA MMU

> 上位: docs/ppc/README.md

## BAT とページ変換

BAT はブロックの実効番地と物理番地を対応させる。IBAT と DBAT は別に設定する。
NetBSD の macppc ブートでは両方の upper を無効化してから lower/upper を設定する
(N/sys/arch/macppc/stand/ofwboot/Locore.c:91)。
ここで用いる 601 以外の BAT の定義は N/sys/arch/powerpc/include/oea/bat.h:80。

| ワード | マスク | 意味 |
|---|---|---|
| upper | `~0x1ffff` | 実効ブロック先頭 |
| upper | `0x00001ffc` | 通常のブロック長 BL |
| upper | `0x2` / `0x1` | supervisor / user で有効 |
| lower | `~0x1ffff` | 物理ブロック先頭 |
| lower | `0x40` / `0x20` / `0x10` / `0x08` | W / I / M / G |
| lower | `0x3` | 保護 PP。`2` は読み書き、`3` は読取り |

通常の長さは 128 KiB〜256 MiB。BL は順に `0`〜`0x1ffc`。
7455+ の拡張長は 512 MiB〜4 GiB として別に定義される
(N/sys/arch/powerpc/include/oea/bat.h:128)。
[未] 7450/7455 の BAT 本数・拡張を有効にする条件・優先順位はメーカー資料で確定する。

## セグメントと HPT

実効番地の上位 4 ビットが 16 本のセグメントレジスタの選択に対応する。
セグメント内のページ番号はビット 12〜27、ページ内は下位 12 ビット。
したがって 1 セグメントは 256 MiB、ページは 4 KiB
(N/sys/arch/powerpc/include/oea/pte.h:115、同 :165)。

| SR のマスク | 意味 |
|---|---|
| `0x80000000` | T。0 が memory 形式 |
| `0x40000000` / `0x20000000` | supervisor / user の保護キー |
| `0x10000000` | 実行不可 |
| 下位 24 ビット | VSID |

ハッシュ入力は VSID とセグメント内ページ番号の XOR。
NetBSD は表のマスクで一次 PTEG を選び、一次が埋まれば反転した二次側を調べる
(N/sys/arch/powerpc/oea/pmap.c:596、同 :847)。
PTEG は 8 個の PTE。32 ビット OEA の PTE は上位・下位各 32 ビットなので
1 PTEG は 64 バイトになる (N/sys/arch/powerpc/include/oea/pte.h:49)。

| PTE | マスク | 意味 |
|---|---|---|
| hi | `0x80000000` | valid |
| hi | `0x7fffff80` | VSID、7 ビット左シフト |
| hi | `0x40` / `0x3f` | 二次ハッシュ印 / API |
| lo | `~0xfff` | 物理ページ番号 |
| lo | `0x100` / `0x80` | 参照 / 変更 |
| lo | `0x40` / `0x20` / `0x10` / `0x08` | write-through / cache inhibit / coherency / guarded |
| lo | `0x3` | PP 保護 |

根拠は N/sys/arch/powerpc/include/oea/pte.h:77 と同 :91。
保護は SR のキーとの組合せで読む。ヘッダの `PTE_EXEC=0x200` はソフトウェア用で、
G4 のページ単位のハードウェア NX ビットと解釈しない (同 :110)。

SDR1 (SPR `0x019`) は HPT の基底を指定する
(N/sys/arch/powerpc/include/oea/spr.h:42)。
NetBSD の OEA 設定は表の基底に `(PTEG 数−1) >> 10` を OR し、
`sync` → SDR1 書込み → `isync`、その後 TLB 無効化を行う
(N/sys/arch/powerpc/oea/pmap.c:3570)。OEA64 分岐の式を流用しない。
[未] SDR1 の予約ビット・許される表サイズと基底整列、PTE 更新と TLB 同期の全条件は
CPU マニュアルの取得後に補う。本節だけで MMU 更新手順が完結するとはしない。
