# sys/kern/ver_format.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: VER_INFO_DESIGN (D1 で移した)
ソース: `sys/kern/ver_format.c`

| 行 | 出す値／取り方 | 取れないとき／意味の限界 |
|---|---|---|
| PIC | `8259A x2; vectors=20h/28h (configured)`。sys/arch/x86/kernel/idt.h の設定定数 | 起動済みの設定説明。実チップを検出済みと主張しない |

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
- 共通 formatter は頭行・API・Build・Commit と行グループの順を持つ。PC-98 の devices・image・debug は sys/platform/pc98/ver_backend.c が作り、services 行末の ver_end までを担う。後端が NULL なら共通の行だけを返す。見出しは `PC-9821 OS32 v3.0-dev (Ring3 Native)`（版は SYS_VERSION）。
- kdebug は `off` / `on (pair %08x)` / `rejected %u %s` を先頭行にし、on は `kdebug pages: body=%u owner=%u`、rejected は `kdebug file pair: %08x` を続ける。取得失敗は `kdebug: unknown`。

- formatter は機器・識別・services の kind に INDENT を付ける。CUI はその bit に従って先頭2空白、各行末に改行を追加。見出し・kdebug・sqlite stack は字下げなし。構造体内に空白接頭辞・改行は含めない。

## PowerMac の表示

PowerMac の exec 像は共通の収集・整形と行追加の関数を使う。見出しは `PowerMac OS32 v<SYS_VERSION>`。装置行は CPU（起動時に保存した PVR）、IRQ（MPIC の source 数）、DEC（設定周波数と `(configured)`）、SER（ESCC の設定 baud）、GFX（OF-fb の幅・高さ・bpp と状態）の順。DEC は常に出す。CPU は保存済み、IRQ は数が非ゼロ、SER は状態取得成功、GFX は幅・高さ・bpp がすべて非ゼロのときに出す。
共通の API・Build に続き、起動情報が取れたら Commit、さらに CRC が有効なら Root image CRC とバイト数を出す。見出しは HEAD、装置行は DEVICE + INDENT、API 以降は IDENTITY + INDENT で、全体は 4〜10 行になる。
