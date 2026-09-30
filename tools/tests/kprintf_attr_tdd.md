# kprintf の属性が PC-98 の画面で読めない (ホスト試験の記録)

- 症状: 実機 **PC-9821Ra266** で FD から起動すると `DEV...OK` の右に赤い `ER`
  (= `fdc_init()` 失敗) と `MOUNT... root panic` が出るのに、**`kprintf` の
  診断行 (`[fdc] …` / `[ide] …`) が画面に 1 行も出ない**。
  「なぜ失敗したか」を書いてあるはずの行が全部消えていて、原因を追えなかった
- 対象: [`lib/kprintf_attr.c`](../../lib/kprintf_attr.c) (新設) と、
  その呼び出し元 [`lib/kprintf.c`](../../lib/kprintf.c)
- 実行: `python3 -B tools/tests/test_kprintf_attr.py [--target] [--mutate] [case ...]`
  (`make check-kprintf-attr-host` が `--target --mutate` 付きで回す)
- 試験: [`kprintf_attr_host.c`](kprintf_attr_host.c) — 実物の
  `lib/kprintf_attr.c` を 1 行も写さずに `#include` する。変換は I/O も VRAM も
  触らない純粋関数なので **模型が 1 つも要らない**
- KAPI は動かしていない。`kprintf` のシグネチャもそのまま
  (足したのは `kprintf_attr_to_pc98` の 1 本だけ)

## 1. なぜエミュレータで見つからなかったか

属性は PC-98 のテキスト属性 VRAM (`0xA2000`) に書かれる。NP21/W の
`/api/tvram` は **文字コードだけ**を返し、属性を見せない。だから
「画面に出ている」ようにしか読めず、実機の画面だけが真を言っていた。
NP21/W の窓を目で見れば分かったはずだが、検証は `/api/tvram` で自動化して
いたので誰も見ていなかった。

| | PC-98 (`include/pc98.h` の `TATTR_*`) | PC/AT (CGA アトリビュート) |
|---|---|---|
| bit0 | **表示** (0 = シークレット) | 前景 青 |
| bit1 | ブリンク | 前景 緑 |
| bit2 | リバース | 前景 赤 |
| bit3 | アンダーライン | 前景 輝度 |
| bit4 | バーチカルライン | 背景 青 |
| bit5 | 青 | 背景 緑 |
| bit6 | 赤 | 背景 赤 |
| bit7 | 緑 | ブリンク |

カーネルの `kprintf` 呼び出しは **73 か所が `kprintf(0x07, …)`**。
PC/AT の「明るい灰色の文字」のつもりで書かれた値で、PC-98 の属性としては
「色ビット無し (= 黒) + リバース + ブリンク + 表示」になる — **読めない**。
`0x0A` `0x0C` `0x0E` `0x02` `0x04` `0x0B` に至っては bit0 (表示) すら
立っていないので完全に不可視。`0xC1` `0xE1` `0xA1` だけが PC-98 流で正しい。

## 2. 直し方

`kprintf` の入口で 1 回だけ `kprintf_attr_to_pc98()` を通す。
下流 (`shell_print` / `shell_print_utf8` / `console_write` →
`tvram_putchar_at`) は全部この `attr` を使うので、入口 1 か所で取りこぼしが
無い。規則は 3 つだけ:

1. PC-98 の色ビット (`0xE0`) が立っていれば **そのまま通す**。
   `0xC1` / `0xE1` / `0xA1` のような正しい呼び出しを壊さない。
   ただし bit0 (表示) が落ちていれば立てる
2. それ以外は PC/AT の前景色として読み替える。輝度 (bit3) は捨てる
3. 色が黒 (= 0) になったら白にする。PC-98 の黒は「何も見えない」なので、
   既定色のつもりの `kprintf(0, …)` を黙って消さない

**リバース (bit2) とブリンク (bit1) は付けない。** PC/AT 流の値の下位ビットは
前景色であって、反転や点滅の指定ではない。

## 3. RED → GREEN

`kprintf_attr_to_pc98` が無い状態 (= `attr` を素通しする状態) から始めた。

### RED

