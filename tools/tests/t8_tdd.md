# T8-K (full-screen GFX の画面所有者、カーネル + KAPI v48) — ホスト TDD の記録

票: [docs/archive/gui_v13/TASK_T8_fullscreen_gfx.md](../../docs/archive/gui_v13/TASK_T8_fullscreen_gfx.md) §2 の D1 / D1a / D3
実装: `exec/appslot.c` + `exec/appslot.h` (所有者の表と判定) / `exec/exec.c` (`hdr_flags` の控え・
`--cpl0` の GUI 拒否・回収の並び) / `gfx/gfx_core.c` + `gfx/gfx.h` (KAPI の門) /
`kernel/kselftest.c` / `sdk/kapi.json` (v48)
試験: `tools/tests/multiapp_impl_host.c` のケース 20 (`python3 -B tools/tests/test_multiapp_impl.py`)

## 様式

`k5b_kernel_tdd.md` / `k7_tdd.md` と同じで、**模型ではなく実物**を見る:

1. `multiapp_impl_host.c` が実物の `exec/appslot.c` を `#include` する (1 行も写さない)。
2. ホスト ILP32 GNU89 (`gcc -m32 -march=i386 -ffreestanding -Werror -nostdlib`) で走らせ、
   **同じソース**をカーネルと同じフラグの `i386-elf-gcc -Werror` でもコンパイルする ([C1])。
3. `make`・エミュレータ・配備は使わない。

### なぜ所有者の表が `exec/appslot.c` にあるのか

所有者そのものは GFX の持ち物に見えるが、**判定材料が 3 つとも AppSlot にある** —
走っている ID (`res_owner_get()`)、その ID が CPL=3 か (`cpl3`)、その ID の OS32X ヘッダ flags
(`hdr_flags`、T8 で追加)。GFX 側に置くと材料を取りに行く向きが逆になり、ホストで試験できない
(`gfx/gfx_core.c` は VRAM とポート I/O に触るので持ち込めない)。

そこで判定を**純関数** `appslot_gfx_claim_check(gui_mode, caller, cpl3, hdr_flags)` に切り出し、
状態を動かす `appslot_gfx_claim()` / `appslot_gfx_owner_exit()` と分けた。`gfx/gfx_core.c` に
残るのは KAPI の門 2 本 (`gfx_kapi_init` / `gfx_kapi_init_200`) と `gfx_screen_owner` だけで、
どれも 3 行以下。`sdk/kapi.json` の `"target"` がスロット `gfx_init` / `gfx_init_200` を門へ
向けているので、生成物は 1 行も手で触っていない ([ABI1])。

`exec/exec.c` はホストに持ち込めない (CR3 / setjmp / ページテーブル)。ケース 20 が実物を叩くのは
判定と回収の 2 つで、`exec_launch` が起動時に写す 2 語 (`ctx->cpl3` / `ctx->hdr_flags`) だけは
ハーネスの `ma_start_gfx()` に写してある — 写した部分の正しさは言わない。

## 試験の区分 — `multiapp_impl_host.c` ケース 20 (33 チェック)

| 検査 | 何を固定したか |
|---|---|
| 20a〜20c | 起動直後の所有者は WM (1)。アプリを立てただけでは移らない (`gfx_init` を呼んでいない) |
| 20d | **CUI 中 (con_sink 無効) は所有者を触らない** — gshell が居らず、全画面は従来どおり誰でも取れる |
| 20e〜20h | GUI 中の `gfx_init` で所有者がその ID へ移る。二度呼んでも変わらず、**他人の回収では戻らない** |
| 20i〜20l | 正常終了でも `exec_kill` でも所有者は WM へ戻る。park しただけでは戻らない (WM が上書きしない根拠) |
| 20m〜20q | 2 本目が取った画面は 2 本目のもの。画面を持っていない 1 本目の回収では所有者が動かない |
| 20r〜20u | **宣言 (`OS32X_FLAG_GFX`) の無い CPL=3 の `gfx_init` は `OS32_ERR_INVAL`**。所有者は動かず `gfx_init_reject_count` だけが 1 増える (D1a) |
| 20v〜20w | CUI 中は宣言が無くても従来どおり通り、素通しは拒否として数えない |
| 20x〜20z | CPL=0 の子 (`--cpl0`) は宣言があっても所有者にならない。WM 自身 (owner 1) の復帰の `gfx_init` も所有者を動かさない |
| 20A〜20D | **GUI からの `--cpl0` は生存アプリ 0 本でも `OS32_ERR_INVAL`** (D1)。拒否は池も帯も 1 つも動かさない |
| 20E〜20G | CUI (`exec_run`) 経路は従来どおり通り帯を claim する。シェル帯の載せ替えは GUI 判定の対象外 |

