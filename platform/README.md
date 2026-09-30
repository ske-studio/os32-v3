# `platform/` — 機種依存の実装

CPU (`arch/`) とは別の軸。ポート I/O のように「その CPU なら必ずこう」では
なく、**機器のつなぎ方**で決まるものを置く。

    platform/pc98/platform_io.h     PC-9801/9821 のポート I/O と I/O ウェイト

契約は [`include/io.h`](../include/io.h)、選ぶのは `build/config.mk` の
`PLATFORM ?= pc98` と `-Iplatform/$(PLATFORM)`。もう 1 本の契約
[`include/cpu.h`](../include/cpu.h) (アドレス変換・特権境界) は CPU の
持ち物なので、`platform/` 側に実装は無い。

2 本の軸の関係、足し方、番人については [`arch/README.md`](../arch/README.md) を読むこと
(片方だけ読めば済むように、手順はそちらに 1 か所でまとめてある)。
