# キャッシュ同期とメモリ順序

> 上位: docs/ppc/README.md

## 命令を書き換えた後の同期

データ側に書いた命令列を命令側が直ちに見るとは限らない。
FreeBSD の `__syncicache` は以下の順で対象範囲を処理する
(F/sys/powerpc/powerpc/syncicache.c:48)。

1. 開始番地をキャッシュライン先頭へ切り下げ、範囲の端数も含める。
2. 全対象ラインに `dcbst` を行い、データ側の内容を書き戻す。
3. `sync` を行う。
4. 同じ対象ラインに `icbi` を行い、命令側を無効にする。
5. `sync` → `isync` を行ってから命令を実行する。

これは BSD の実装で確認した局所的な手順。ライン幅は変数 `cacheline_size` を使う
(同 :53)。全 G4 の値を固定する根拠にはしない。
NetBSD のモジュール読込みもロード後に `__syncicache` を呼ぶ
(N/sys/arch/powerpc/powerpc/kobj_machdep.c:182)。
[未] SMP の他 CPU に対する同期、別名マッピング、DMA と命令取得の関係は別途確認する。

## メモリと I/O の順序

`sync` は上のデータ書戻しと命令無効化の間にも必要とされ、
`isync` は最後の命令実行側の同期に使われる。片方だけでこの列を代用しない。
FreeBSD は `eieio` をメモリ clobber 付きの命令として公開する
(F/sys/powerpc/include/cpufunc.h:184)。
NetBSD の OEA 用 I/O バリアは `eieio; sync` を組み合わせる
(N/sys/arch/powerpc/include/pio.h:47)。
これは BSD の I/O アクセサの選択で、あらゆるロード・ストアに対する
`eieio` と `sync` の等価性を意味しない。
以下はメーカー資料で確認した追加条件。

## 745x の命令条件 (MPC7450UM Rev 3.1)

2.3.4.3.1 は上の `dcbst (または dcbf); sync; icbi; sync; isync` と一致する。
icbi 後の sync は I-cache 無効化完了、isync は自 CPU の命令バッファを捨てるため。
L1 のブロックは 32 バイト (3.1)。他 CPU の命令バッファ同期をこの列だけで保証しない。

3.4.4.4 の dcbst は番地変換・保護判定では load として扱われ、違反は DSI、
STEN=1 の変換 miss は data TLB miss-on-load になり得る。
WIMG と独立に動作するが、無効な cache には効果が無い。
M=1 の broadcast は HID1[ABE] に依存する。dirty data の書戻しだけを見て
あらゆる属性で同じ外部トランザクションと仮定しない。
同節は L1 を invalid、L2/L3 の該当 line を exclusive と記述している。
この Rev 3.1 の記述から全階層に line が残るとは断定しない。Rev 5 の訂正有無は [未]。

2.3.5.2 の eieio は先行の cache-inhibited/write-through access と後続の同種 access を
順序付ける。外部への同期命令 broadcast は HID1[SYNCBE] の設定も確認する。
isync は store queue の全 store 完了を待たない。同命令をデータ書戻しの sync の代わりにしない。
[未] DMA の所有権移譲・別名マッピング・SMP の実行 CPU 全体への同期手順。

icbi は data MMU で変換し load の保護判定を受ける。I-cache が disabled/locked でも
一致 entry を無効化するが、L2/L3 は無効化しない。外部 broadcast は変換属性と
HID1[ABE] に従う (MPC7450UM 3.4.4.8)。
同節は PEM 5.1.5.2 の列に 2 個目の sync が無い点を明示している。
資料間の差として記録し、745x には BSD と MPC7450UM が一致する 2 個の sync の列を採る。