ケース 19 (K5b の `--cpl0` 受入) は `appslot_cpl0_admit` に `gui` 引数が増えたので、
CUI 経路 (`gui = 0`) を明示して同じ検査を通してある。ハーネスの `ma_start_cpl0_gui(is_shell, gui)`
は拒否の戻り値を `OS32_ERR_FULL` へ丸めずそのまま返すようにした (以前は丸めていたので、
`OS32_ERR_INVAL` と `OS32_ERR_FULL` を区別できなかった)。

## RED → GREEN

最初の RED はコンパイルエラー: `appslot_cpl0_admit` が 1 引数のままのハーネスが
「too few arguments to function」で落ちた (署名を広げた = K5b A1 の条件を広げた印)。

そこから、実装を 1 か所ずつ「ありそうな間違い」に差し替えて、試験が**その間違いだけ**を
捕まえることを見た。差し替えは全部戻してある (`exec/appslot.c` は GREEN の形)。

| # | 差し替え | 落ちた検査 |
|---|---|---|
| R1 | `appslot_gfx_claim` が所有者を取らない (`if (r > 0) g_gfx_owner = r;` を外す) | 20f / 20g / 20h / 20k / 20n |
| R2 | `appslot_gfx_owner_exit` を no-op にする (回収で戻さない) | 20j / 20l / 20o / 20q |
| R3 | 宣言ビットを見ない (`hdr_flags & OS32X_FLAG_GFX` の判定を外す) | 20s / 20t / 20u / 20v / 20w / 20x |
| R4 | `gui_mode` を見ない (CUI 中も所有者を取る) | 20d / 20v / 20w |
| R5 | `appslot_cpl0_admit` の `gui` 判定を外す (GUI からの `--cpl0` を通す) | 20A / 20B / 20C / 20D / 20E |

R3 で 20v / 20w (CUI 中は通る) と 20x (CPL=0 は取らない) も落ちるのは、判定を外すと
「本来なら CUI・CPL=0 の枝で素通しになるはずの呼び出しが所有者を取ってしまう」ため —
拒否と素通しが同じ関数の枝なので、片方を壊すともう片方も動く。

R4 で 20v / 20w が落ちるのも同じ理由 (CUI の素通しが GUI の判定へ落ちる)。

## GREEN の実測

```
$ python3 -B tools/tests/test_multiapp_impl.py
HOST ILP32 GNU89 COMPILE PASS
...
  ok   20G シェル帯の載せ替えは GUI 判定の対象外
ALL PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
```

221 チェック (K7 までの 188 + ケース 20 の 33)、失敗 0。

## 巻き込み — `boot_splash_native_host.c` (2026-09-12、f19dd00 の後)

`make check` の `check-boot-splash-host` が `undefined reference to 'con_sink_is_enabled'` /
`appslot_gfx_claim` / `appslot_gfx_owner` で落ちた。このハーネスは `gfx/gfx_core.c` を
**ホストで直リンク**するので、T8 で門 (`gfx_kapi_init` / `gfx_kapi_init_200` /
`gfx_screen_owner`) が増えた分だけ未定義参照が増える。

直したのは試験側だけ (`tools/tests/boot_splash_native_host.c` にスタブ 3 本)。このハーネスが
見るのはバックエンドの選択と 9801 のライフサイクルで、画面の所有者は対象外 — 所有者は
ケース 20 が実物の `exec/appslot.c` で見る。なので「CUI 中 / 誰も所有していない」=
門が素通しになる値 (`0` / `0` / `1`) を返すだけにした。**カーネル本体は 1 行も変えていない。**

## kselftest (ブート時、実機)

`kernel/kselftest.c` の `test_gfx_owner()` が `appslot_gfx_owner_selftest()` を呼び、2 項を見る:

- `gfx owner moves on claim, returns on exit` — CUI 中は動かず、GUI 中に宣言付きで呼べば移り、
  他人の回収では戻らず、本人の回収で WM へ戻る
- `gfx claim without OS32X_FLAG_GFX is refused` — GUI 中の宣言なしは `OS32_ERR_INVAL` で
  所有者は動かず `gfx_init_reject_count` が 1 増え、CUI 中は通って数も増えない

