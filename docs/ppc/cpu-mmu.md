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
MPC7450UM 5.3.1、表 2-6 (Rev 3.1) では 7450/7451 は各 4 エントリ、
7455 は HID0[HIGH_BAT_EN] で追加の IBAT4〜7・DBAT4〜7 を有効にして各 8 エントリ。
各エントリが upper/lower の 2 SPR を持つ。「BAT レジスタ数」とエントリ数を区別する。
7455 の HID0[XBSEN] は拡張長 512 MiB・1/2/4 GiB を有効にする。
起動時に設定したらクリアしない条件がある (MPC7450UM 表 2-6、5.3.2.1)。
BAT の重複ヒットは優先順位で解決せずプログラミングエラー。
変換無効時にも valid な重複を避け、初回設定前に全 valid を落とす (同 5.3)。

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
通常の 32 ビット物理番地では SDR1 の仕様ビット 16〜22 は予約、
HTABMASK は末尾だけ連続した 1。HPT は 64 KiB〜32 MiB の 2 の冪サイズで、
基底も表サイズに整列する。例: 1 MiB は mask=15、基底 1 MiB 整列
(PEM 7.7.1.1〜7.7.1.2、表 7-21/7-22)。
36 ビット拡張時は HID0[XAEN] と HTABEXT/HTMEXT を含む別形式になるため、
この予約ビット規則を流用しない (MPC7450UM 5.5.1)。

## 更新時の同期条件 (MPC7450UM Rev 3.1)

表 2-33 では SDR1・SR・DBAT の変更前に `dssall; sync`、
変更後に `sync` と context synchronizing 命令 (例 `isync`) が必要。
AltiVec data streaming を使用しない場合に限り dssall を省ける。
IBAT の変更後にも context synchronization が必要。
BSD の上記の短い列だけを CPU の全条件とせず、この表の条件を併記して採る。
MSR[IR] 変更直後の同期命令は変換前後の両番地で実行可能でなければならない (同表)。

局所 CPU の `tlbie` 完了には後続の `sync`、複数 CPU の TLB 無効化完了には
`tlbsync` が必要。さらに `tlbsync` 後は `sync` が完了してから他 CPU が
`tlbie/tlbsync` を行うよう直列化する。違反は livelock の危険がある
(MPC7450UM 5.4.4.2.3)。ブロードキャストには HID1[ABE] も関係する (表 2-7)。
[未] PTE の valid・R/C 更新と競合するハードウェア探索を含めた全更新プロトコル。
以上のレジスタ/TLB 条件だけでは PTE 更新手順は完結しない。
