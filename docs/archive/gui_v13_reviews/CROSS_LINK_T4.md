# T4 — guestターゲットのコンパイル・リンク検査

> 発行: PM (2026-09-09) / 状態: **完了記録 (2026-09-10)**

限定リンク検査成功・ゲスト実行なし。
親: [PLAN.md](../../tasks/gui/v13/PLAN.md)、[REVIEW_T4.md](REVIEW_T4.md)。

## 対象と結果

- 現行 `userland/libos32term` を `sdk/rust/i686-os32-none.json` でreleaseビルド: 成功。
- 別のno_std staticlib診断プローブから `Grid::clip` を実行時引数で参照し、i386-elf-gccで最終ELFへリンク: 成功。
- `i386-elf-nm -u` の出力は空（未解決シンボルなし）。
- ELFはIntel i386 / ELF32 / static。`__divdi3` は定義済みのweak symbolとして含まれる。
- 同じguestターゲットで `size_of::<Cell>()` と `align_of::<Cell>()` を配列シンボル長に埋め込み、nm -Sで確認: size 8B / align 4B。
  ホスト試験と一致するが、このコンパイラ・ターゲットの観測値でありRust ABI保証ではない。

## コマンド

リポジトリルートで実行:

```sh
cargo build --manifest-path userland/libos32term/Cargo.toml --release \
  --target ~/os32/sdk/rust/i686-os32-none.json \
  -Z json-target-spec -Z build-std=core,compiler_builtins \
  -Z build-std-features=compiler-builtins-mem --offline
```

一時診断クレート: `/tmp/os32-t4-link-probe/`。
manifestは独立workspace・staticlib・panic=abort、依存は現行libos32termのみ。
probeは可変のセル幅・高さと矩形座標からGrid::clipを呼び、定数畳込みによる除算消失を避けた。
panic handlerは停止ループ。これはリンク専用であり、実行していない。

```sh
cargo build --manifest-path /tmp/os32-t4-link-probe/Cargo.toml --release \
  --target ~/os32/sdk/rust/i686-os32-none.json \
  -Z json-target-spec -Z build-std=core,compiler_builtins \
  -Z build-std-features=compiler-builtins-mem --offline
i386-elf-gcc -nostdlib -static \
  -Wl,-e,term_probe,--gc-sections,-u,CELL_SIZE_PROBE,-u,CELL_ALIGN_PROBE,-Map,/tmp/os32-t4-link-probe/probe.map \
  -o /tmp/os32-t4-link-probe/probe.elf \
  /tmp/os32-t4-link-probe/target/i686-os32-none/release/libos32_t4_link_probe.a -lgcc
i386-elf-nm -u /tmp/os32-t4-link-probe/probe.elf
i386-elf-nm -S /tmp/os32-t4-link-probe/probe.elf
```

## 解決した点と限界

M4の「i64除算ヘルパを解決できない可能性」は、この限定リンクでは発生しなかった。
実際のgshellまたは表示試験アプリのCRT・リンカスクリプト・ライブラリ構成での最終リンクは別途必要。
診断ELFはOS32Xではなく、配備・起動可能なアプリ成果物として扱わない。
モデルの全経路のリンク網羅、ゲスト動作、描画、ヒープ空き、CUI捕捉は検証していない。
プローブは/tmpの一時成果物であり、恒久ゲートに登録していない。
