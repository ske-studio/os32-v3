# kernel/module/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3・HWDEV_DESIGN・GF6200_S7_SHELL・RECOVER_T4B_INJECT (D1 で移した)
ソース: `kernel/module/`

**形式検査・表生成の共通部を使った専用のモジュールローダ**
T4の共通検証核を `/boot` のPOST_ROOT部品へ使う。

### module_image

`R_386_32` の位置一覧
ローダはベースを加算。**表に無い種別が残っていたら拒否**

| 項 | 契約 |
|---|---|
| 生成時とロード時の検証を分ける | `mkmod.py` の合格は、ディスクから読んだ画像が同じ内容である保証にはならない。**ロード時にも**: ヘッダ最小長、形式・ABI 世代の識別 (D35)、text / data / bss / 各表の範囲が `mem_size` の内側、整数オーバーフロー、**再配置の書込先 4 バイト全体が範囲内** (位置が `mem_size − 2` なら隣を壊す)、**重複・重なる再配置の拒否** (同じ位置が二度あればベースを二度足す)、import slot・export・init の参照が範囲内 — を検査する。現行 shlib の「加算した後に比較する」サイズ検査 (`shlib.c`) はそのまま雛形にしない |

| 実物・根拠 (file:line) | 確認したこと |
|---|---|
| `tools/mkmod.py`、`build()`、`sys/kern/module/module_image.c` の `module_image_header()` | ELF32、R_386_32/PC32、未解決参照拒否、生成 import 型契約、CRC・PAIR・範囲検査を再利用できるが、部品間 import の自動解決はない (§4 で接続) |

### kdebug_call

KDEBUGはビルド時の分岐ではなく起動設定なので同じ成果物を使い
実物sys/kern/module/kdebug_call.cの未公開opsによるNOSYS

kernel→既存dbg_call。

## 索引

- [sys/kern/module/kdebug_imports.def](kdebug_imports.md)
- [sys/kern/module/module_runtime.c](module_runtime.md)
- [module_image](module_image.md)
