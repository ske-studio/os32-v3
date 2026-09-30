# K3: 共有ライブラリ帯域 (0x400000〜0x4FFFFF) とロードアドレス移動

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: K / 前提: **他レーン全部の完了後** (W2, C2 まで)。C3 と同時
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §9.3 (案 A、ユーザ決定 2026-09-04)
> 排他: `exec/**` `kernel/paging.c` `include/memmap.h` `sdk/link/app.ld`、`build/app.conf`

## ゴール

libos32gui (G 描画 + ウィジェット + スタブ) を**固定アドレス常駐の共有ライブラリ**として
0x400000〜0x4FFFFF に置き、アプリ本体を 0x500000 からにする。アプリの .bin から
ライブラリ分が消え、GUI アプリが 1MB を超えても載る (1MB 上限は 2026-09-04 に撤廃済み)。

## 作業

1. **memmap.h**: `MEM_SHLIB_BASE 0x400000` / `MEM_SHLIB_SIZE 0x100000` /
   `MEM_SHLIB_DATA_PAGES` (アプリごとの .data/.bss ページ数、ライブラリのヘッダから読む) /
   `MEM_EXEC_LOAD_ADDR 0x500000`。`RING3_HEAP_TOP` 等の上端は変えない。
   `MEM_EXEC_SBRK_MIN` / `HEAP_MIN` の計算 (`exec.c` の動的レイアウト) はそのまま。
2. **ライブラリ形式**: OS32X をそのまま使う (`OS32X_FLAG_SHLIB` を追加)。先頭 4KB に
   ジャンプ表: `magic`, `version: u32`, `nfunc`, `entry[]`。C3 がこの形式で
   `libos32gui.shlib` を出す。K3 は `sdk/os32x.h` に構造体を切る (K 排他)。
3. **ローダ**: 起動時 (gshell が Level 0 で上がる前、または `os32gui` 経路) に
   `/sys/lib/libos32gui.shlib` を 0x400000 に読み、`.text/.rodata` を **read-only + USER で
   全 PD 共有**。`.data/.bss` は「同じ仮想アドレスにアプリごとの物理ページ」: `exec_child_claim`
   の PD 生成時にライブラリの data ページを複製して張る (v2 C2 の PD ごと帯域と同じ機構)。
4. **`sdk/link/app.ld`**: `. = 0x500000`。`crt0.asm` に変更なし。`build/app.conf` の
   必要 KAPI 版を上げる。
5. **一斉再ビルド**: `make clean && make all && make external` (apps / game の submodule も)。
   `tools/prune_stale.py` が旧 0x400000 バイナリを NHD から消すことを確認。
   submodule ポインタを更新してコミット。
6. **CPL=3 アプリからライブラリの data が見えること**、**他アプリの data が見えないこと**を
   `ring3_guard` 系のテストで確認 (0x400000 帯の .text に書くと kill)。

## 鉄則

- ライブラリは**位置依存** (再配置無し)。ロードアドレスは 1 つ。
- バージョン照合はスタブ側 (C3) が `entry[0]` の版と `GUI_PROTO_VERSION` を比べる。K3 は
  読み込んだ版をカーネルログに出すだけ。
- **[D1] [V1]**: NHD を入れ替えるまで検証と呼ばない。HostDrv の `/host` から古い
  0x400000 バイナリを起動すると `exec` が `OS32_ERR_INVAL` (ロードアドレス不一致) で
  弾くこと (K3 がヘッダの `load_addr` を検証に足す)。

## 完了条件 (ゲート G4)

- `heap_test` が `load=0x500000`、`guard_a` が上に 1MB ずれた値を表示。
- `gui_demo.bin` が C3 前より小さく、`gshell` 上で G2 と同じ操作が通る。
- os32-apps / os32-game の全 deploy.yaml エントリが再ビルド後に起動する (検証層が
  1 本ずつ `os32-cycle run`)。
- 旧バイナリの起動が `INVAL` で弾かれ、rshell が沈黙しない (メモリ os32-verify-traps)。
