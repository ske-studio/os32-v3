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
[未] キャッシュ属性ごとの厳密な順序保証・完了条件は MPC7450 系マニュアルで補う。
