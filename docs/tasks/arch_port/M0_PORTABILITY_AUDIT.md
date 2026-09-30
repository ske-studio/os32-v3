# M0 — 非整列アクセス / キャッシュ前提の監査

*実施日: 2026-09-08 / 種別: **監査記録** / 対象: OS32 本体 (kernel 側 + userland)*
*位置づけ: [00_INDEX.md](00_INDEX.md) §3「移植先を問わず先にやれること」の 1 と 2。実機不要*

**この監査ではコードを一切変更していない。** 見つかった事項の修正は別作業とする。

x86 では黙って通るが ARMv5 (Brain / i.MX28) をはじめ多くの ISA で壊れる書き方を、
機械的に洗い出して仕分けた記録。副産物として **x86 のままでも危ない箇所が 1 件**見つかっている (§3-3)。

---

## 1. 結論

| 観点 | 結果 |
|---|---|
| 非整列アクセス | 警告 202 件 → 仕分けの結果 **確定的な不具合 2 件**、**潜在 1 件**、残りは安全または宣言 1 行で確定できる |
| 構造体レイアウト | **i386 と ARM で完全一致**。`KernelAPI` 736B、`GuiEvent` 16B とも同値。8 バイト整列を要求する型を共有ヘッダが 1 つも使っていないため |
| キャッシュ前提 | OS32 にキャッシュ管理の概念が無く、**ロードしたコードへ飛ぶ経路**と**ページ表操作**が ARM では追加処理を要する |
| ARM でのコンパイル | 試した範囲では通る。`__cdecl` は **ARM で無視される**という警告が出る (制約 [C3] の実証) |

**移植の障害として見たとき、非整列アクセスは想定より軽い。** 危ないのは局所的な 3 か所で、
どれも直し方が明確である。一方 §4 のキャッシュ側は「今は存在しない概念を足す」話であり、こちらが本体。

---

## 2. 方法

### 2-1. 使ったもの

| ツール | 版 | 用途 |
|---|---|---|
| `gcc -m32` | Ubuntu 13.3.0 | `-Wcast-align=strict` による全走査 |
| `arm-linux-gnueabi-gcc -march=armv5te` | Ubuntu 13.3.0 | ARM での実コンパイル、構造体レイアウト実測、シンボル整列の実測 |

`-Wcast-align=strict` は **ターゲット非依存**で「アラインメント要件を上げるキャスト」を警告する。
x86 と ARM で同一の結果が出ることを小さな例で確認したうえで、ホスト側の `gcc -m32` で全走査した。

再実行できるように `tools/audit_cast_align.sh` を置いた (`make check` には組み込んでいない)。

```bash
tools/audit_cast_align.sh kernel   # カーネル側
tools/audit_cast_align.sh user     # userland
```

### 2-2. 網羅率 — ここを読まずに件数を信じないこと

`-fsyntax-only` が通らなかったファイルからは警告が出ない。**「警告 0 件」は「安全」ではない。**

| 範囲 | 解析成功 | 解析失敗 | 網羅率 |
|---|---:|---:|---:|
| kernel 側 (`kernel/ drivers/ gfx/ fs/ exec/ kapi/ lib/`) | 80 | 5 | **94%** |
| `userland/` (Rust 除く) | 36 | 97 | **27%** |

- kernel 側の失敗 5 件は `drivers/lgy98.c` `drivers/loop_dev.c` `drivers/mouse.c` `drivers/ne2000.c` `exec/exec.c`。
  ただし解析が途中で止まったファイルでも、そこまでの警告は有効で拾えている (実際 `exec/exec.c` からは 1 件出ている)。
- **userland の網羅率が低いのは newlib ヘッダ (i386-elf クロス) がこの環境に無いため**であり、
  userland の結果は「見えた範囲でこうだった」以上の意味を持たない。
  クロスコンパイラのある環境で再走査すること。
- `lib/sqlite3/` は上流コードのため対象外。

---

## 3. 結果 A — 非整列アクセス

### 3-1. 全体の仕分け

kernel 側 189 件 + userland 13 件 = 202 件。

| 分類 | 件数 | 判定 |
|---|---:|---|
| 定数オフセットが型サイズで割り切れない | **0** | — |
| 定数オフセットは整合。基底バッファの整列次第 | 86 | 実測では安全。ただし言語仕様上の保証がない (§3-4) |
| 変数オフセット・ポインタ演算・構造体キャスト | 103 | 個別判断 → §3-2, §3-3 |
| userland (`userland/lib/db/libos32db.c` のみ) | 13 | §3-2 の読み側。同一の不具合 |