借りるのは空きスロット `APP_ID_MAX` で、スロットの中身・`cur`・owner・所有者・カウンタは
丸ごと保存して戻す (`appslot_resume_mark_selftest` と同じ作法)。

**この項はまだ実機で踏んでいない** — `make` もエミュレータも使っていない ([V4])。
ホストでは `appslot_gfx_owner_selftest()` 自体は走らせていない (ケース 20 が同じ遷移を
より細かく見ているため)。

## 未実施

- `make` (clean ビルド / `make check` / `make external`) — コーダーの禁止事項。
  `tools/check_kapi_version.py` と `tools/check_constraints.py` は単体で走らせて通した。
  `tools/check_manifests.py` / `tools/check_privileged.py` はビルド成果物を要求するので未実行。
- 実機 (NP21/W) の受入 F1〜F7。配備・エミュレータ操作も禁止。

---

# T8-2 (CUI 専用の宣言 + 「拒否は畳む」、カーネル + ビルド系) — 追記 2026-09-12

票: [TASK_T8_fullscreen_gfx.md](../../docs/archive/gui_v13/TASK_T8_fullscreen_gfx.md) §6 の F5 / F1 後半
実装: `sdk/include/os32/os32_kapi_shared.h` (`OS32X_FLAG_CUI_ONLY` = 0x0010) / `sdk/mkos32x.py`
(`--cui-only`) / `build/programs.mk` + `build/app.conf` (4 列目 `gfx`|`cui`) /
`tools/check_manifests.py` §2b / `exec/appslot.c` + `exec/appslot.h` (`appslot_cui_only_admit`、
拒否で `abort_req`) / `exec/exec.c` (`exec_launch` の砦) / `gfx/gfx_core.c` (門の文言) /
`kernel/v86.c` (`v86_gui_refuse`) / `kernel/kselftest.c`
試験: `multiapp_impl_host.c` ケース 21 (`python3 -B tools/tests/test_multiapp_impl.py`)

## なぜ 2 つ足したのか

**F5 の不合格**: T8 は「GUI から V86 / VDM を起動させない」を `OS32X_FLAG_FORCE_CPL0` で
判定したが、`userland/cmds/v86.bin` の flags は **0x0**。v86 は CPL=3 のプログラムで、V86 へは
KAPI (`v86_selftest` / `v86_disktest` / `v86_boot` / `v86_boot2`) を通してカーネル側から入る。
CPL で判定する砦では原理的に捕まらないので、**宣言ビットを 1 つ増やした**。

**F6 の実測**: 宣言の無い `gfx_init` を「断って続行させる」と、プログラムは失敗を知らないまま
描画 KAPI と VRAM 直書きで描き続け、`gfx_init_reject_count` が 2 になった後に FPS 計測の絵が
上 200 ラインへ出た。**拒否 = そのアプリを畳む**に変えた。走っている本人なので `exec_kill` は
使えず、K5c の CTRL+STOP と同じく `abort_req` を立てて syscall 出口の `ring3_abort_check()` に
畳ませる (専用カウンタは増やさず `gfx_init_reject_count` / `v86_gui_reject_count` のまま)。
`ring3_abort_count` と `fault_kill_count` は畳む経路が同じなので一緒に増える (F3 と同じ註)。

## 試験の区分 — `multiapp_impl_host.c` ケース 21 (18 チェック)

| 検査 | 何を固定したか |
|---|---|
| 21a〜21c | 純関数 `appslot_cui_only_admit`: GUI からの宣言付きだけ `OS32_ERR_INVAL`。CUI は通り、宣言なしは GUI でも通る |
| 21d | 他のビット (`OS32X_FLAG_GFX`) と混ざっていても宣言を見る |
| 21e | **`FORCE_CPL0` では捕まらない** — F5 の不合格そのもの (v86.bin の flags は 0x0) |
| 21f〜21h | `exec_start` 経路 (池の admit → ヘッダ読み → cui_only の admit の順) で起動しない。拒否は池も枚数も 1 つも動かさない |
| 21i〜21j | CUI (`exec_run`) 経路は従来どおり通り、1 本立つ |
| 21k〜21o | 宣言の無い `gfx_init` の拒否で **`abort_req` が立つ** (= syscall 出口で畳まれる)。数は `gfx_init_reject_count` のまま |
| 21p〜21r | CUI 中の素通しは畳まない。二度目の拒否でも要求は立つ |

## RED → GREEN

