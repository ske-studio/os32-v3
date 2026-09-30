# K7-K (入力統合、カーネル + KAPI v47) — ホスト TDD の記録

票: [docs/archive/gui_v13/TASK_K7_input.md](../../docs/archive/gui_v13/TASK_K7_input.md) §1 D1〜D7 / §5 (R1 / R2 / B / C / D)
実装: `kernel/kbd_inject.c` + `include/kbd_inject.h` / `drivers/kbd.c` / `exec/appslot.c` + `exec/appslot.h` /
`exec/exec.c` (`exec_park_kbd` / `exec_resume` の kbd 分岐) / `kernel/kselftest.c` / `sdk/kapi.json` (v47)
試験: `tools/tests/kbd_inject_host.c` + `test_kbd_inject.py` (`make check-kbd-inject-host`) と
`tools/tests/multiapp_impl_host.c` のケース 19 (`make check-multiapp-model-host`)

## 様式

`con_sink_tdd.md` / `k5b_kernel_tdd.md` と同じで、**模型ではなく実物**を見る:

1. `kbd_inject_host.c` が `kernel/kbd_inject.c` を `#include` する (1 行も写さない)。
   権限の照合相手も模型にしない — **`kernel/con_sink.c` を同じ翻訳単位に入れ**、
   読み手の確立は本物の `con_sink_read()` で行う (注入の権限 = con_sink の読み手 1 本、票 §5 R2)。
2. `multiapp_impl_host.c` は実物の `exec/appslot.c` と `kernel/kbd_inject.c` の両方を取り込み、
   K5a の 84 検査に加えてケース 19 (第 2 の park 点) を回す。
3. ホスト ILP32 GNU89 (`gcc -m32 -march=i386 -ffreestanding -Werror -nostdlib`) で走らせ、
   **同じソース**をカーネルと同じフラグの `i386-elf-gcc -Werror` でもコンパイルする ([C1])。

ホスト側だけ `-DCON_SINK_NO_IRQ_LOCK` / `-DKBD_INJECT_NO_IRQ_LOCK` を付ける (CPL=3 では
`cli` / `popfl` を実行できない)。`make`・エミュレータ・配備は使わない。

`exec/exec.c` はホストに持ち込めない (CR3 / setjmp / ページテーブル)。ケース 19 は
`exec_park_kbd` / `exec_resume` の **kbd 分岐 3 行だけ**をハーネスへ写す
(`ma_park_kbd` / `ma_resume_kbd`) — K5a のケース 12〜16 が gshell の選択規則を写しているのと
同じ扱いで、写した部分の正しさは言わない。

## 試験の区分

### `kbd_inject_host.c` (58 チェック)

| 区分 | 何を固定したか |
|---|---|
| 1 permission | 読み手未確立 / 別の所有者 / 読み手の退場後 は `OS32_ERR_EXIST`、`NULL` は `OS32_ERR_INVAL`、`len 0` は 0。拒否はリングを 1 バイトも動かさない。端末が 2 本目 (busy 側) は読み手になれず注入もできない (票 §3 の 2) |
| 2 byte order | 積んだ順に 1 バイトずつ。UTF-8 (「あ」= E3 81 82) の並びは変えない (D4)。取り出しの途中に割り込んだ注入は末尾に付く |
| 3 pending | 注入で増え取り出しで減る。**所有権は要らない** — WM が `ready_to_run` に使う (§5 指摘 C)。覗けても注げはしない |
| 4 discard | `kbd_inject_discard()` (CUI 復帰) で空になり、環は先頭から使える。`kbd_inject_owner_exit(読み手)` で捨て、別 ID の退場では捨てない (D5) |
| 5 overflow | 容量内では 1 バイトも落とさない。あふれは **新しい方**を捨て (con_sink と逆)、積んだ数を戻り値で返す。`kbd_inject_drop_count` が超過分だけ進む。折り返しを跨いでも順序はそのまま |
| 6 selftest | `kbd_inject_selftest()` (実機の kselftest がブート時に呼ぶもの) がホストでもビットマスク 0 |

### `multiapp_impl_host.c` ケース 19 (33 チェック)

| 検査 | 何を固定したか |
|---|---|
| 19b〜19h | OP_WAIT の外でも kbd 待ちなら park できる。`WAIT_KEY` + 印 `parked_from_kbd`、cur/owner はシェル帯へ、`exec_app_state` は **3**、`ring3_kbd_park_count` が進み `park_reject_count` は進まない |
| 19i〜19m | 注入が空の resume は `OS32_ERR_AGAIN`。印も状態も動かず、`switch_count` も `bad_frame_count` も進まない (「空」は「印なし」ではない) |
| 19n〜19s | 注入があれば起き、EAX に入るのは **1 バイトだけ**。残りは pending に残る。印は 1 回きり |
| 19t〜19w | `WAIT_KEY` を OP_WAIT の印では起こせない (`STALE` + `bad_frame_count`)。鍵待ちのアプリは `exec_kill` で畳める (D5) |
| 19x〜19F | 2 バイト目が次の resume で届く。走っている間は `exec_kill` を呼べない。CUI の入れ子の子とシェル帯 (top-level) からの park は弾かれる |

## RED → GREEN