`kprintf_attr_to_pc98` が恒等写像 (= `attr` を素通しする、直す前の姿) のとき、
**4 ケース全部が落ちる**。

```
FAIL real_callers:41: 07 -> 07 (want e1)
FAIL real_callers:43: 0a -> 0a (want 81)
FAIL real_callers:44: 0c -> 0c (want 41)
FAIL real_callers:45: 0e -> 0e (want c1)
FAIL real_callers:46: 02 -> 02 (want 81)
FAIL real_callers:47: 04 -> 04 (want 41)
FAIL real_callers:48: 0b -> 0b (want a1)
FAIL real_callers:51: 00 -> 00 (want e1)
EXIT real_callers=1
FAIL pc98_passthrough:67: e0 -> e0 (want e1)
FAIL pc98_passthrough:68: 80 -> 80 (want 81)
FAIL pc98_passthrough:69: 40 -> 40 (want 41)
FAIL pc98_passthrough:70: 20 -> 20 (want 21)
EXIT pc98_passthrough=1
FAIL invariants:91: (out & TATTR_VISIBLE) != 0        (x10)
EXIT invariants=1
FAIL cga_bits:119: ... 輝度ビットで色が変わる          (x8)
FAIL cga_bits:124: 01 -> 01 (want 21)
FAIL cga_bits:130: 07 -> 07 (want e1)
EXIT cga_bits=1
```

`invariants:91` の 10 件が、実機で起きていたことそのもの —
**bit0 (表示) が落ちた属性がそのまま属性 VRAM へ行く** (`0x0A` `0x0C`
`0x0E` `0x02` `0x04` `0x0B` `0x00` などが該当)。

### GREEN

```
HOST GNU89 -Werror compile PASS (real lib/kprintf_attr.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT real_callers=0
EXIT pc98_passthrough=0
EXIT invariants=0
EXIT cga_bits=0
SUMMARY 4/4 PASS
ENTRY kprintf applies conversion x1
```

### 否定側 (`--mutate`) — 6 本すべて RED

実物の `lib/kprintf_attr.c` を 1 か所ずつ壊して、試験が気づくことを見る。

```
MUTATION 1 RED (3 件): 表示ビットを立てない (色だけ指定 = シークレットのまま消える)
MUTATION 2 RED (2 件): 色が黒のときに白へ倒さない (kprintf(0, …) が不可視のまま)
MUTATION 3 RED (3 件): PC-98 流の属性の表示ビットを補わない
MUTATION 4 RED (3 件): PC-98 流の属性まで CGA として読み替える (0xC1 が赤に化ける)
MUTATION 5 RED (2 件): CGA の赤を PC-98 の青へ写す (色の対応が入れ替わる)
MUTATION 6 RED (3 件): 素通し判定から青を落とす (0x21 が緑に化ける)
```

## 4. 見ているもの

| ケース | 何を固定するか |
|---|---|
| `real_callers` | ソースに実在する値の写像 (`07→E1` `0A→81` `0C→41` `0E→C1` `02→81` `04→41` `0B→A1` `00→E1`) |
| `pc98_passthrough` | 既に PC-98 流の属性を壊さない (`C1→C1` `E0→E1`、リバース/下線付きも保つ) |
| `invariants` | 256 通り全部で **必ず見える** (表示ビット + 色ビットが立つ)、冪等、PC/AT 入力に反転・点滅を足さない |
| `cga_bits` | 輝度 (bit3) を捨てる / 青・緑・赤の対応 / PC-98 の色ビットがあれば CGA として読まない |

加えて `check_entry()` が `lib/kprintf.c` の入口に変換の 1 行があることを
静的に見る — 変換が正しくても呼ばれていなければ画面は黒いままなので。

## 5. カーネル側 (kselftest)

`kprintf` の原始命令に触ったので `kernel/kselftest.c` に `test_kprintf_attr()`
を足した (CLAUDE.md の規則)。上の 10 個の期待値をそのまま `check()` する。
`kprintf` は KAPI 公開関数でもあるので、ここが壊れるとカーネルもアプリも
同時に黙る — ブート時に毎回踏む。
