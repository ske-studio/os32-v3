# ARM_GAUGE — ARM コンパイル計測の基準値

発行: コーダー (2026-09-15、移植性準備の**順序 1**、ユーザー承認済み)。
基点 `b32c34b`。

将来の ARM 移植に向けた準備の順序は次の 4 段で、本票はその 1 段目。

1. **ARM コンパイル計測 + ABI フラグ** ← 本票
2. `hlt` / `cli` / `sti` の直書きを `io.h` 経由に寄せる
3. `arch/` ディレクトリの導入と `ARCH` 選択
4. ext2 等の LE アクセサ化と `kstring` の C 版 (並行)

**順序 1 は移植そのものを一切行わない。**2〜4 の効果を同じ物差しで見るための
計測器を先に作り、現時点の数字を基準値として残すことだけが目的。

---

## 1. 計測器

    make check-arm-compile                          # 人が読む表
    python3 tools/check_arm_compile.py --json       # 機械可読
    ARM_CC=<path> python3 tools/check_arm_compile.py

実体は [`tools/check_arm_compile.py`](../../../tools/check_arm_compile.py)。
`arm-none-eabi-gcc` が無い環境では SKIP と出して**終了コード 0**。

### `make check` には入れていない

これは合否の門ではなく計測器。今 ARM で通らないのは当たり前 (x86 前提でよい、と
決めて書いてある) で、落ちても OS32 のビルドは壊れていない。`check` の列に入れると
「直さないと緑にならない」圧力がかかり、まだ設計の決まっていない `arch/` の分離を
急がせてしまう。独立したターゲットとして呼ぶ。

### 測り方の要点

| | |
|---|---|
| 対象集合 | `build/kernel.mk` の `C_KERNEL` (カーネルに実際にリンクされる C ソースの正典) |
| フラグ | `build/config.mk` の `CFLAGS_COMMON` を**読んで**使い、x86 専用の `-m32` `-march=i386` `-mno-red-zone` だけを外す。`-O2 -D__KERNEL_BUILD__` を足してカーネル本番ビルドに揃える |
| インクルード | `build/config.mk` の `INC_*` を読む。`build/kernel.mk` の個別ルールも写してある (`fs/fatfs/` は `INC_FATFS`、`drivers/` のうち `mouse` `loop_dev` `ne2000` `lgy98` の 4 本は `INC_KERNEL`) |
| コンパイル | `-c -o /dev/null` (コード生成まで) |

フラグとインクルードを `config.mk` から**読む**のは、ここに書き写すと二重管理に
なって片方だけ直ったときに黙ってずれるため。作業 B で足した
`-fsigned-char -fno-short-enums` も、この仕組みで自動的に計測側へ流れている。

### `-fsyntax-only` ではなく `-c` を使う理由

`-fsyntax-only` はインライン asm の**中身を見ない**。試したところ
`kernel/gdt.c` (`__asm__` に `eax` を書いている) すら素通りし、
「通った本数」が実態より大きく出る。コード生成まで走らせる `-c` を使う。

### アセンブル段のエラーを元のソース行へ戻している

`inb` のように**制約**が ARM に無いものはコンパイル段で落ちるので、エラー行が
`include/io.h:14` と出て分類できる。ところが `pushfl` / `sti` / `mov %cr3` のように
制約 (`"=r"`) だけは ARM でも通るものはコンパイル段を素通りし、gcc の中間 `.s` を
指した `/tmp/ccXXXXXX.s:96: Error: bad instruction 'pushfl'` になる。これでは
(1) どのヘッダ由来か分からず「io.h を分離したら何本通るか」が測れない、
(2) 一時ファイル名が毎回変わって記録が差分で追えない。

そこで `-g -S` で `.s` を出し直してアセンブルし、`.loc` / `.file` の表で元の
ソース行に戻している。この処理のおかげで **7 本が「その他」から `io.h` 由来へ
正しく移った** (`kernel/con_sink.c` `kernel/kbd_inject.c` `kernel/kernel.c`
`kernel/pgalloc.c` `kernel/snd_engine.c` `kernel/v86.c` `net/link.c`)。
`-g` はコード生成を変えないので、測っている対象は同じ。

### 対象から外しているもの (理由つき)

