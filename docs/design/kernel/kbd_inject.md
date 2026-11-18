# kernel/kbd_inject.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_K7_input (§1 D1〜D7、§5 R1・R2・B・C・D) の契約を今のソースと照らして要約
ソース: `kernel/kbd_inject.c`・`include/kbd_inject.h`

GUI 中の打鍵の注入リング。GUI 中は IRQ1 が raw リングにしか積まず、打鍵は WM (gshell) が受けてフォーカス窓の端末アプリへ配る。端末アプリはそれを UTF-8 のバイト列のまま `kbd_inject` で積み、GUI 中の `kbd_getchar`・`kbd_getkey` (と、その待ちから起こされた `exec_resume`) はこのリングだけを読む。

## リング

- 容量 `KBD_INJECT_RING_SIZE` = 256 バイト。カーネルの静的配列 (kmalloc しない)。
- バイトごとに宛先を持つ (`g_inj_dest`)。0 = 端末の読み手の流れ、それ以外 = 全画面の所有者の ID (WM が積んだもの)。
- 積んだ順に 1 バイトずつ出る。UTF-8 の並び (「あ」= E3 81 82) は変えない。取り出しの途中に割り込んだ注入は末尾に付く。宛先ごとの FIFO の順は、別の宛先のバイトを先に取っても崩れない。
- あふれは**新しい方**を捨てる (con_sink と逆。古い方を捨てると打った文字列の頭が欠ける)。戻り値は積んだバイト数 (< `len` なら入らなかった分がある)。捨てたバイト数は `kbd_inject_drop_count` に足す。折り返しをまたいでも順は崩れない。
- 錠は `irq_save`/`irq_restore`。ホスト試験だけ `KBD_INJECT_NO_IRQ_LOCK` で空の錠に替える。

## `kbd_inject(utf8, len)` (KAPI) — 誰が積めるか

| 条件 | 結果 |
|---|---|
| `utf8 == NULL` | `OS32_ERR_INVAL` |
| 全画面の WM 例外 (下) に当たる | 宛先 = 画面の所有者 (`appslot_gfx_owner()`) で積む |
| WM の pump の中 (`ring3_wm_depth > 0`) で例外に当たらない | `OS32_ERR_EXIST` (中断中の端末の読み手の権限を借りない) |
| con_sink の読み手が居ない | `OS32_ERR_EXIST` |
| 呼び手 (`res_owner_get()`) が読み手でない | `OS32_ERR_EXIST` |
| 読み手 | 宛先 0 で積む |
| `len == 0` (権限の検査を通った後) | 0 (エラーではない、何も積まない) |

- 拒否はリングを 1 バイトも動かさない。所有者の表を別に持たず、権限は con_sink の読み手 1 本だけ (端末は `con_sink_read` で読み手になってから注ぐ)。読み手が退場した後は、次に読み手になった 1 本だけが注げる。読み手になれなかった 2 本目の端末は注げない。
- 全画面の WM 例外: 画面の所有者がアプリ ID で、呼び手が WM (シェル帯から、または WM の pump の中) で、その所有者が読み手の端末の子孫でも読み手そのものでもない (所有者の親をたどってシェル帯に着く) とき。読み手の無い Run 起動の全画面プログラムへの打鍵がこの経路。

## 読む側

- 読めるのは「自分の ID とその親・起動要求の親 (`slot->parent` と `launch_child` の連鎖) の誰かが宛先」のバイトだけ (`inj_readable`)。宛先 0 は、con_sink の読み手がその集合に入っているときだけ読める (端末の打鍵を無関係な Run アプリに渡さない)。
- `kbd_inject_take` / `kbd_inject_peek`: 現在のアプリ宛の先頭 1 バイト。取れたら 1、無ければ 0 で `*out` は変えない。peek は動かさない。WM の pump の中は呼び手を WM (シェル帯) とみなす。
- `kbd_inject_take_for(id)` / `kbd_inject_peek_for(id)`: `exec_resume` が再開先を明示して使う (KAPI ではない)。
- `kbd_inject_pending()` (KAPI): 現在のアプリが取れる未読の数。所有権は要らない (覗けても注げない)。WM (シェル帯) には端末由来の件数も起床のヒントとして数えるが、WM は端末の流れを take・peek できない。実際に渡るかは `exec_resume` が宛先を照合し直す。

## 捨てる

- `kbd_inject_discard()`: 全部捨てて環を先頭から使える状態に戻す。CUI 復帰 (`console_text_gdc_start`) から呼ぶ。
- `kbd_inject_owner_exit(id)`: 退場する ID 宛のバイトを捨てる。退場するのが con_sink の読み手なら宛先 0 も捨てる。読み手でない ID の退場では宛先 0 を残す。残すバイトの順は保つ (読み手が退場しても WM 宛は残り、全画面の子孫が退場しても端末の打ち込みは残る)。`exec_reclaim_owned` が `con_sink_owner_exit` の直前に呼ぶ。

## 自己試験

`kdebug/selftest_kbd_inject.c` の `kst_kbd_inject()` は、ブート時にリングと `kbd_inject_drop_count` を保存して試し、元に戻す。ホスト試験でも同じものを呼び、ビットマスク 0 と、終わった後のリングが空・捨てた累計が戻っていることを見る。

## 検査

`tools/tests/test_kbd_inject.py` (`kbd_inject_host.c`)。`kernel/kbd_inject.c` と `kernel/console/con_sink.c` を同じ翻訳単位に取り込み、読み手の確立は本物の `con_sink_read()` で行う。park・resume の側は `docs/design/exec/appslot_transition.md`。
