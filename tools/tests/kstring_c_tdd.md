# 移植準備 順序 4-b — `kstring` の C 版とアセンブリ版の答え合わせ (試験の記録)

- 票: 移植準備 順序 4-b (`lib/kstring_asm.asm` の C 版を用意する)。
  計測の正典は [`docs/tasks/portability/ARM_GAUGE.md`](../../docs/tasks/portability/ARM_GAUGE.md) §9 の
  「順序 4-b (kstring の C 版) 後」
- 実行: `python3 -B tools/tests/test_kstring_c.py [--mutate]`
  (`make check-kstring-c-host` が `--mutate` 付きで回す)
- 対象: `lib/kstring_asm.asm` (x86 の既定) / `lib/kstring_c.c` (C 版)
- ハーネス: `tools/tests/kstring_c_host.c`

## 0. 正直に書く ([V4])

**試験は実装のあとに書いた。** `lib/kstring_c.c` を先に書き、そのあとで
`tools/tests/kstring_c_host.c` と `test_kstring_c.py` を書いた。だから
「先に RED を見た」とは書けない。この記録自体はさらに後 (2026-09-15、
文書整理 段階 E) に起こしたもので、根拠がそれまで `build/sdk.mk` の
コメント 1 行しか無かったのを埋めるためのもの。

代わりの担保は **変異試験 (`--mutate`)** で、C 版をわざと壊した 5 版すべてが
落ちることを確かめてある (§3)。これは実際に走らせた記録。

## 1. これは何の門でもない

**x86 の既定ビルドはアセンブリのまま** (`build/kernel.mk` の `ARCH` 分岐)。
この試験が見るのは「C 版に差し替えても結果が 1 バイトも変わらない」ことだけで、
**切り替えの門ではなく答え合わせ**。ARM へ移すときに C 版を選ぶかどうかは別の判断。

## 2. 何を確かめる試験か

1. 実物の `lib/kstring_asm.asm` を `nasm -f elf32` で組み、`objcopy` で 13 本の
   シンボルを `a_*` に改名する。
2. 実物の `lib/kstring_c.c` をホスト ILP32 GNU89 (`-m32`) でコンパイルし、
   同じ 13 本を `c_*` に改名する。
3. **両方を 1 つの実行ファイルにリンクし**、同じ入力で**戻り値とバッファの
   全内容 (前後の番兵を含む)** を突き合わせる。食い違ったら C 版が悪い
   (アセンブリ版が「正」)。

13 本 = `kmemcpy` `memcpy` `kmemset` `memset` `kstrlen` `strlen` `kstrcmp`
`strcmp` `kstrncmp` `strncmp` `kstrcpy` `kstrncpy` `memcmp`。
`lib/kstring_asm.asm` の `global` 宣言と突き合わせるので、ここが増減したら
`check_symbol_set` が食い違いを検出する。

比較そのものに試験対象を使わないよう、`h_*` の素朴な実装を別に持つ。
libc は使わない (`-nostdlib`、Linux の `int 0x80` で write/exit するだけ)。

突き合わせる入力: 空 / 1 バイト / 4・8・16 の前後 / **0x80 以上のバイトを含む列
(日本語の UTF-8)** / `n` が長さより長い・短い・0 / **重なりコピー (前方・後方)** /
`kstrncpy` の埋め。`kernel/kselftest.c` の `test_str` の既存ケースも両版で通す。

### 重なりコピーまで一致させる理由

アセンブリ版は 4 バイト単位のバルク転送 (`rep movsd` 相当) をするので、
重なった領域の**壊れ方**が 1 バイトずつの版と違う。`lib/kstring.h` は重なりを
保証しないと書いているが、C 版に差し替えた瞬間に壊れ方が変わると、
**たまたま動いていた呼び出しが黙って壊れる**。だから壊れ方まで一致を要求する。

## 3. GREEN と変異 (実測、2026-09-15)

```
SYM asm (nasm)   13 本すべて定義
SYM c (host m32) 13 本すべて定義
== kstring: asm 版 と C 版 の答え合わせ ==
CHECKS 77956 FAILURES 0
EXIT kstring_c_host=0
TARGET i386-elf GNU89 -Werror COMPILE PASS
SYM i386-elf     13 本すべて定義
TARGET arm-none-eabi GNU89 -Werror COMPILE PASS
SYM arm-none-eabi 13 本すべて定義
MUTATE kstrcmp_signed         RED (期待どおり落ちた)
MUTATE memcmp_signed          RED (期待どおり落ちた)
MUTATE kstrncpy_pads          RED (期待どおり落ちた)
MUTATE kmemcpy_bytewise       RED (期待どおり落ちた)
MUTATE alias_separate_body    RED (期待どおり落ちた)
```

| 変異 | 壊した規則 | なぜ効くか |
|---|---|---|
| `kstrcmp_signed` | 差を**符号付き**で取る | アセンブリ版は `movzx` = 符号無し。0x80 以上のバイト (日本語ファイル名) の**並び順が逆転する** — この票の中心 |
| `memcmp_signed` | 同じ罠を `memcmp` で | 同上 |
| `kstrncpy_pads` | libc の `strncpy` のように残りを 0 で埋める | OS32 の `kstrncpy` は埋めない。埋めると番兵まで潰れる |
| `kmemcpy_bytewise` | バルク部を 1 バイトずつにする | `rep movsd` は 4 バイト読んでから 4 バイト書くので、**重なりコピーの壊れ方**が変わる |
| `alias_separate_body` | `memcpy` を `kmemcpy` の別名でなく別の実体にする | 別名であること自体が契約 (リンク時に 1 つの実体へ畳まれる) |

変異は `git` を使わずにソースを書き換え、`finally` で必ず戻す。
`--target` では `i386-elf-gcc` と **`arm-none-eabi-gcc`** の両方で C 版が
`-Werror` で通り、13 本すべてが定義されることも見る (無ければ SKIP)。

## 4. この試験が言わないこと

- **性能は測っていない。** C 版がアセンブリ版より遅いかどうかは対象外。
- **x86 の既定ビルドを切り替えてはいない。** `lib/kstring_c.c` が
  `C_KERNEL` に加わって ARM 計測の母数が 92 → 93 になったことは
  [`ARM_GAUGE.md`](../../docs/tasks/portability/ARM_GAUGE.md) §9 の側の記録。
- 起動時自己試験 (`kernel/kselftest.c` の `test_str`) の代替にはならない。
  あちらは**実機で毎起動**、出荷するアセンブリ版を踏む。