| 除外 | 理由 |
|---|---|
| `lib/sqlite3/` | vendored SQLite アマルガメーション。移植性は上流が見ていて ANSI C で書かれており、こちらで直す対象ではない。25 万行あるので計測時間だけが延びる |
| `lib/zlib/` `lib/microtar/` `lib/lz4.c` | vendored だが**カーネルにはリンクされない** (ユーザーランド用。`build/libs.mk` / `build/programs.mk` が `PROGRAM_FLAGS` で別に組む)。カーネルの移植度の数字に混ぜる意味がない |

対象集合が `C_KERNEL` から導かれる以上この一覧は説明でしかないが、
`kernel/ drivers/ fs/ exec/ kapi/ lib/ net/ gfx/` を glob して**未知の** `*.c` が
出てきたときは DRIFT として警告する (カーネルに足したなら `C_KERNEL` へ、
そうでないなら `KNOWN_NON_KERNEL` へ理由つきで)。

---

## 2. 基準値 (2026-09-15、基点 `b32c34b`)

> この節は**基準値**なので置き換えない。順序 2 以降の再計測は §9。

コンパイラ `arm-none-eabi-gcc` 14.2.1。

### 通った本数

    54 / 92

### 失敗の分類

| 分類 | 本数 |
|---|---|
| (a) インライン asm (x86 命令・レジスタ) | 5 |
| (b) `io.h` 経由のポート I/O | 33 |
| (c) x86 固有ヘッダ・型 | 0 |
| (d) その他 | 0 |

分類は**最初のエラー 1 行**の正規表現で機械的に行う。上から
(b) → (a) → (c) → (d) の順に当てて最初の一致を採る。`io.h` を先に見るのは、
`io.h` のエラーも asm エラーではあるので、後に回すと全部 (a) に吸われて
「`io.h` を分離すれば何本通るか」が見えなくなるため。

(c) と (d) が 0 なのは意味のある結果で、**x86 依存は inline asm に閉じている**。
ヘッダの取り合いや型の食い違いで落ちているものは 1 本も無い。

### ディレクトリ別

| | 通過 / 全体 |
|---|---|
| `kapi/` | 4 / 4 |
| `lib/` | 8 / 8 |
| `fs/` | 14 / 15 |
| `exec/` | 4 / 5 |
| `kernel/` | 18 / 32 |
| `drivers/` | 5 / 20 |
| `gfx/` | 1 / 7 |
| `net/` | 0 / 1 |

`kapi/` と `lib/` が丸ごと通るのは移植の見通しとして良い材料。逆に `drivers/`
`gfx/` はほぼ全滅で、ここが `arch/` (順序 3) とデバイス層の切り分けの本番になる。

---

## 3. 通らなかったファイルの一覧

### (a) インライン asm — 5 本

| ファイル | 最初のエラー |
|---|---|
| `exec/exec.c` | `exec/exec.c:1728:13: error: unknown register name 'eax' in 'asm'` |
| `gfx/gfx_vram.c` | `gfx/gfx_internal.h:52:5: error: impossible constraint in 'asm'` |
| `kernel/gdt.c` | `kernel/gdt.c:40:5: error: unknown register name 'eax' in 'asm'` |
| `kernel/paging.c` | `kernel/paging.c:545: Error: ARM register expected -- 'mov %cr3,r3'` |
| `kernel/tss.c` | `kernel/tss.c:53:5: error: impossible constraint in 'asm'` |

この 5 本は `io.h` を経由せず**その場に x86 asm を書いている**もの。`gdt` / `tss` /
`cr3` は x86 の仕組みそのものなので、順序 3 で `arch/x86/` へ移す対象。
`gfx/gfx_internal.h:52` と `exec/exec.c:1728` は移設先の判断が要る。

### (b) `io.h` 経由 — 33 本

エラーの内訳 (`include/io.h` の行):

| 行 | 中身 | 本数 |
|---|---|---|
| 14 | `inp` — `inb %w1, %b0` | 9 |
| 19 | `outp` — `outb %b0, %w1` | 17 |
| 43 | `_enable` — `sti` | 1 |
| 56 | `irq_save` — `pushfl; popl; cli` | 6 |

ファイル:

`drivers/atapi.c` `drivers/fdc.c` `drivers/fm.c` `drivers/ide.c` `drivers/kbd.c`
`drivers/kcg.c` `drivers/lgy98.c` `drivers/mouse.c` `drivers/mouse_bus.c`
`drivers/mouse_seamless.c` `drivers/ne2000.c` `drivers/np2sysp.c` `drivers/rtc.c`
`drivers/serial.c` `drivers/wab_glue_xe10.c` `fs/hostdrvfs.c` `gfx/backend_pc98.c`
`gfx/backend_pegc.c` `gfx/gfx_core.c` `gfx/gfx_scroll.c` `gfx/palette.c`
`kernel/con_sink.c` `kernel/console.c` `kernel/idt.c` `kernel/isr_handlers.c`
`kernel/kbd_inject.c` `kernel/kernel.c` `kernel/pgalloc.c` `kernel/snd_engine.c`
`kernel/sys.c` `kernel/v86.c` `kernel/v86_io.c` `net/link.c`

---

## 4. 読み方の注意

### 「最初のエラー 1 行」しか見ていない

分類は 1 本につきエラー 1 件。**`io.h` を分離したら 33 本がそのまま通るとは
限らない**。最初の原因が消えた後ろに 2 つ目の原因が隠れている可能性がある
(とくに `kernel/` `gfx/` の濃いところ)。順序 2 / 3 を終えたら**同じ計測器を
もう一度回して**、実際にいくつ増えたかで測ること。それがこの票の存在理由。

ユーザーの事前の見積もりは「`io.h` を分離すれば 29 本通る」だった。実測では
`io.h` が最初の原因になっているのは **33 本**で、見積もりより 4 本多い。ただし
上記のとおり 33 本全部が通るようになるとは限らないので、**29 も 33 も予測であり、
基準値は「今 54 本通る」の方**。

### 分類 (b) に `sti` / `pushfl` が入っている

順序 2 は「`hlt` / `cli` / `sti` の直書きを `io.h` 経由に寄せる」だが、計測で
見えた 7 本 (`sti` 1 + `pushfl` 6) は**すでに `io.h` 経由**で、`io.h` の中の
`_enable()` / `irq_save()` が x86 命令だから落ちている。つまり順序 2 の作業は
「呼び出し側を直す」より「`io.h` の中を arch 別にする」ほうが本体になる。
呼び出し側に残っている直書きは別途 grep で洗うこと (本票では調べていない)。

### `net/link.c` が 0/1

`net/` はファイルが 1 本しかないので割合に意味は無い。`io.h` の `irq_save` 由来。

---

## 5. 計測器そのものの検証

インクルードパスやフラグの写し間違いで落ちていたら、計測は移植性ではなく
自分の設定ミスを測っていることになる。同じ計測器を `i386-elf-gcc` で回した:

    ARM_CC=i386-elf-gcc python3 tools/check_arm_compile.py
    → 通った本数 : 92 / 92

**92/92。**よって対象集合・インクルードパス・フラグは本番ビルドを再現できており、
ARM で落ちた 38 本は本当に ARM 固有の理由で落ちている。

---

## 6. 作業 B — ABI の前提を固定するフラグ

`build/config.mk` の `CFLAGS_COMMON` に次の 2 つを足した。

    -fsigned-char -fno-short-enums

| フラグ | 何から守るか |
|---|---|
| `-fsigned-char` | ARM EABI は `char` が **unsigned** が既定。**今のコードに符号依存は見つかっていない** (`lib/utf8.c` は `& 0x80`、`fs/iso9660.c` は `(unsigned char)` に明示キャスト) が、`kstrcmp` はまだ `lib/kstring_asm.asm` の x86 アセンブリで、これを C 版に起こす (順序 4) と `*a - *b` の符号で 0x80 以上のバイト = 日本語ファイル名の**並び順が変わる**。移植の最中に既定が裏返らないよう先に固定する |
| `-fno-short-enums` | ARM EABI は `-fshort-enums` が既定 (enum を収まる最小サイズに縮める)。`sdk/include/os32/os32_kapi_shared.h` の `exec_status_t` は `0 … -5` なので **1 バイトに縮む**。カーネルと外部プログラムで共有する ABI ヘッダなので、幅が食い違うと構造体のレイアウトごとずれる ([ABI1]) |

`CFLAGS_COMMON` はカーネル (`KERNEL_CFLAGS`) と
ユーザーランド (`USER_CFLAGS` → `PROGRAM_FLAGS`) の**両方**の親なので、
ABI の両側に同じ前提がかかる。

### 生成物が変わらないことの確認