| # | 差し替え | 落ちた検査 |
|---|---|---|
| R1 | `appslot_cui_only_admit` が `gui` を見ない (`(void)gui;` にして常に断る) | 21b / 21i / 21j |
| R2 | `appslot_cui_only_admit` が常に 0 を返す (宣言を見ない) | 21a / 21d / 21f / 21g / 21h / 21i / 21j |
| R3 | 拒否で `appslot_abort_request()` を呼ばない (断るだけ = T8 のまま) | 21n / 21q |
| R4 | 畳む位置を間違える (`claim` の判定の前に立てて素通しも畳む) | 21p |

R2 で 21i / 21j も落ちるのは、`ma_start_cui_only(1)` が通ってしまい GUI 側で 1 本立ち、
続く CUI の起動が池と `appslot_start_admit` の S2 判定に掛かるため (拒否と素通しが同じ枝)。

`--cui-only` の焼き込みは `sdk/mkos32x.py` を単体で実行して確かめた
(`--api 38 --cui-only` → ヘッダ offset 0x0C が `0x0010`)。
`tools/check_manifests.py` の §2b は関数を直接呼んで RED を 2 通り確かめた
(app.conf から `cui` を外す → `v86_* KAPI を呼ぶのに 'cui' が無い`、
4 列目を `cuix` にする → 書式 NG)。

## GREEN の実測

```
$ python3 -B tools/tests/test_multiapp_impl.py
HOST ILP32 GNU89 COMPILE PASS
...
  ok   21r 畳んだ後は 1 本も残らない
ALL PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
```

239 チェック (ケース 20 までの 221 + ケース 21 の 18)、失敗 0。
巻き込みで `tools/tests/boot_splash_native_host.c` に `shell_print` のスタブを 1 本足した
(門が拒否の理由を端末へ出すようになったため。カーネル本体は変えていない)。
`test_boot_splash_native.py` / `test_con_sink.py` / `test_multiapp_model.py` /
`test_kbd_inject.py` / `test_owner_reclaim.py` / `tools/check_constraints.py` も通した。

## kselftest

`test_gfx_owner()` に 1 項追加 —
`OS32X_FLAG_CUI_ONLY refused only from GUI` (`appslot_gfx_owner_selftest()` のビット 2)。
ビット 1 の側にも「拒否したアプリに `abort_req` が立つ / 素通しでは立たない」を足した。
**実機ではまだ踏んでいない** ([V4])。

## 未実施

- `make` (clean ビルド / `make check` / `make external`) — コーダーの禁止事項。
  `tools/check_constraints.py` と `tools/check_manifests.py` の §2b は単体で走らせて通した
  (§1 / §2 / §3 はビルド成果物を要求するので未実行)。
- 実機 (NP21/W) の受入 F5 / F6 再試験。配備・エミュレータ操作も禁止。
- gshell / 端末側の入口判定 (`classify` に `cui` を足す) は別票 T8-2W。

---

# T8-3 K (ポーリング型の協調 yield、カーネル) — 追記 2026-09-12

票: [TASK_T8_fullscreen_gfx.md](../../docs/archive/gui_v13/TASK_T8_fullscreen_gfx.md) §7 D8 (受入 F8)
実装: `exec/appslot.c` + `exec/appslot.h` (状態 `APP_STATE_WAIT_POLL` = 4、印 `parked_from_poll`、
`appslot_park_poll_check/commit`、`appslot_poll_yield_reset`、カウンタ `ring3_poll_yield_count`) /
`exec/exec.c` + `exec/exec.h` (`exec_park_poll` と `exec_resume` の poll 分岐) /
`drivers/kbd.c` (`kbd_trygetchar` / `kbd_trygetkey` の GUI 分岐) / `kernel/kselftest.c`
試験: `multiapp_impl_host.c` ケース 22 (`python3 -B tools/tests/test_multiapp_impl.py`)
**KAPI は 1 本も増やしていない** (v48 のまま。`kbd_trygetchar` / `kbd_trygetkey` の中身だけが変わる)。

## なぜ第 3 の park 点が要るのか

K7 の park 点 (`WAIT_KEY`) は `kbd_getchar` / `kbd_getkey` = **塞ぐ**呼び出し用で、
「注入リングに文字が来るまで起こさない」(空の `exec_resume` は `OS32_ERR_AGAIN`)。
ところが `gfx200_test` の FPS 段のような描画ループは `kbd_trygetchar` で **回し続ける** —
「無ければ -1」で戻る約束なので `WAIT_KEY` では止められず、K7 の後も GUI 中は
CPU を独占したままになる (票 §7 の F1 後半)。

