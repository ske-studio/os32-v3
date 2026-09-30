# kbd_dlog — RED → GREEN の記録

票: [`docs/tasks/gui/TASK_KBD_NAV.md`](../../docs/tasks/gui/TASK_KBD_NAV.md) §3 (実機で確かめることの準備)
対象: `drivers/kbd_dlog.c` (リングの純粋部) / `drivers/kbd.c` (IRQ1 ハンドラが積む、`kbd_diag_log`) /
`userland/shell/kbd_watch.c` (`kbdstat -w` の行)
試験: `tools/tests/test_kbd_dlog.py` + `tools/tests/kbd_dlog_host.c` (+ `tools/tests/kbd_hostshim/io.h`)
実行: `make check-kbd-dlog-host` (`check-par` の列)

## 何のための道具か

GUI のマウスキー (票 TASK_KBD_NAV) はカナをマウスモードの切り替えに使う。実機のカナ
(と CAPS) が「ロックで make、解除で break」(機械式ロック) か「押すたびに make だけ」かで
`drivers/kbd.c` の扱い (方式 A / B、票 §2) が決まるが、今の `kbdstat` では判定できない:
`last_code` は最後の 1 件だけ、`irq_count` は EMPTY / ERROR も数える、`now` は 8251 の
ステータスでカナの状態ではない。そこで IRQ1 が 0041h から**使うバイトを読むたびに**
`seq / code / 処理後の修飾 / 印` を 32 件の循環リングへ積み、KAPI v67 `kbd_diag_log` で
写し、シェルの `kbdstat -w` が 1 行ずつ出す。

## なぜホストで見るのか

**EMPTY / ERROR を積まないことは NP21/W では踏めない** (`keyboard_i43` は IRQ1 の前に
必ず RxRDY を立て、エラービットは溢れの OE しか出さない)。リングの溢れ (読み手が 32 件
以上遅れる) もエミュレータでは狙って作りにくい。`drivers/kbd.c` は IRQ1 ハンドラごと
実物をホストで回し、0043h / 0041h だけ模型 (`kbd_hostshim/io.h`) へ回す。

## RED

### RED 1 — IRQ1 が積まない (実装前の姿)

`drivers/kbd.c` の `kbd_dlog_push(...)` の 1 行を外した写しで回した (リングの純粋部と
行の組み立ては通るが、IRQ1 を通る 3 ケースが落ちる):

```
EXIT ring_basic=0
EXIT ring_wrap=0
FAIL case_irq_skip:181: n == 2
FAIL case_irq_skip:182: out[0].seq == 1 && out[0].code == 0x1D && out[0].flags == 0
...
EXIT irq_skip=1
FAIL case_irq_mods:206: n == 6
FAIL case_irq_mods:207: out[0].code == 0x72 && out[0].mods == KBD_DLOG_MOD_KANA
...
EXIT irq_mods=1
FAIL case_irq_flags_api:234: n == 2
...
EXIT irq_flags_api=1
EXIT watch_fmt=0
EXIT watch_lost=0
SUMMARY 4/7 PASS
```

### RED 2 — 変異 (否定側)

下の GREEN の `MUTATION 1〜22` がそれぞれ 1 件以上落ちること。

`kbd_diag_log` の「max を `KBD_DLOG_CAP` で頭打ち」は変異に入れていない — 保持件数が
CAP 以下なので `kbd_dlog_copy` は頭打ち無しでも CAP 件を超えて書かず、外しても振る舞いが
変わらない (等価変異)。ローカルの `tmp[]` を守る防御として残している。

## GREEN

```
$ python3 -B tools/tests/test_kbd_dlog.py --target --mutate
HOST GNU89 -Werror compile PASS (real drivers/kbd_dlog.c + drivers/kbd.c + userland/shell/kbd_watch.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT ring_basic=0
EXIT ring_wrap=0
EXIT irq_skip=0
EXIT irq_mods=0
EXIT irq_flags_api=0
EXIT irq_locks=0
EXIT watch_fmt=0
EXIT watch_lost=0
SUMMARY 8/8 PASS
MUTATION 1 RED (2 件): 最古で切らない (上書き済みの枠を古い seq のつもりで写す = 飛びが見えない)
MUTATION 2 RED (2 件): max を見ない (呼び手の配列を越えて書く)
MUTATION 3 RED (6 件): seq を進めない (同じ枠を上書きし続ける)
MUTATION 4 RED (6 件): 積む位置と読む位置が 1 つずれる
MUTATION 5 RED (2 件): 最古を 1 つ古く見積もる (上書き済みの 1 件を混ぜる)
MUTATION 6 RED (1 件): 空 IRQ も積む (読んでいないバイトが行になる)
MUTATION 7 RED (1 件): PE / FE で捨てたバイトも積む
MUTATION 8 RED (3 件): 配る前に積む (カナの行に更新前の修飾が載る)
MUTATION 9 RED (1 件): OVERRUN の印を付けない
MUTATION 10 RED (1 件): kbd_init でリングを空にしない (前の記録が残る)
MUTATION 11 RED (1 件): 引数を検査しない (NULL へ書く / 0 件を成功と答える)
MUTATION 12 RED (2 件): ロックキーを方式 A に戻す (make で反転、break を無視)
MUTATION 13 RED (2 件): ロックキーの break を無視する (make で立てるだけ = 外しても落ちない)
MUTATION 14 RED (1 件): V86 中はロックを追わない (セッション後に食い違う)
MUTATION 15 RED (1 件): 起動時に 053Ah を見ない (ロックしたまま起動すると OFF と思う)
MUTATION 16 RED (1 件): 053Ah を丸ごと写す (SHIFT / GRPH / CTRL まで引き継ぐ)
MUTATION 17 RED (1 件): 053Ah の CAPS をカナと取り違える
MUTATION 18 RED (1 件): make と break を取り違える
MUTATION 19 RED (1 件): KANA のビットを CAPS と取り違える
MUTATION 20 RED (2 件): 取りこぼしの件数を 1 多く数える
MUTATION 21 RED (1 件): 1 件だけの飛びを取りこぼしと言わない
MUTATION 22 RED (1 件): 切り詰めで NUL の分を空けない (cap を 1 バイト越える)
```

