# タスク: i386-elf クロスコンパイラ soft-float 再構築

> **ステータス: 見送り (2026-04-27)**
>
> 調査の結果、以下の理由により soft-float リビルドは実施しないことを決定:
>
> 1. `-msoft-float` は **コンパイル時の固定決定** であり、ハードウェア FPU が
>    存在してもソフトウェア演算が使われる（ランタイム切り替え不可）
> 2. ソフトウェア浮動小数点は FPU ハードウェア比で **10〜50倍遅い**
> 3. PC-9801 の i386 以降は **全機種 FPU 内蔵** であり、FPU 初期化の方が合理的
>
> **採用方式**: カーネル起動時に FPU 存在チェック (`CR0.ET`) → `fninit` で初期化。
> 現行の FPU ハードウェア使用を維持する。
>
> 本ドキュメントは将来 soft-float が必要になった場合の参照資料として保持する。

## 1. 背景と動機

### 問題

OS32 カーネルに統合した SQLite 3.53.0 が `double` 型の浮動小数点演算を使用しており、
現在のビルドでは **121 個の x87 FPU 命令** がバイナリに含まれている。

これにより以下の問題が発生:

1. **FPU 初期化が必須** — カーネル起動時に `CR0.EM=0` + `fninit` が必要
2. **ISR でのFPU状態保存** — 将来的に割り込みハンドラで `FXSAVE`/`FXRSTOR` が必要になる可能性
3. **障害の原因になった** — FPU 未初期化 (#NM) → #PF 連鎖が SQLite 導入時に発生

### 根本原因

現在のクロスコンパイラ (`i386-elf-gcc 13.2.0`) は **ハード FPU 前提** で構築されている:

```
../gcc-13.2.0/configure --target=i386-elf --prefix=~/opt/cross \
    --disable-nls --enable-languages=c,c++ --without-headers
```

`--with-float=soft` オプションがないため:
- `libgcc.a` 内の浮動小数点ヘルパー関数 (`__floatdidf` 等) が x87 命令を使用
- `-msoft-float` でコンパイルしても、ソフトウェア浮動小数点の基本演算関数
  (`__adddf3`, `__muldf3` 等 12個) が libgcc に存在せずリンクエラー

### 目標

`-msoft-float` でコンパイルしたコードが **FPU 命令ゼロ** で動作するよう、
クロスコンパイラを再構築する。

---

## 2. 現行環境

### ツールチェーン

| コンポーネント | バージョン | インストール先 |
|---------------|-----------|---------------|
| GCC | 13.2.0 | `~/opt/cross/` |
| binutils | 2.41 | `~/opt/cross/` |
| newlib | 4.4.0.20231231 | `~/opt/cross/i386-elf/` |
| NASM | 2.16.01 | システム |

### ソースコード (全て保持済み)

| ソース | パス |
|--------|------|
| GCC 13.2.0 | `~/src/gcc-13.2.0/` |
| binutils 2.41 | `~/src/binutils-2.41/` |
| newlib 4.4.0 | `~/src/newlib-4.4.0.20231231/` |

### 現行の libgcc 依存シンボル (sqlite3.o)

```
=== 整数演算 (libgcc提供, FPU不使用) ===
__divdi3, __moddi3, __udivdi3, __umoddi3
__divmoddi4, __udivmoddi4

=== 浮動小数点 (libgcc提供, ただしハードFPU使用) ===
(現行ビルドではFPUハードウェアを直接使用するため未参照)
```

### soft-float ビルドで追加される未解決シンボル (12個)

```
=== 四則演算 ===
__adddf3, __subdf3, __muldf3, __divdf3

=== 比較 ===
__eqdf2, __nedf2, __ltdf2, __ledf2, __gtdf2, __gedf2

=== 型変換 ===
__floatsidf, __fixdfsi
```

---

## 3. 再構築計画

### 3-1. 再構築対象

| コンポーネント | 再構築要否 | 理由 |
|---------------|-----------|------|
| **binutils** | ❌ 不要 | アセンブラ・リンカは FPU 設定と無関係 |
| **GCC + libgcc** | ✅ 必須 | `--with-float=soft` で再構築 |
| **newlib** | ✅ 必須 | GCC 再構築後に再コンパイルが必要 (libgcc に依存) |

### 3-2. 構築手順

#### Step 0: 現行環境のバックアップ

```bash
# 現行の cross ディレクトリをバックアップ
cp -a ~/opt/cross ~/opt/cross.bak.$(date +%Y%m%d)
```

#### Step 1: GCC 再構築 (soft-float)

```bash
cd ~/src
mkdir -p build-gcc-softfp
cd build-gcc-softfp

../gcc-13.2.0/configure \
    --target=i386-elf \
    --prefix=~/opt/cross \
    --disable-nls \
    --enable-languages=c \
    --without-headers \
    --with-float=soft

make all-gcc all-target-libgcc -j$(nproc)
make install-gcc install-target-libgcc
```

**変更点:**
- `--with-float=soft` 追加 → libgcc がソフトウェア浮動小数点関数を含む
- `--enable-languages=c` (C++ は OS32 では不使用のため削除して高速化)

> **注意**: `--without-headers` は維持。OS32 はフリースタンディング環境であり、
> ホストヘッダは不要。

#### Step 2: newlib 再構築

```bash
cd ~/src
mkdir -p build-newlib-softfp
cd build-newlib-softfp

../newlib-4.4.0.20231231/configure \
    --target=i386-elf \
    --prefix=~/opt/cross \
    --disable-multilib \
    --disable-newlib-supplied-syscalls \
    --enable-newlib-nano-malloc \
    --enable-newlib-nano-formatted-io

make -j$(nproc)
make install
```

> newlib 自体はコンパイラランタイム関数を提供しないが、
> GCC 再構築後のヘッダ・ABI 整合性のために再ビルドが望ましい。

#### Step 3: 検証

```bash
# 1. soft-float libgcc にソフトウェア浮動小数点関数が含まれるか確認
i386-elf-nm $(i386-elf-gcc -print-libgcc-file-name) | grep '__adddf3'
# 期待: "00000000 T __adddf3" (T = テキストセクションに定義あり)

# 2. libgcc 内に FPU 命令がないか確認
i386-elf-objdump -d $(i386-elf-gcc -print-libgcc-file-name) | \
    grep -cE '\b(fild|fstp|fld|fadd|fsub|fmul|fdiv|fcom)\b'
# 期待: 0

# 3. SQLite を -msoft-float でビルドしてリンク確認
cd /mnt/c/WATCOM/src/os32
# CFLAGS_SQLITE に -msoft-float を追加してビルド
make clean
make all
# 期待: エラーなし

# 4. sqlite3.o に FPU 命令がないか確認
i386-elf-objdump -d lib/sqlite3/sqlite3.o | \
    grep -cE '\b(fild|fstp|fld|fadd|fsub|fmul|fdiv|fcom)\b'
# 期待: 0

# 5. sqlite.bin のサイズ確認
ls -la sqlite.bin
# 期待: 373KB 前後 (+2KB 以内の増加)
```

### 3-3. OS32 Makefile の変更

GCC 再構築後、`Makefile` の `CFLAGS_SQLITE` に `-msoft-float` を追加:

```diff
-CFLAGS_SQLITE = -std=gnu89 -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -mno-red-zone -Os -fcommon -ffunction-sections -fdata-sections -Wno-long-long -w -DNDEBUG
+CFLAGS_SQLITE = -std=gnu89 -m32 -march=i386 -msoft-float -ffreestanding -fno-pie -fno-stack-protector -nostdlib -mno-red-zone -Os -fcommon -ffunction-sections -fdata-sections -Wno-long-long -w -DNDEBUG
```

### 3-4. カーネル FPU 初期化の変更

`-msoft-float` により SQLite は FPU を使わなくなるが、
将来の拡張に備えて FPU 初期化コードは **条件付きで残す**:

```c
/* kernel/kernel.c — FPU 初期化 (存在チェック付き) */
static void init_fpu(void)
{
    unsigned long cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (cr0 & (1 << 4)) {
        /* CR0.ET=1: FPU が存在する (i386以降は常に1) */
        cr0 &= ~(1 << 2);  /* EM ビットクリア */
        cr0 |= (1 << 1);   /* MP ビットセット */
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
        __asm__ volatile("fninit");
    }
}
```

SQLite が FPU を使わなくなるため、`init_fpu()` は安全策として残すだけとなり、
FPU なしの環境でもカーネルが正常動作する。

---

## 4. リスク評価

| リスク | 影響 | 対策 |
|--------|------|------|
| 既存プログラムの動作への影響 | 低 — カーネル `CFLAGS_BASE` は変更しない | カーネル本体は引き続きハード FPU 使用可能 |
| newlib の ABI 不整合 | 低 — newlib は libgcc の浮動小数点関数を直接呼ばない | `make clean && make all` で全再ビルド |
| libgcc サイズ増加 | 極小 — soft-float 関数は数KB | 検証ステップでサイズ確認 |
| ビルド失敗 | 低 — GCC 13.2.0 は soft-float を公式サポート | バックアップからロールバック可能 |

---

## 5. ロールバック手順

構築に失敗した場合:

```bash
# バックアップから復元
rm -rf ~/opt/cross
mv ~/opt/cross.bak.YYYYMMDD ~/opt/cross
hash -r  # シェルのコマンドキャッシュクリア

# OS32 を再ビルドして動作確認
cd /mnt/c/WATCOM/src/os32
make clean
make all
```

---

## 6. 作業見積もり

| ステップ | 見積もり時間 |
|---------|------------|
| Step 0: バックアップ | 1分 |
| Step 1: GCC ビルド | 30-60分 (WSL, j$(nproc)) |
| Step 2: newlib ビルド | 10-20分 |
| Step 3: 検証 | 10分 |
| Makefile 変更 + OS32 再ビルド | 5分 |
| **合計** | **約1-2時間** |

---

## 7. 期待される成果

| 項目 | 現行 | 再構築後 |
|------|------|---------|
| sqlite3.o 内 FPU 命令 | 121 個 | **0 個** |
| FPU 初期化の必要性 | 必須 | 不要 (安全策として残す) |
| .text サイズ増加 | — | **+1.7KB (+0.47%)** |
| libgcc 依存シンボル数 | 6 | 18 (整数除算6 + 浮動小数点12) |
| カーネル拡張域の収まり | ✅ | ✅ (余裕は約 5KB に減少) |

---

## 8. 依存関係図 (再構築後)

```
sqlite3.o (-msoft-float)
  │
  ├── カーネル提供 (17個)
  │   ├── malloc / free / realloc    ← MEMSYS5 経由
  │   ├── memcpy / memmove / memset / memcmp
  │   ├── strchr / strcmp / strcspn / strlen / strncmp / strspn
  │   ├── fabs
  │   └── sqlite3DbIsNamed / sqlite3_os_init / sqlite3_os_end
  │
  └── libgcc 提供 (18個, FPU命令ゼロ)
      ├── 整数除算 (既存)
      │   └── __divdi3 / __moddi3 / __udivdi3 / __umoddi3
      │       __divmoddi4 / __udivmoddi4
      │
      └── ソフトウェア浮動小数点 (再構築で追加)
          ├── 四則演算: __adddf3 / __subdf3 / __muldf3 / __divdf3
          ├── 比較:     __eqdf2 / __nedf2 / __ltdf2 / __ledf2
          │             __gtdf2 / __gedf2
          └── 型変換:   __floatsidf / __floatdidf
                        __fixdfsi / __fixdfdi
                        __fixunsdfdi / __extendsfdf2
```

---

*Cross-Compiler Rebuild Plan — 2026-04-27*