そこで **1 周だけ譲る** park 点を足した。`WAIT_POLL` は WM から見て「常に ready、
ただし優先度は最下位」で、WM は次の周に必ず起こす。そのとき `exec_resume` は
注入リングに文字があればその 1 バイトを、空なら **EAX = -1 (キーなし)** を書く。
アプリからは `kbd_trygetchar()` が普通に戻ったように見える。

## tick の間引き — なぜ「控えは check 側で進める」のか

ポーリングは秒間数万回来るので、譲りには **PIT tick の間引き** (100Hz = 10ms に 1 回まで) が要る。
実装で 1 か所迷ったのが控え (`g_poll_last_tick`) を進める位置で、**成立 (commit) のときだけ**
進めると、表の側で弾かれる文脈 (GUI 中の CUI 入れ子 `exec_run` の子) が回すたびに
`ring3_park_reject_count` が跳ね上がる (間引きが効かない)。
なので順番を「**間引き → 表の検査**」に固定し、控えは **弾かれた試みでも** 進めるようにした。
走っているアプリは常に 1 本なので、弾かれた試みが「譲れたはずの誰か」の枠を食うことはない
(弾かれる文脈はそもそも譲れない)。この順番だけを見る検査が 22M / 22N。

`now_tick` を**引数で受ける**のは 2 つの理由: `exec/appslot.c` はハードウェアを知らない規約と、
ホストで tick を差し替えられるようにするため (`drivers/kbd.c` が `tick_count` を読んで渡す)。

## `kbd_has_key` に譲りを入れなかった理由

同じノンブロッキング系だが **同型ではない**。`kbd_trygetchar` / `kbd_trygetkey` は
「値か -1」を返すので `exec_resume` が EAX に書く値とそのまま噛み合うが、`kbd_has_key` は
真偽を返す — 1 バイトを EAX に書けば「真を返しつつその 1 バイトを落とす」ことになる。
`kbd_has_key` は **KAPI に無く** (CPL=3 から呼べない)、いま呼び手も 0 なので、
GUI 中に注入リングを見る枝だけ足して (従来は常に 0 を返していた) 譲りは入れていない。

## 試験の区分 — `multiapp_impl_host.c` ケース 22 (37 チェック)

| 検査 | 何を固定したか |
|---|---|
| 22a〜22e | tick が進んでいれば `OP_WAIT` の外でも譲れる。`ring3_poll_yield_count` が増え、**弾き数にも `ring3_kbd_park_count` にも載らない** |
| 22f〜22i | `WAIT_POLL` + 印 `parked_from_poll` のみ。`cur` / owner はシェル帯へ戻り、`exec_app_state` は **4** |
| 22j〜22o | **注入が空でも resume は通り、EAX に -1 が入る** (`WAIT_KEY` の `OS32_ERR_AGAIN` との差)。印は消え、状態は RUNNING へ |
| 22p〜22s | **同じ tick では 2 度譲らない** (10ms に 1 回まで)。譲っていないので数えず、弾き数にも載せず、アプリは走ったまま |
| 22t〜22x | tick が 1 つ進めばまた譲れる。注入があれば **1 バイトだけ** EAX に入り、残りはリングに残る |
| 22y〜22E | 印の取り違え (`parked_from_kbd` で `WAIT_POLL` を起こす) は `OS32_ERR_STALE` で `bad_frame_count` に載る。譲り中のアプリは `exec_kill` で畳める |
| 22F〜22I | 走っている本人は kill できない。GUI 中でも **CUI の入れ子 `exec_run` の子は譲れない** (弾き数に載る) |
| 22J〜22N | シェル帯 (WM top-level) からの譲りは弾かれる。**同じ tick の連打は間引きで止まり、弾き数も tick ごと 1 回まで** |

## RED → GREEN

最初の RED はコンパイルエラー — `feat/gui` の `exec/appslot.{c,h}` (cfd2768) を
`-I` で先に置いてケース 22 をビルドすると、`appslot_park_poll_check` / `appslot_park_poll_commit` /
`appslot_poll_yield_reset` / `ring3_poll_yield_count` / `APP_STATE_WAIT_POLL` /
`AppSlot.parked_from_poll` が未定義で落ちる (7 種)。

そこから実装を 1 か所ずつ「ありそうな間違い」に差し替えて、試験が**その間違いだけ**を
捕まえることを見た。差し替えは scratchpad のコピーだけで、作業ツリーには書き戻していない。