### 3-2. 確定的な不具合 (2 件)

#### (1) `lib/utf8.c:107,110,113` — `utf8_pack32()` が任意位置から 4 バイト読む

```c
u32 utf8_pack32(const u8 *p)
{
    ...
    if ((b0 & 0xE0) == 0xC0) { return *(const u32 *)p & 0x0000FFFFu; }
    if ((b0 & 0xF0) == 0xE0) { return *(const u32 *)p & 0x00FFFFFFu; }
    return *(const u32 *)p;
}
```

`p` は UTF-8 文字列の**任意の位置**を指す。4 バイト境界にある保証はどこにもない。
ARMv5 では非整列 load が回転した値を返すため、**フォールトせず静かに誤った文字コードになる**。
日本語のホットパス (3 バイト文字) をそのまま踏む。

なお既存コメントが「呼び出し側でバッファ末尾の 4B 読み出し境界をチェックすること」と注意しているとおり、
**末尾での 4 バイト過剰読み出し**という別の危険も同居している。x86 でもページ境界に当たれば落ちうる。

- 対処: バイト単位の合成に置き換える (`p[0] | p[1]<<8 | p[2]<<16`)。
  x86 でも最適化で 1 命令に畳まれることが多く、性能上の損は小さい。過剰読み出しも同時に消える。

#### (2) `kapi/kapi_db.c` — DB 結果 SHM の可変長列の後ろで固定幅を非整列書き込み

`data_offset` の進め方が型で違う:

| 列型 | 進み方 | 整列 |
|---|---|---|
| INTEGER / FLOAT | `data_offset += 4` | 保つ |
| TEXT | `data_offset += len + 1` | **任意量** |
| BLOB | `data_offset += len` | **任意量** |

TEXT / BLOB の直後に INTEGER か FLOAT が来ると、

```c
*(i32 *)(DB_SHM_PTR + data_offset) = val;   /* kapi/kapi_db.c:126, 153 */
```

が非整列アドレスへの書き込みになる。
再現条件は単純で、`SELECT name, age FROM t` の `name` が 4 文字なら `data_offset` は 5 進み、
`age` が奇数境界に書かれる。

**読み側も同じ欠陥を持つ**: `userland/lib/db/libos32db.c` の 13 件はすべてこの `data_offset` 経由の読み出しである。
つまり **KAPI の DB 結果プロトコルそのものが非整列を許す設計**になっている。

- 対処: 固定幅型を書く前に `data_offset` を 4 の倍数へ切り上げ、切り上げ後の値を `cols[i].data_offset` に記録する。
  書き手と読み手の両方を同時に直す必要がある (ABI の意味論変更なので [ABI2] の扱いを要確認)。

### 3-3. 潜在的な不具合 (1 件) — x86 でも危ない

#### `fs/ext2_dir.c` — ディレクトリエントリの `rec_len` を検証せずに `pos` を進める

全 8 か所のループが同じ形をしている:

```c
while (pos < EXT2_BLOCK_SIZE) {
    u32 de_inode  = *(u32 *)&ext2_g_aux[pos];
    u16 de_reclen = *(u16 *)&ext2_g_aux[pos + 4];
    ...
    if (de_reclen == 0) break;
    pos += de_reclen;
}
```

検査は `de_reclen == 0` だけである。ext2 の仕様上 `rec_len` は 4 の倍数だが、
**壊れた (あるいは悪意のある) ファイルシステムではそうとは限らない**。

- ARM では `pos` が 4 の倍数でなくなった時点で `*(u32 *)&ext2_g_aux[pos]` が非整列になり、
  フォールトせず誤った inode 番号を読む。
- **x86 でも問題がある**: `pos + de_reclen` が `EXT2_BLOCK_SIZE` を超えないことを誰も確かめていない。
  `pos` がブロック末尾近くのとき `ext2_g_aux[pos + 8 + j]` (名前のコピー) がバッファ外に出る。
  これは移植とは無関係な既存の堅牢性の穴である。

- 対処: ループ内で `de_reclen` に対し「4 の倍数」「8 以上」「`pos + de_reclen <= EXT2_BLOCK_SIZE`」を検査し、
  外れたらそのブロックの走査を打ち切る。ARM 対応と既存バグ修正を兼ねる。

### 3-4. 安全と判定したもの

#### 定数オフセット群 (86 件) — 実測では整列している

`fs/ext2_fmt.c` `fs/ext2_super.c` `fs/ext2_inode.c` の `*(u32 *)&ext2_g_blk[N]` 形式。
`N` はすべて型サイズの倍数だった (割り切れないものは 0 件)。残るのは基底バッファの整列。

