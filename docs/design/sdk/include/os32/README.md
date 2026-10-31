# sdk/include/os32/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3・VER_INFO_DESIGN (D1 で移した)
ソース: `sdk/include/os32/`

### os32_generations

**世代の識別を 4 つ別に持つ** — OS32X ヘッダの形式版・**KAPI ABI 世代**・**メモリ配置世代**・**shlib プロトコル**。互換性を切る世代は一致を要求し、その世代の内側でだけ最低機能版 (min KAPI) を使う

### os32_ver_info

C89 互換、ポインタや size_t は含めない。

```c
#define OS32_VER_TEXT 64
#define OS32_VER_COLS 52 /* 440px - 左右PAD20px、8px字幅以内 */
/* kind低8bit: 0=head, 1=device, 2=identity, 3=debug; bit8=INDENT */
typedef struct { u32 kind; char text[OS32_VER_TEXT]; } OS32_VerLine;
```

公開行 kind・INDENT・配列上限は os32_ver_info.h

**`OS32_VER_LINES` は 20** (構造体は約 1364B の固定、表示は count の行だけで窓は広げない)。

`sdk/include/os32/os32_ver_info.h` の大きさの typedef の検査 (68・1364) は **`#if defined(__i386__)` の中に置く**。
大きさと offset は 32 ビットで組む専用の host 試験 (V3 の ver_layout_host・test_ver_layout) と i386 の製品のビルドで確かめる。

## 索引

- [sdk/include/os32/os32_gui_shared](os32_gui_shared.md)

- [sdk/include/os32/os32_kapi_shared](os32_kapi_shared.md)

- [sdk/include/os32/os32_surface](os32_surface.md)