どちらも i386 GCC では既定と同じなので、付けても `.o` は変わらないはず。
`make` は使わずに `i386-elf-gcc` を直接呼んで確かめた
(新旧フラグでコンパイルし `.o` の md5 を突き合わせ):

| 対象 | 結果 |
|---|---|
| カーネル C 92 本 (`C_KERNEL` 全部、`KERNEL_CFLAGS` 相当) | **92 / 92 が md5 一致**、相違 0、エラー 0 |
| ユーザーランド 5 本 (`USER_CFLAGS` 相当、`cal` `diff` `du` `find` `cfg`) | **5 / 5 が md5 一致** |

`arm-none-eabi-gcc` も 2 つのフラグを受け付け、計測値は 54/92 のまま変わらない
(ABI の前提を固定するだけで、通る本数には影響しない)。

---

## 7. CONSTRAINTS への追加 — 足さないと判断した

このフラグを規則として [`docs/CONSTRAINTS.md`](../../CONSTRAINTS.md) に採番する
案を検討したが、**今は足さない**。

理由:

1. **`check_constraints.py` は ID の有無しか照合しない。**規則本文の中身も、
   フラグが実際に `config.mk` に在るかも検査しない。歯の無い ID を増やすと
   「規則はあるが守られているか誰も見ていない」状態になる。
   [ABI1]〜[ABI3] には `check_kapi_version.py` / `check_manifests.py` という
   実物の検査が付いていて、そこが違う。
2. **守るべき場所に、より強い形で書いてある。**フラグのすぐ上に、何から守るのか
   (ARM EABI の既定、`exec_status_t`)、既定と同じである根拠 (92/92 の md5 一致)
   まで `config.mk` にコメントとして置いた。「このフラグを消すな」を CLAUDE.md に
   1 行足すより、フラグ本体に付いた説明のほうが消しにくい。
3. **まだ live な危険ではない。**対応アーキが i386 だけのうちは、この 2 つは
   既定の追認でしかない。規則が要るのは `arch/` が入って複数アーキを同時に
   ビルドし始める順序 3 以降。
4. CLAUDE.md は入口で、規則は現在 16 件と意図的に短い。計測の票から恒久的な
   規則を増やすのはスコープ外。

### 順序 3 で採るなら (PM 判断用の下ごしらえ)

採番は **[ABI4]** が空いている (`ID_RE` は `(?:C|HW|ABI|V|D)\d+`、並べ替えが
`int(s[-1])` なので 1 桁である必要がある。`ABI4` はそのまま載る)。文言案:

> **[ABI4]** 共有 ABI ヘッダ (`sdk/include/os32/`) の型は `char` の符号・`enum` の
> 幅の既定に依存しない。`CFLAGS_COMMON` の `-fsigned-char` / `-fno-short-enums`
> を外さない。

歯を付けるなら `tools/check_constraints.py` ではなく専用の検査
(`build/config.mk` の `CFLAGS_COMMON` に 2 つのフラグが在ることを確かめる数行) を
`make check` に足すのが筋。ID だけ足しても検査にはならない。

---

## 8. 次にこの票を使うとき

順序 2 / 3 / 4 を終えたら `make check-arm-compile` をもう一度回し、
§2 の表を**置き換えずに追記**して基準値との差を残すこと (追記先は §9)。
増えた本数がその作業の効果そのもの。

---

## 9. 経過 — 順序ごとの再計測

§2 の基準値は置き換えない。ここに行を足していく。

| 時点 | 基点 | 通過 | (a) asm | (b) io.h 経由 | (c) ヘッダ・型 | (d) その他 |
|---|---|---|---|---|---|---|
| 順序 1 (基準値) | `b32c34b` | **54 / 92** | 5 | 33 | 0 | 0 |
| 順序 3 の直前 | `51f3f50` | **54 / 92** | 5 | 33 | 0 | 0 |
| 順序 3 の直後 | 本作業 | **54 / 92** | 5 | 33 | 0 | 0 |
| 順序 4-a の直前 | `ff4160c` | **55 / 93** | 5 | 33 | 0 | 0 |
| 順序 4-a の直後 | 本作業 | **55 / 93** | 5 | 33 | 0 | 0 |
| 順序 5 の直前 | `d191fd5` | **55 / 93** | 5 | 33 | 0 | 0 |
| 順序 5 の直後 | 本作業 | **55 / 93** | **1** | **37** | 0 | 0 |