```
fs/ext2_priv.h:21   extern u8 ext2_g_blk[EXT2_BLOCK_SIZE];   /* アラインメント指定なし */
```

ARM で実際にコンパイルして確認した結果:

```
$ arm-linux-gnueabi-gcc -march=armv5te -O2 -c fs/ext2_super.c ...
$ readelf -SW ext2_super.o | grep .bss     →  Al = 4
$ readelf -sW ext2_super.o | grep ext2_g_
  ext2_g_blk  offset 0x000   ext2_g_dat  offset 0x400   ext2_g_aux  offset 0x800
```

`.bss` が 4 バイト境界、各バッファも 4 の倍数位置に置かれるため**現状は安全**。
ただしこれは **コンパイラがそう置いているだけで、言語仕様上の保証ではない**。

- 対処 (推奨): 宣言に `__attribute__((aligned(4)))` を付けて確定させる。
  同じリポジトリの `fs/hostdrvfs.c:43-49` が既にこの書き方をしているので、様式としても揃う。
  1 行ずつ 3 か所で、リスクなく 86 件の警告の根拠が消える。

同じ理由で `drivers/kcg.c` のローカル `hdr[16]`、`fs/fatfs_vfs.c:441` の `buf` も
「オフセットは整合、基底の整列は暗黙」に該当する。

#### `kernel/kmalloc.c` (9 件) — 明示的に整列済み

`BLK_ALIGN 8` で基底を切り上げ、サイズを 8 の倍数に丸め、`BLK_HDR_SIZE` も 8。
`BlkHdr` は常に 8 バイト境界に載る。ソース中のコメントも整列が前提だと明記している。**対処不要**。

#### `kernel/v86*.c` (8 件)

V86 サブシステムは ARM に等価物が無く移植先で消滅するため、**この監査の対象外**とした。

---

## 4. 結果 B — 構造体レイアウトは i386 と ARM で一致

i386 SysV ABI と ARM AAPCS の代表的な差は `long long` / `double` の整列 (4 バイト vs 8 バイト) である。
これが共有構造体に入っていると、同じヘッダから作った kernel 側と app 側でレイアウトがずれる。

**共有ヘッダ (`sdk/include/os32/*.h`, `include/*.h`) に `long long` / `double` / `u64` は 1 つも無い。**
実際に両ターゲットでコンパイルして値を取り出した結果も完全一致した:

| 項目 | i386 (`gcc -m32`) | ARM (`armv5te`) |
|---|---:|---:|
| `sizeof(KernelAPI)` | 736 (0x2e0) | 736 (0x2e0) |
| `sizeof(OS32Header)` | 44 (0x2c) | 44 (0x2c) |
| `sizeof(OS32ShlibHeader)` | 32 (0x20) | 32 (0x20) |
| `sizeof(DB_ResultHeader)` | 12 | 12 |
| `sizeof(DB_ColumnInfo)` | 12 | 12 |
| `offsetof(DB_ColumnInfo, data_offset)` | 8 | 8 |
| `__alignof__(KernelAPI)` | 4 | 4 |
| `sizeof(GuiEvent)` / `__alignof__` | 16 / 4 | 16 / 4 |

**GUI v1.1 で凍結した「型付き 16B イベント」の契約は ARM でもそのまま成立する** (`tasks/gui/API_CONTRACTS.md`)。

移植で作り直しになるのは**構造体の形ではなく呼び出し規約**である (§5)。

---

## 5. 結果 C — ARM でのコンパイル所見

`fs/ext2_super.c` を ARM で実際にコンパイルしたところ、**エラーなく通った**。出た警告は次の 1 種類:

```
sdk/include/os32/os32_kapi_shared.h:430: warning: 'cdecl' attribute directive ignored [-Wattributes]
```

`__cdecl` は x86 専用の属性で、**ARM では黙って無視される**。
これは制約 [C3] (「KernelAPI に公開する関数は `kapi/` に `__cdecl` ラッパを持つ」) が
移植先では意味を失うことの実証であり、[BRAIN_MX28_HARDWARE.md](BRAIN_MX28_HARDWARE.md) §7-4 の
「KAPI ABI は AAPCS + `svc` へ作り直し」という見立てを裏づける。

---

## 6. 結果 D — キャッシュ / TLB の前提

OS32 には**キャッシュ管理の概念が存在しない**。x86 のコヒーレントキャッシュと自動的な
命令キャッシュ無効化に暗黙に依存している。ARM926EJ-S は D キャッシュが VIVT でハードウェア
コヒーレンシを持たず、命令キャッシュとデータキャッシュも別 (Harvard) なので、以下が新規に必要になる。

