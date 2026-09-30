---
name: os32-kapi-add
description: OS32 の KernelAPI (KAPI) を追加・変更するときに使う。sdk/kapi.json への追記、再生成、版数の同期、clean ビルド、外部リポジトリの再ビルドまでを取りこぼさないための手順。KAPI に触らない通常のコード変更には使わない。
---

# KernelAPI の追加・変更

手順の正典は `docs/KAPI_SPEC.md` §3-1、規則は `docs/CONSTRAINTS.md` の [ABI1]〜[ABI3]。
ここには手順本文を複製しない — **迷ったら正典を読む**。このスキルは
「どこで落ちやすいか」と「終わったと言える条件」だけを持つ。

## 前提の確認

- `sdk/kapi.json` が唯一の真実 ([ABI1])。`os32_kapi_generated.h` / `kapi_generated.c` /
  `exec_kapi_init.inc` / `kapi_generated.rs` / `os32_kapi_slots.h` は**生成物**で、手編集しない。
- 追加は **`api` 配列の末尾だけ** ([ABI2])。並べ替え・削除は配備済みバイナリの ABI を壊す。
  既存スロットの意味を変えるのも同じ結果になる。
- 未実装の追加を先に押さえたいときは `docs/KAPI_SPEC.md` §3-2 の版番号予約を見る
  (GUI / network の各計画が予約済みの番号を持っている)。

## 落ちやすいところ

- **版数は 2 か所**: `sdk/kapi.json` の `"version"` と
  `sdk/include/os32/os32_kapi_shared.h` の `KAPI_VERSION`。一致していないと `make check` が落ちる。
- **再生成のあとに `git diff --stat` を見る。** 意図した追加以外の行が動いていたら、
  生成物を手で触ったか、kapi.json の既存部分を動かしている。
- **差分ビルドは無言で壊れる** ([ABI3])。構造体が変わるので `make clean` → `make all`。
  古い `syscalls.o` が残ると `malloc` が全部 ENOMEM になる類の症状が出る (POLICY_DEBUG §4-1)。
- **`make external` を忘れない。** `apps/` `game/` のアプリは静的リンクなので、
  古い `.bin` を残さない。再ビルドだけでは submodule のポインタは変わらない。
  ポインタ更新条件は `docs/08_build.md` §8-4 を参照し、コミット・push は明示的な指示がある場合のみ行う。
- 関数ポインタでない値は `api` ではなく `data_fields`。ジェネレータは `= 0;` を出すだけなので、
  実際の代入を `exec/exec.c` の `exec_init()` に書く。書き忘れると呼び出し側で NULL を踏む。
- 新 API に依存するプログラムは `build/app.conf` の要求バージョンを上げる。上げ忘れると
  古いカーネルで動かして原因不明の失敗になる。

## 終わったと言える条件

1. `python3 sdk/gen_kapi.py && python3 sdk/kapi_rust_gen.py` の後、`git diff --stat` が意図した差分だけ。
2. `make clean` → `make all` が通り、`make external` も通る。
3. `make check` が通る (`check-kapi-version` が kapi.json / 生成物 / README / docs の版数を照合する)。
4. `docs/KAPI_SPEC.md` §4 の関数表に新しいエントリがある (`check-kapi-version` が照合する)。
5. **実機で新しい API を呼ぶまでは合格ではない** ([V1])。NHD へ配備して呼ぶ。
   配備と反映確認の進め方はスキル `os32-build-verify`。

報告には、追加したエントリ名とスロット、上げた版数、`make check` の結果、
実機で何を呼んで何が返ったかを含める。未実施の手順は未実施と書く ([V4])。