実装を 1 か所ずつ「ありそうな間違い」に差し替えて、試験が**その間違いだけ**を捕まえることを見た。
差し替えは全部戻してある (`kernel/kbd_inject.c` / `exec/appslot.c` は GREEN の形)。

### R1 — 注入の権限を見ない (誰でも注げる)

`kbd_inject()` の `con_sink_reader_get()` / `res_owner_get()` の照合を外す。

```
  FAIL 1a 読み手が未確立なら OS32_ERR_EXIST
  FAIL 1b 拒否された注入はリングを動かさない
  FAIL 1f 注いだ分が pending に出る
  FAIL 1g 別の所有者からは OS32_ERR_EXIST
  FAIL 1h その拒否もリングを動かさない
  FAIL 1j len 0 は何も積まない
  FAIL 1k 読み手の退場後は誰も注げない
  FAIL 1n その 2 本目は注入も拒否される (busy 側は注がない)
  FAIL 3e 覗けても注げはしない
  FAIL 6a 自己診断のビットマスクは 0
FAILURES
EXIT kbd_inject_host=1
```

### R2 — あふれで **古い方** を捨てる (con_sink と同じ方針にしてしまう)

`inj_push()` の先頭に「入るまで tail を進める」ループを足す。

```
  FAIL 5d 満杯への注入は 0 (積めなかった数が呼び手に返る)
  FAIL 5e 捨てたバイト数が kbd_inject_drop_count に載る
  FAIL 5f 残っているのは**最初に打った**バイト (古い方を捨てない)
  FAIL 5g 入る分だけ積み、積んだ数を返す (半端も正直に)
  FAIL 5h 入らなかった 2 バイトを数える
  FAIL 6a 自己診断のビットマスクは 0
FAILURES
EXIT kbd_inject_host=1
```

### R3 — 読み手の退場でリングを捨てない

`kbd_inject_owner_exit()` の `kbd_inject_discard()` を外す。

```
  FAIL 4e 読み手の退場では捨てる
  FAIL 4f 読み手なしの ID では捨てない
FAILURES
EXIT kbd_inject_host=1
```

(4f が続けて落ちるのは、4e で残った 2 バイトがそのまま次の検査に載るため。
捨てる口が 1 つでも欠けると「前の端末の打鍵が次の端末へ漏れる」形で出る。)

### R4 — `WAIT_KEY` の印を見ない (どの印でも起こせる)

`appslot_resume_check()` の `WAIT_KEY` 分岐から `parked_from_kbd` の照合を外す。

```
  FAIL 19u WAIT_KEY を OP_WAIT の印では起こせない
  FAIL 19v 印の取り違えは bad_frame_count に載る
  FAIL 19w 鍵待ちのアプリは exec_kill で畳める (D5)
  FAIL 19x 印を戻せば起こせる (残りの 'b')
  FAIL 19y 2 バイト目が次の resume で届く
  FAIL 19z 注入リングは空に戻る
FAILURES
```

### R5 — `exec_kill` を `PARKED` だけに戻す (票 §5 の指摘 D の逆)

```
  FAIL 19w 鍵待ちのアプリは exec_kill で畳める (D5)
FAILURES
```

### R6 — kbd の park でも「OP_WAIT の中」を要求する (第 2 の park 点が立たない)

`appslot_park_kbd_check()` に `|| !g_cur_op_is_wait` を足す。ケース 19 の 19 検査が落ちる
(park が 1 度も成立しないので以降が総崩れになる = これが「今まで `hlt` で止まっていた」状態)。

```
  FAIL 19b OP_WAIT の外でも kbd 待ちなら park できる
  ... (19c〜19z の 19 件)
FAILURES
```

### GREEN (最終)

```
$ python3 -B tools/tests/test_kbd_inject.py
  ok   6a 自己診断のビットマスクは 0
  ok   6b 終わった後は空
  ok   6c 捨てた累計を元に戻す
ALL PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
EXIT kbd_inject_host=0

$ python3 -B tools/tests/test_multiapp_impl.py
  ok   19E 畳んだら WM top-level へ戻る
  ok   19F シェル帯 (WM top-level) からの park は弾かれる
ALL PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
```

## この試験が言わないこと

- **実機のことは何も言わない。** 受入 I1〜I5 (ゲスト) は 1 つも実施していない。
  `make` も配備もエミュレータ操作も行っていない ([V4])。
- `exec/exec.c` の実物 (`exec_park_kbd` の longjmp / フレーム 13 語の保存 / CR3 の載せ替え /
  `exec_resume` の `ring3_resume`) はホストに持ち込めないので通っていない。ケース 19 が
  見ているのは表 (`appslot.c`) とリング (`kbd_inject.c`) の噛み合わせだけ。
- `drivers/kbd.c` の分岐 (GUI 中だけ注入リングを見る / `hlt` へ落ちる境界) は
  クロスコンパイルが通ることしか見ていない。IRQ1・`hlt`・rshell のタイムアウトは
  ホストに無い。CUI 経路が本当に無変更かは回帰 (受入 I4) の担当。
- KAPI 経由 (CPL=3 の `int 0x80`、ディスパッチャのポインタ検証) は通っていない。
- WM (gshell) 側と端末アプリ側 (K7-W / K7-A) は別票。ここでは `exec_app_state == 3` と
  `kbd_inject_pending() > 0` という**カーネルが出す材料**までしか用意していない。