母数が 92 → 93、通過が 54 → 55 に増えたのは順序 **4-b** (`lib/kstring_c.c` の
新設) の分で、順序 4-a は 1 本も動かしていない。ディレクトリ別は 4-a の前後で
同じ (`kapi/` 4/4、`lib/` 9/9、`fs/` 14/15、`exec/` 4/5、`kernel/` 18/32、
`drivers/` 5/20、`gfx/` 1/7、`net/` 0/1。順序 1〜3 との差は `lib/` の 8/8 → 9/9
だけ)。

### 順序 2 では数字が動かなかった

順序 2 は `hlt` / `cli` / `sti` の**直書きを `io.h` 経由に寄せる**作業で、
呼び出し側から x86 の asm を消したが、消えた先は `io.h` の中の
`_halt()` / `_idle()` / `_stop()` で、そこはやはり x86 の asm。落ちる本数は
変わらないのが正しい (§4 の「(b) に `sti` / `pushfl` が入っている」で
予告したとおり)。

### 順序 3 でも数字は動かない — それが確かめたかったこと

順序 3 は `io.h` を**契約** (`include/io.h`) と**実装**
(`arch/x86/arch_io.h` / `platform/pc98/platform_io.h`) に割っただけで、
`ARCH=x86` のまま計測している以上、ARM コンパイラに渡る命令列は 1 つも
変わらない。**54/92 のまま、分類の内訳も 5 / 33 / 0 / 0 のまま**であることが、
「骨格を入れただけで中身は動かしていない」ことの裏づけになる。

変わったのは**エラーが指すファイル名**だけ:

| 順序 2 まで | 順序 3 以後 | 本数 |
|---|---|---|
| `include/io.h:14` (`inp`) | `platform/pc98/platform_io.h:20` | 9 |
| `include/io.h:19` (`outp`) | `platform/pc98/platform_io.h:25` | 17 |
| `include/io.h:58` (`_enable`) | `arch/x86/arch_io.h:17` | 1 |
| `include/io.h:74` (`irq_save`) | `arch/x86/arch_io.h:27` | 6 |

(a) の 5 本 (`exec/exec.c` `gfx/gfx_vram.c` `kernel/gdt.c` `kernel/paging.c`
`kernel/tss.c`) はエラー行もそのまま。順序 1 の §3 が「`gdt` / `tss` / `cr3` は
順序 3 で `arch/x86/` へ移す対象」と書いていたが、**この 5 本の移設は行って
いない** — 順序 3 の承認範囲は `io.h` の分割と受け皿の導入までで、`gdt.c` /
`tss.c` / `paging.c` の中身を `arch/` へ移すのは別の判断が要る (移すのは
関数単位か、ファイルごとか、`gfx_internal.h:52` と `exec.c:1735` の
移設先をどこにするか)。次の票の材料。

### 順序 4-a でも数字は動かない — この計測器が測っていないものだから

順序 4-a は `fs/ext2_*.c` / `drivers/kcg.c` / `lib/utf8.c` の
`*(u32 *)&buf[off]` を `include/endian_le.h` の LE アクセサに置き換えた作業。
**この計測器で数字が動かないのは正しく、しかも「効果が無い」という意味では
ない。** 理由を分けて書いておく:

* この計測器は **`arm-none-eabi-gcc` が通るか**しか見ない。
  `*(u32 *)&buf[off]` は ARM でも**普通にコンパイルできる** — 壊れるのは
  実行時 (`SCTLR.A=1` なら非アラインで abort、BE なら値が入れ替わる) なので、
  コンパイル計測には最初から現れない。
* 実際、置き換えた 8 本のうち 7 本 (`fs/ext2_*.c` 5 本、`fs/iso9660.c`、
  `lib/utf8.c`) は**順序 1 の時点から ARM を通っていた** (`fs/` 14/15、
  `lib/` 8/8)。`drivers/kcg.c` だけは今も落ちるが、原因は
  `platform/pc98/platform_io.h:25` のポート I/O で、エンディアンとは関係が無い。
  基点 `ff4160c` の 55/93 は前後で 1 本も動かない (分類も 5 / 33 / 0 / 0 のまま)。

つまり順序 4-a の効果は**この票の物差しでは測れない**。代わりの物差しとして、
`-Wcast-align=strict` の警告数を置く (`tools/check_le_access.py`、`make check`):

