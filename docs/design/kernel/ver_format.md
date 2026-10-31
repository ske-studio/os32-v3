# kernel/ver_format.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: VER_INFO_DESIGN (D1 で移した)
ソース: `kernel/ver_format.c`

| 行 | 出す値／取り方 | 取れないとき／意味の限界 |
|---|---|---|
| PIC | `8259A x2; vectors=20h/28h (configured)`。arch/x86/kernel/idt.h:101–102 の設定定数 | 起動済みの設定説明。実チップを検出済みと主張しない |

- count は実際の件数。装置 unknown は行を残す。boot_image_info 失敗時だけ Commit/CRC を従来どおり省く。
- formatter は上限付き追記 helper と u32 の10進/16進変換だけで作る。符号・NUL・残容量を先に扱い、カーネル文字列関数は kstrlen/kstrncpy 等を使う。
表示52桁以内
SYS_VERSION は20文字以内を静的検査。
- 実行時 NOSPC / count=0 の経路は作らない。上限付き helper の超過検出は host では失敗とし、実行時は当該行だけ `<label>: unknown` にする。Commit/CRC は最大長が既知の専用書式で必ず維持。
formatter に cold 属性を付ける。

- formatter は gfx_active に応じて `active` / `GUI target` を必ず付ける。同設定でも GUI 起動前後で語尾は変わり得る。

- formatter は `sqlite stack high-water=%u of %u` と `services: %s public=%u` を debug 行にする。詳細ページ数等は既存 services_report に残し、ver の要約には含めない。

## 4. CUI と GUI の共通化
- 正規の行順: 見出し → CPU → PIC → PIT → KBD → SER → SND → GFX → API → Build → Commit → Image CRC → kdebug（必要時は継続1行）→ sqlite stack → services。
- kernel/ver_format.c だけが英文と順序を持つ。見出しは `PC-9821 OS32 v3.0-dev (Ring3 Native)`（版は SYS_VERSION）。
- kdebug は `off` / `on (pair %08x)` / `rejected %u %s` を先頭行にし、on は `kdebug pages: body=%u owner=%u`、rejected は `kdebug file pair: %08x` を続ける。取得失敗は `kdebug: unknown`。

- formatter は機器・識別・services の kind に INDENT を付ける。CUI はその bit に従って先頭2空白、各行末に改行を追加。見出し・kdebug・sqlite stack は字下げなし。構造体内に空白接頭辞・改行は含めない。
