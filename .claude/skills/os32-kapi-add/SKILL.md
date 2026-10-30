---
name: os32-kapi-add
description: OS32 の KernelAPI (KAPI) を追加・変更するときに使う。sdk/kapi.json への追記、再生成、版数の同期、clean ビルド、外部リポジトリの再ビルドまでを取りこぼさないための手順。KAPI に触らない通常のコード変更には使わない。
---

# KernelAPI の追加・変更

手順の正典は [KAPI_SPEC §3-1](../../../docs/KAPI_SPEC.md#3-1-kernelapi-関数の追加手順)、規則は `docs/CONSTRAINTS.md` の [ABI]。
ここには手順本文を複製しない — **迷ったら正典を読む**。このスキルは
「どこで落ちやすいか」と「終わったと言える条件」だけを持つ。

## 前提の確認

- `sdk/kapi.json` が唯一の真実 ([ABI])。`os32_kapi_generated.h` / `kapi_generated.c` /
  `exec_kapi_init.inc` / `kapi_generated.rs` / `os32_kapi_slots.h` は**生成物**で、手編集しない。
- 追加は **`api` 配列の末尾だけ** ([ABI])。並べ替え・削除は配備済みバイナリの ABI を壊す。
  既存スロットの意味を変えるのも同じ結果になる。
- 未実装の追加を先に押さえたいときは `docs/KAPI_SPEC.md` §3-2 の版番号予約を見る
  (GUI / network の各計画が予約済みの番号を持っている)。

## 落ちやすいところ

- **版数は 2 か所**: `sdk/kapi.json` の `"version"` と
  `sdk/include/os32/os32_kapi_shared.h` の `KAPI_VERSION`。一致していないと `make check` が落ちる。
- **再生成のあとに `git diff --stat` を見る。** 意図した追加以外の行が動いていたら、
  生成物を手で触ったか、kapi.json の既存部分を動かしている。
- **差分ビルドは無言で壊れる** ([ABI])。構造体が変わるので `make clean` → `make all`。
  古い `syscalls.o` が残ると `malloc` が全部 ENOMEM になる類の症状が出る (POLICY_DEBUG §4-1)。
- **外部 apps/game は H-7 で持越し中**。[再開条件](../../../docs/tasks/v3/T2H_EXTERNAL_GATE.md#再開前に揃える-8-項目) を満たし、
  再開を決定した後だけ `make external` で再構築する。
- 関数ポインタでない値は `api` ではなく `data_fields`。ジェネレータは `= 0;` を出すだけなので、
  実際の代入を `exec/exec.c` の `exec_init()` に書く。書き忘れると呼び出し側で NULL を踏む。
- 新 API に依存するプログラムは `build/app.conf` の要求バージョンを上げる。上げ忘れると
  古いカーネルで動かして原因不明の失敗になる。

## 完了条件と報告

生成・版更新・ビルド・関数表更新は [KAPI_SPEC §3-1](../../../docs/KAPI_SPEC.md#3-1-kernelapi-関数の追加手順)、
役別の検査条件は [ROLES §0「検査」](../../../docs/tasks/agents/ROLES.md#0-現行の体制) を参照する。
配備と呼出し確認は [os32-build-verify](../os32-build-verify/SKILL.md#起動したバイナリを確認する) に従う ([V1])。
報告には追加したエントリとスロット、版数、担当した検査の結果、ゲストでの呼出し結果を含める。
未実施は未実施と書く ([V4])。