| 対象 | 順序 4-a の前 | 順序 4-a の後 |
|---|---|---|
| `fs/ext2_dir.c` | 29 | 0 |
| `fs/ext2_fmt.c` | 54 | 0 |
| `fs/ext2_inode.c` | 46 | 0 |
| `fs/ext2_super.c` | 28 | 0 |
| `drivers/kcg.c` | 3 | 0 |
| `lib/utf8.c` | 3 | 0 |
| `fs/ext2_file.c` / `ext2_vfs.c` / `iso9660.c` | 0 | 0 |
| **計** | **163** | **0** |

`-Wcast-align` は既定では「非アラインを許す CPU」で黙るので、x86 のホストで
見張るには **`=strict` が要る**。等価性 (x86 で振る舞いが変わらないこと) は
`.o` の突き合わせではなく **B8 の試験と実物の `e2fsck`** で見た — アクセサが
インライン展開されると命令列は変わりうるので `.o` 比較は成立しない。
`tools/tests/test_b8_open.py` が 1461 件 0 失敗、`e2fsck` 654 像で
自前の媒体検査との食い違い 0。

### 計測器に 2 つ手を入れた

1. **`?=` を読めるようにした。** `parse_make_vars` の正規表現が `=` と `:=`
   しか拾わず、`ARCH ?= x86` が読めなかった。その結果
   `-Iarch/$(ARCH)` が `-Iarch/` に潰れ、最初の計測は 50/92 (io 40 / asm 2) に
   なった — これは移植度が下がったのではなく、**33 本が「`arch_io.h` が
   無い」で落ちていた**だけ。`?=` は make なら「未定義なら」の条件が付くが、
   この計測器は `config.mk` を単独で読むので必ず既定値が採られる。
2. **`-I` の実在検査を足した。** 上の潰れ方は「INC が空」の検査には掛からない
   (`-Iarch/` は空文字ではない)。展開後の `-I` が実在するディレクトリを
   指しているかを確かめ、指していなければエラーで止める。同じ壊れ方を
   黙って通さないため。

分類 (b) の判定は基底名が `io.h` かどうかで行っていたので、`arch_io.h` /
`platform_io.h` も (b) に数えるようにした。しないと 33 本がまるごと (a) へ
移り、順序 1 の基準値と比べられなくなる (ラベルも
「(b) io.h 経由 (arch_io / platform_io)」に直した)。

### 生成物が変わらないことの確認 (順序 3)

`git archive 51f3f50` で無垢な木を展開し、`C_KERNEL` の C **92 本全部**を
新旧の木で同一フラグ (新しい `-Iarch/x86 -Iplatform/pc98` を含む。無垢な木に
そのディレクトリは無いが gcc は存在しない `-I` を黙って無視する) で
コンパイルして `.o` を突き合わせた:

| 対象 | 結果 |
|---|---|
| `C_KERNEL` 92 本のうち 91 本 | **91 / 91 が md5 一致**、相違 0、エラー 0 |
| `kapi/kapi_sys.c` | `__DATE__` / `__TIME__` を埋めるので md5 比較の対象外。同じ分にコンパイルして**バイト一致**を確認 |

### 順序 4-b (kstring の C 版) 後 — 2026-09-15

| 時点 | 通った本数 | (a) asm | (b) io.h 経由 | (c) | (d) |
|---|---|---|---|---|---|
| 順序 4-b 後 | **55 / 93** | 5 | 33 | 0 | 0 |

`lib/kstring_c.c` が `C_KERNEL` に加わって母数が 92 → 93 になり、そのまま ARM で通る側に入った。
x86 の既定は `lib/kstring_asm.asm` のまま (`build/kernel.mk` の `ARCH` 分岐)。
計測器は `kernel.mk` の `$(KSTRING_C_SRC)` をファイル名と誤解して (d) に数え DRIFT を出していたので、
変数を `kernel.mk` 自身の代入から展開するようにした (`parse_kernel_vars`)。

### 順序 5 — (a) が 5 → 1 になった (通った本数は動かない)

順序 5 は `gdt` / `tss` / `cr3` / `CR0.PG` / CPL=3 降下の asm を、その場から
`arch/x86/` のヘッダへ移した作業。`ARCH=x86` のまま測っている以上、ARM
コンパイラに渡る命令列は 1 つも変わらないので **55 / 93 は動かない**。
動いたのは分類で、落ちるファイルが指すエラー行が変わった:

| 順序 4 まで | 順序 5 以後 | 本数 |
|---|---|---|
| `exec/exec.c:1735` (リング降下) | `arch/x86/arch_cpu.h:69` | 1 |
| `kernel/gdt.c:40` (lgdt + セグメント) | `arch/x86/x86_desc.h:25` | 1 |
| `kernel/paging.c:545` (`mov %cr3`) | `arch/x86/arch_cpu.h:29` | 1 |
| `kernel/tss.c:53` (`ltr`) | `arch/x86/x86_desc.h:45` | 1 |
| `gfx/gfx_vram.c` (`rep movsw` ほか) | 変わらず `gfx/gfx_internal.h:52` | 1 |

分類 (a) の意味は「差し替え先がまだ決まっていない、その場の x86 asm」なので、
`arch/` や `platform/` の下に入ったものは (b) 側に数えるよう
`tools/check_arm_compile.py` の `classify()` を直した (基底名の一覧では
`arch_cpu.h` / `x86_desc.h` を取りこぼす。順序 3 で `arch_io.h` /
`platform_io.h` を (b) に入れたのと同じ理由)。ラベルも
「(b) arch/ platform/ の実装経由」に改めた。**計測器を直さずに測ると
順序 5 の直後も 5 / 33 のまま**で、上の表のとおりエラー行だけが変わる。

ついでに 2 つ:

* `CR0_PE` / `CR0_PG` を `include/pc98.h` から `arch/x86/arch_cpu.h` へ移した
  (CPU のビットであって機種の持ち物ではない)。使っていたのは `kernel/paging.c`
  だけで、そこは `arch_mmu_enable()` の内側になった。
* その結果 `kernel/paging.c` が `pc98.h` を引く理由が無くなったので外した
  (`.o` は md5 一致のまま)。ページングのコードが機種ヘッダに依存しなくなる。

残る (a) の 1 本は `gfx/gfx_vram.c` (`gfx/gfx_internal.h` の `rep movsw` /
`rep stosl`)。これはデバイス層 (VRAM への転送) の話で、順序 5 の範囲外
(HANDOVER_2026-09-16 §5「やらないこと」)。

### 生成物が変わらないことの確認 (順序 5)

基点 `d191fd5` で `make kernel` した `.o` を退避し、移設後に同じ `make kernel`
(全 `.o` を消してからの再ビルド) をして突き合わせた:

| 対象 | 結果 |
|---|---|
| カーネルの `.o` 103 本のうち 102 本 | **102 / 102 が md5 一致**、相違 0 |
| `kapi/kapi_sys.o` | `__DATE__` / `__TIME__` を埋めるので対象外。`objdump -s` で見た差は `.rodata` のビルド時刻文字列 1 か所だけ (命令は無差) |

一致させるために、特権境界の 2 つ (`arch_enter_user` /
`arch_call_on_stack`) は `static inline` 関数ではなく**マクロ**にした。
関数にすると:

* 退避先スロットがポインタ引数になり、`saved_esp_stack[id]` の
  `mov %esp,0x34(,%edi,4)` という添字つきの番地計算がレジスタに乗って、
  `exec_launch` 全体のレジスタ割り付けが動く (逆アセンブル 334 行の差)。
* asm が呼び手の基本ブロックから出て、分岐の並びと整列 nop が変わる
  (同 39 行の差)。

どちらも動作は同じだが、「移設で命令列は変わっていない」を `.o` の md5 で
示せなくなる。理由は `include/cpu.h` の §2 に書いた。
`kernel_tss.esp0` を引数のポインタで渡す案も、`struct tss_entry` が packed の
ため `-Waddress-of-packed-member` が出るのでやめ、記録先は arch 実装の内側
(x86 では `kernel_tss.esp0`) に閉じた。

### kstring の asm 版 / C 版の速度 (2026-09-17、基点 `63754b4`)

順序 4-b で新設した `lib/kstring_c.c` を **x86 でも使うか**を実測した
(票 [`TASK_KSTRING_BENCH.md`](../../archive/portability/TASK_KSTRING_BENCH.md)、計測プログラム
`userland/tests/kstr_bench.c`、集計 `tools/kstr_bench_report.py`)。
NP21/W の既定設定 (約 16MHz) で 3 回、13 本 × 7 長 × 2 ずれ × 2 版 = 364 升。

