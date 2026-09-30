# kbd_status — RED → GREEN の記録

票: 実機の打鍵不達 (2026-09-23)、経緯は [`docs/POLICY_DEBUG.md`](../../docs/POLICY_DEBUG.md) §4-57
対象: `drivers/kbd_status.c` (判定・反射の可否) / `drivers/kbd.c` (IRQ1 ハンドラ・`kbd_init`・`kbd_diag`) /
`kernel/v86_kbd.c` (仮想 8251 の空読み) / `kernel/isr_stub.asm` (`irq_stub_1` が反射の可否を見る)
試験: `tools/tests/test_kbd_status.py` + `tools/tests/kbd_status_host.c`
実行: `make check-kbd-status-host` (`check-par` の列)

## 何を直しているか

実機 PC-9821Ra266 で本体キーボードの打鍵が一切届かなかった (シリアルは動く、
NP21/W では効く)。本命の原因は `kbd_init` が 0043h に書いていたコマンド語
0x14 (DTR = 0 = RTY# LOW = キーボードへの再送要求) で、これは 0x16 に直した
(ホストでは試せない — kselftest が語の値を見張る)。

同じ変更で IRQ1 ハンドラが **0041h を読む前に 0043h を見る**ようにした。
直す前は無条件に 0041h を読んでいたので、空 IRQ でも前回のバイト (か不定値) を
打鍵として配り、PE/FE の化けたバイトもそのまま配り、エラーフラグは
`kbd_init` 以後一度も解除しなかった。判定を I/O の無い純粋部に切り出して
ここで固定する。

### 実装レビューの後 (2026-09-23、Codex / Opus)

- **V86 への偽の ESC**: 空 IRQ・エラーで `kbd_irq_handler` が早く戻っても
  `irq_stub_1` は `V86_REFLECT 1` を無条件に通し、ゲストは空の仮想 FIFO から
  0x00 (= ESC のメイク) を読んでいた。ハンドラが「反射してよいか」
  (`kbd_status_reflects`: DATA / OVERRUN だけ 1) を返し、スタブは 0 なら反射を
  飛ばす。仮想 8251 の空読みも**前回のバイト** (セッション開始時は 0xFF) にした。
- **OE は正しいバイト**: 8251A の OE は「前のバイトを読む前に次が来た」で、
  レジスタのバイトは新しく正しい。OE だけなら `KBD_ST_OVERRUN` (使って ER で
  解除、`overrun_count` に数える)。PE / FE は従来どおり読み捨て (OE と重なっても)。

## なぜホストで見るのか

**EMPTY と ERROR は NP21/W では踏めない**。NP21/W の `keyboard_i43` は
`status | 0x85` を返し、IRQ1 を上げる前に必ず RxRDY (bit1) を立てる。エラー
ビットは自前のバッファが溢れたときの OE しか出さない。逆に `| 0x85` で立つ
ビット (DSR / TxEMP / TxRDY) を判定に混ぜると、エミュレータの打鍵が全部落ちる
— これも実機を待たずにここで固定する。

## RED

### RED 1 — 直す前の姿 (無条件に 0041h を読む = 常に DATA)

判定を「常に `KBD_ST_DATA`」にした写しで同じハーネスを回した
(直す前の IRQ1 ハンドラと同じ振る舞い):

```
FAIL case_empty:27: kbd_status_classify(0x00) == KBD_ST_EMPTY
FAIL case_empty:28: kbd_status_classify(0x85) == KBD_ST_EMPTY
...
FAIL case_error:36: kbd_status_classify(0x02 | 0x08) == KBD_ST_ERROR
FAIL case_error:37: kbd_status_classify(0x02 | 0x10) == KBD_ST_ERROR
...
EXIT empty=1
EXIT error=1
EXIT data=0
```

### RED 2 — 変異 (否定側)

下の GREEN の `MUTATION 1〜14` がそれぞれ 1 件以上落ちること。5 / 6 / 12 は
レビュー前の実装そのもの (OE を読み捨てる・いつでも反射する・空読みが 0x00)。

### RED 3 — レビュー前の実物 (bda95fa) に新しいハーネス

`git show 86b696b:<path>` の 3 本を写して同じハーネスを当てた:

```
kbd_status_host.c:52:47: error: 'KBD_ST_OVERRUN' undeclared
kbd_status_host.c:76:11: error: implicit declaration of function 'kbd_status_reflects'
```

判定側だけ新しくし、`kernel/v86_kbd.c` を旧のままにすると `v86empty` が落ちる:

```
FAIL case_v86empty:93: v86_kbd_in(V86_KBD_DATA) != 0x00
FAIL case_v86empty:94: v86_kbd_in(V86_KBD_DATA) == 0xFF
FAIL case_v86empty:102: v86_kbd_in(V86_KBD_DATA) == 0x1E
...
```

## GREEN

```
$ python3 -B tools/tests/test_kbd_status.py --target --mutate
HOST GNU89 -Werror compile PASS (real drivers/kbd_status.c + kernel/v86_kbd.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT empty=0
EXIT error=0
EXIT overrun=0
EXIT data=0
EXIT reflect=0
EXIT v86empty=0
SUMMARY 6/6 PASS
MUTATION 1 RED (2 件): RxRDY を見ない (空 IRQ でも 0041h を読んで前回のバイトを打鍵にする)
MUTATION 2 RED (2 件): PE / FE を見ない (化けたバイトを打鍵として配る)
MUTATION 3 RED (1 件): エラーを RxRDY より先に見る (受信データが無いのに 0041h を読み捨てる)
MUTATION 4 RED (1 件): OE を見ない (ER で解除せず、overrun_count も数えない)
MUTATION 5 RED (2 件): OE を ERROR にする (正しいバイトを読み捨てる — 修正前の挙動)
MUTATION 6 RED (1 件): いつでも反射する (空 IRQ・エラーでゲストに偽の打鍵 — 修正前の挙動)
MUTATION 7 RED (1 件): OVERRUN で反射しない (使ったバイトがゲストに届かない)
MUTATION 8 RED (5 件): RxRDY を bit2 (TxEMP) と取り違える (NP21/W では常に立つので全部 DATA に化ける)
MUTATION 9 RED (2 件): OE を化けたバイトに数える (正しいバイトを読み捨てる)
MUTATION 10 RED (2 件): FE を落とす (フレーミングエラーのバイトを使う)
MUTATION 11 RED (3 件): DSR (bit7) をエラーに数える (NP21/W の `| 0x85` で打鍵が全部落ちる)
MUTATION 12 RED (1 件): 空読みで 0x00 を返す (= ESC のメイク — 修正前の挙動)
MUTATION 13 RED (1 件): 読んだバイトを覚えない (空読みが初期値のまま)
MUTATION 14 RED (1 件): セッション開始で前回のバイトを捨てない (前のセッションの打鍵が出る)
```

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `empty` | RxRDY = 0 なら、エラービットが立っていても EMPTY (0041h を読まない)。NP21/W の空 (0x85) も EMPTY |
| `error` | RxRDY = 1 で PE / FE を 1 ビットずつ、OE と重なったとき、全ビット |
| `overrun` | RxRDY = 1 で OE だけ (NP21/W の溢れ `0x85 | 0x02 | 0x10` を含む) は OVERRUN |
| `data` | RxRDY だけ、NP21/W の打鍵 (0x87)、bit0 / bit2 / bit6 / bit7 の 16 通りの組み合わせがどれも DATA |
| `reflect` | 反射は DATA / OVERRUN だけ。0043h の 256 通りで「RxRDY かつ PE/FE 無し ⇔ 反射」 |
| `v86empty` | 仮想 8251 の 0x41 の空読みが 0x00 (ESC) でなく前回のバイト、セッション開始で 0xFF に戻る、空読みは `n_read` に数えない |

## ここでは見られないもの (実機)

- コマンド語 0x16 で実機の打鍵が戻るか (DTR = RTY# の極性は資料の記述
  だけが根拠 — 戻らなければ `kbdstat` で切り分ける、§4-57)
- IRQ1 がそもそも来るか (PIC の経路)、キーボードが送っているか
- エラー解除 (ER 込みのコマンド語の書き直し) の後に受信が続くか
- `irq_stub_1` が戻り値で反射を飛ばすこと自体 (アセンブリ。V86 セッション中の空 IRQ は
  NP21/W では起きない — 実機か、`/api/cmd` で V86 ゲストを動かしながら `kbdstat` の
  `empty` が増えるときに見る)