| # | 差し替え | 落ちた検査 |
|---|---|---|
| R1 | 間引きを外す (`now_tick` を見ない) | 22p / 22q / 22s / 22t / 22M / 22N |
| R2 | `check` で控え (`g_poll_last_tick`) を進めない | 22p / 22q / 22s / 22t / 22M / 22N |
| R3 | 印 `parked_from_poll` を立てない | 22f / 22k / 22l / 22m / 22o / 22s / 22t / 22v / 22w / 22x / 22y / 22A / 22D / 22E |
| R4 | `resume_check` の `WAIT_POLL` の枝で印を見ない | 22z / 22A / 22B / 22C / 22D / 22E |
| R5 | `kill_check` が `WAIT_POLL` を畳ませない | 22B |
| R6 | `appslot_state` が 4 を返さない (2 に丸める) | 22i |
| R7 | 間引きを表の検査の**後**に置く | 22M / 22N |

R7 は最初の版の試験では **捕まえられなかった** — 22p/22r は `cur` が譲れるアプリのままなので、
順番を入れ替えても間引きが先に効いてしまう。順番だけを見る 22M / 22N (譲れない文脈の連打)
を足して捕まるようにした。R3 が 14 件も落とすのは、印を立てないと `resume_check` が
`OS32_ERR_STALE` を返し、以後の park / resume の連鎖が全部ずれるため。

## GREEN の実測

```
$ python3 -B tools/tests/test_multiapp_impl.py
HOST ILP32 GNU89 COMPILE PASS
...
  ok   22L その拒否は弾き数に載る
  ok   22M 同じ tick の連打は間引きで止まる (間引きは表の検査より先)
  ok   22N 弾き数も tick ごと 1 回まで
ALL PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
```

276 チェック (ケース 21 までの 239 + ケース 22 の 37)、失敗 0。
`test_multiapp_model.py` / `test_kbd_inject.py` / `test_con_sink.py` / `test_owner_reclaim.py` /
`test_boot_splash_native.py` / `tools/check_constraints.py` も通した (巻き込みなし)。

カーネル側 3 本はカーネルと同じフラグで単体コンパイルして確かめた ([C1]、`-Wall -Wextra` 無警告):

```
$ i386-elf-gcc -std=gnu89 -m32 -march=i386 -ffreestanding ... -O2 -Wall -Wextra -D__KERNEL_BUILD__ \
    -c drivers/kbd.c exec/exec.c exec/appslot.c kernel/kselftest.c
(4 本とも警告 0)
```

## kselftest (ブート時、実機)

`appslot_resume_mark_selftest()` に 2 項追加 (`kernel/kselftest.c` の `test_resume_mark`):

- `resume needs the poll mark (WAIT_POLL)` — ビット 6。`WAIT_POLL` を `parked_from_wait` /
  `parked_from_kbd` の印では起こせず `OS32_ERR_STALE` + `bad_frame_count`、
  `parked_from_poll` を立てれば通る。譲り中のアプリは `kill_check` が通す
- `poll yield is throttled to one PIT tick` — ビット 7。同じ tick の 2 度目は `OS32_ERR_AGAIN` で
  弾き数に載らず、tick が進めば間引きを抜けて表 (`cur` = シェル帯) の側で弾かれ、
  その連打はまた間引きで止まる (= 順番が「間引き → 表」で控えが check 側)

ビット 4 (`exec_app_state`) にも `WAIT_POLL` → 4 を足した。借りるスロット・`cur` / owner・
カウンタ・控えは丸ごと保存して戻す (既存の作法どおり)。
**この 2 項はまだ実機で踏んでいない** — `make` もエミュレータも使っていない ([V4])。

## 未実施

- `make` (clean ビルド / `make check` / `make external`)、配備、エミュレータ操作 — コーダーの禁止事項。
  `tools/check_constraints.py` は単体で走らせて通した。
- 実機 (NP21/W) の受入 F8 (`gfx200_test` の FPS 段で Space → 自分で終了、`ring3_poll_yield_count` が
  秒数 × ≤100 で増える、譲りあり / なしの相対性能)。
- WM (gshell) 側 = `WAIT_POLL` を「常に ready、優先度は最下位」で起こす分は別票 T8-3 W。
  カーネルだけを入れて gshell が 4 を知らないと、譲ったアプリが二度と起きない
  (`app_state` が 4 を返しても `ready_to_run` が偽) — **K と W は同時に入れること**。