| 箇所 | 現在 | ARM で必要になること |
|---|---|---|
| `exec/exec.c:1014` `entry = (ExecEntry)(load_addr + entry_off);` からの呼び出し | ロードして即ジャンプ | **D-cache clean + I-cache invalidate**。無いと古い命令を実行する |
| `kernel/shlib.c` の共有ライブラリ読み込み | 同上 | 同上 |
| `kernel/hotdeploy.c` のバイナリ差し替え | 同上 | 同上。ホットデプロイは特に踏みやすい |
| `kernel/paging.c:113-118` `cr3` 再ロードによる TLB 全消し | x86 は i386 に `invlpg` が無いため cr3 リロード | `TLBIALL` + `DSB` / `ISB`。ARMv5 では ASID が無いので PD 切替ごとに TLB と VIVT キャッシュの全消しが要る (コストが高い) |
| GFX の VRAM 直書き | `0xA8000` へ直接書き込み | LCDIF のフレームバッファは **DRAM 上**。描画後・DMA 前に D-cache clean。VRAM 相当は存在しない |
| DMA | `drivers/fdc.c` `lgy98.c` `ne2000.c` (移植先では全廃) | LCDIF と SSP/MMC で**新規に**必要。バッファのキャッシュ属性設計が要る |

**ARMv5 の VIVT キャッシュはページディレクトリ切替のたびに全消しが要る**点は、
OS32 の「外部プログラムごとに独自のページディレクトリ」([09_exec.md](../../09_exec.md)) と相性が悪い。
プロセス切替のコストが x86 より明確に高くなるため、移植する場合は早い段階で実測すべき項目である。

---

## 7. 推奨する対処 (優先順)

**本監査ではコードを変更していない。** 以下は所見であり、着手は別途判断する。

| 順 | 対象 | 内容 | 効く範囲 |
|---|---|---|---|
| 1 | `fs/ext2_dir.c` | `rec_len` の検証 (4 の倍数 / 下限 / ブロック内に収まる) | **x86 でも堅牢性が上がる**。移植とは独立に価値がある |
| 2 | `lib/utf8.c` | `utf8_pack32()` をバイト合成に置換 | ARM 必須。x86 でも過剰読み出しが消える |
| 3 | `kapi/kapi_db.c` + `userland/lib/db/libos32db.c` | `data_offset` を固定幅型の前で 4 の倍数へ切り上げ | ARM 必須。**書き手と読み手を同時に直す**必要あり |
| 4 | `fs/ext2_super.c` の 3 バッファ | `__attribute__((aligned(4)))` を明示 | 警告 86 件の根拠が消える。挙動は変わらない |
| 5 | userland | クロスコンパイラのある環境で再走査 | 現在の網羅率 27% を上げる |

1 と 4 は移植の可否に関係なく単独で入れられる。2 と 3 は ABI と挙動に触るので、
移植に着手すると決めてからでよい。

---

## 8. この監査が見ていないもの

- **`void *` からの構造体キャスト**。`-Wcast-align` は `void *` を最大整列とみなすため警告しない。
  `kapi/kapi_db.c:61` の `(DB_ResultHeader *)DB_SHM_PTR` のような形は、SHM の先頭が整列していれば安全だが、
  この監査では検出できていない。
- **userland の 73%** (§2-2)。
- `lib/sqlite3/` (上流コード)。
- `apps/` `game/` submodule。
- Rust コード (`userland/rust/`)。
- **実行時の検証**。ARM 実機で動かして確かめたものは 1 つも無い。本監査は静的解析と
  クロスコンパイルによる実測 (構造体レイアウト、シンボル整列) までである。
- ビットフィールドのレイアウトと、`packed` 属性の有無による差。共有ヘッダに該当がなかったため踏み込んでいない。

---

## 9. 再現方法

```bash
# 非整列アクセスの走査
tools/audit_cast_align.sh kernel
tools/audit_cast_align.sh user

# ARM でのコンパイルと構造体レイアウトの実測 (要 gcc-arm-linux-gnueabi)
sudo apt-get install -y gcc-arm-linux-gnueabi
arm-linux-gnueabi-gcc -march=armv5te -std=gnu89 -ffreestanding -fno-pie -O2 \
    -c fs/ext2_super.c -o /tmp/e2s.o \
    -I. -Iinclude -Isdk/include -Isdk/include/os32 -Ifs -Ifs/fatfs -Idrivers -Ikernel -Ilib
readelf -SW /tmp/e2s.o | grep '\.bss'
readelf -sW /tmp/e2s.o | grep ext2_g_
```