### 方式 B (2026-09-26、wt/kbd-lockkeys)

NP21/W の観測 (票 §3-1: カナ・CAPS は機械式ロック) を受けてカナ・CAPS を **make で ON、break で OFF** に
直した (票 §2)。RED は変異 12 (方式 A = 直す前のドライバの姿) と 13 (break を無視) で、どちらも `irq_mods` と
`irq_locks` が落ちる。起動時の初期値は BIOS ワークエリア 0000:053Ah (KB_SHFT_STS) のカナ・CAPS ビットから
取る (変異 15〜17)。ホストでは 053Ah の読みを `KBD_HOST_TEST` で模型 `kbd_host_bios_shift` へ回す。

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `ring_basic` | seq は 1 から通し。`after_seq` より新しい分を古い順に、`max` で打ち切り、`after_seq >= last` / `max <= 0` は 0 件 |
| `ring_wrap` | ちょうど 32 件は何も失わない。40 件で先頭が seq 9 (1〜8 は上書き) = 取りこぼしが seq の飛びで分かる、消えた seq を渡しても最古から、番兵で `max` を越えて書かない |
| `irq_skip` | IRQ1: EMPTY (0x00・NP21/W の空 0x85) と ERROR (FE、PE+OE) は積まない、DATA は積む、OVERRUN は `KBD_DLOG_F_OVERRUN` 付きで積む。捨てた分は `KbdDiag` の `empty_count` / `err_count` に数えられている |
| `irq_mods` | 修飾は配った**後**の値: カナ make → KANA、カナ break → 解除 (方式 B)、2 度目の make → KANA。SHIFT / CAPS の組み合わせ、`kbd_get_modifiers()` と一致、続きの読み (ESC の make) |
| `irq_locks` | 方式 B: カナ・CAPS とも make で ON、make の繰り返しでも ON、break で OFF (break の重複でも OFF)、互いに独立。cooked は CAPS を外すと小文字に戻る、raw (GUI 中) は修飾キー自身の行に更新後の状態。起動時は 053Ah のカナ・CAPS だけを引き継ぐ (SHIFT / GRPH / CTRL は捨てる) |
| `irq_flags_api` | V86 中は `KBD_DLOG_F_V86` (カナ・CAPS のロックは追い、SHIFT 等と押下表は更新しない)、GUI 中は `KBD_DLOG_F_GUI`、`out` NULL・`max <= 0` は `OS32_ERR_INVAL`、40 バイト溜まったら先頭 seq 9、大きな `max` は CAP で頭打ち、`kbd_init` で seq が 1 から、禁止区間の釣り合い |
| `watch_fmt` | `seq=12 code=F2 break key=72 KANA mods=CAPS|KANA` の形、印 `[OE] [V86] [GUI]`、修飾の並び SHIFT / CAPS / KANA / GRPH / CTRL、無ければ `-`、切り詰めても NUL 終端 |
| `watch_lost` | 取りこぼしの件数 (`first - prev - 1`)、`LOST seq=13..20 (8): cannot judge this span` の形、飛びが無ければ空 |

## ここでは見られないもの

- `kbdstat -w` の待ちのループ自体 (`g_api` を呼ぶ側、`userland/shell/cmd_sys.c`) —
  ESC / 30 秒で終わること、rshell からの出力がシリアルに届くことは NP21/W で見る
- 実機のカナ / CAPS が NP21/W と同じ機械式ロックか (ドライバは方式 B を採った。確かめるのは票 §3 の表 / CHECKLIST_2026-09-26 の手順 10)、ロックしたまま起動したとき BIOS の 053Ah にロックが載っているか (票 §3 の表の 6)
- 9600bps のシリアルでは 1 行 (約 45 文字) に約 50ms かかる。押しっぱなしで make が
  繰り返されると出力が追いつかずリングが上書きされ、`LOST` 行が出る (それ自体が
  「繰り返している」の印)