| 関数 | 比の中央値 (C / asm) | 比の最大 | 差の出どころ |
|---|---|---|---|
| `memcmp` | 9.02 | **13.58** | asm は `repe cmpsd` |
| `kmemcpy` / `memcpy` | 6.73 | **11.27** | asm は `rep movsd` |
| `kmemset` / `memset` | 3.64 | **7.80** | asm は `rep stosd` |
| `kstrncpy` | 2.30 | 2.41 | asm は `repne scasb` で長さを測り `rep movsb` (バイト幅) |
| `kstrlen` / `strlen` | 2.12 | 2.30 | asm は `repne scasb` (バイト幅) |
| `kstrcpy` | 1.53 | 1.59 | asm は `repne scasb` + `rep movsb` (バイト幅) |
| `kstrcmp` / `strcmp` | 1.08 | 1.08 | **両版ともバイトループ** |
| `kstrncmp` / `strncmp` | 1.07 | 1.07 | **両版ともバイトループ** |

全体の中央値 1.55 / 最悪 13.58 / 最良 0.96。3 回のばらつきは
**364 升すべてで 10% 以内** (最悪 3.3%)。`MISMATCH` は 0 件で、
13 本すべてが両版で同じ結果を返すことをゲストでも確かめた。

**倍率は asm 側が使う命令の幅でほぼ決まる。** 3 段にきれいに分かれる:

| asm 側の実装 | 該当 | 比 |
|---|---|---|
| **語幅**の `rep` (`movsd` / `stosd` / `cmpsd`) | `kmemcpy` `memcpy` `kmemset` `memset` `memcmp` の 5 本 | 6.7〜13.6 |
| **バイト幅**の `rep` (`scasb` / `movsb`) | `kstrlen` `strlen` `kstrcpy` `kstrncpy` の 4 本 | 1.5〜2.3 |
| `rep` を使わない素のバイトループ | `kstrcmp` `strcmp` `kstrncmp` `strncmp` の 4 本 | 1.07〜1.08 |

この並びは**エミュレータが倍率を膨らませている**ことも示す。`rep` の 1 命令は
ホスト側の転送 1 回に潰れるのに対し、C 版は同じ仕事を数千命令として解釈される。
`rep` を使わない 4 本が 1.08 に留まるのがその対照。**実機での倍率はこれより小さい**ので、
13 倍を実機の値として引用しない ([V4])。

**差は C の限界ではない。** `lib/kstring_c.c` は境界をまたぐ読み込みを避けるため
1 バイトずつ 4 回読んで語に組み直している。送り元と送り先の境界が揃う場合に
`u32` のまま動かす分岐を足せば、素の C のまま差の大半は縮む。

**決定 (ユーザー、2026-09-17): x86 はアセンブリ版を維持。** `build/kernel.mk` の
`ifeq ($(ARCH),x86)` はそのまま。`lib/kstring_c.c` は他アーキ向けに現状の設計で進める。

高速側 (P100 相当) の条件は**測っていない** — `np21x64w.ini` の `clk_mult` 変更が要り、
[D2] の承認対象かつスキル `os32-emu-config` の扱うキーに無いため見送った。

---

## 10. 対象とする語長 — 32 ビットのみ (ユーザー決定 2026-09-17)

**移植の対象は 32 ビットアーキテクチャに限る。16 ビットと 64 ビットは範囲外。**
64 ビット機に載せたい場合は、利用者が 64 ビットのプログラムを書く。

この前提が効く場所:

| 前提 | 効く場所 |
|---|---|
| `sizeof(void *) == 4`、`u32` とポインタが同じ幅 | `lib/kstring_c.c` の語単位処理、`include/types.h`、KAPI の構造体レイアウト ([ABI1]) |
| 語の単位は 4 バイト | `KSTR_WORD` とゼロ検出の定数 `0x01010101` / `0x80808080` |
| `int` は 32 ビット | KAPI の戻り値、`OS32_ERR_*` |

つまり **64 ビット対応のための抽象化 (ポインタ幅の型・可変語長) は足さない**。
足さないことで、移植の作業が「32 ビットの別 CPU に載せる」に閉じる。
16 ビットを外すのは、保護モード・ページングを前提にした設計 (`docs/02_memory.md` §2-1)
がそもそも 16 ビット機では成立しないため。
