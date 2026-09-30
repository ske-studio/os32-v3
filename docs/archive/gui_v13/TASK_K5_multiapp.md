# K5: GUI アプリ 4 本の同時実行 (契約 T2a) — v1.3 の最初の票

> 発行: PM (2026-09-10) / 状態: **受入完了 (2026-09-12)**
> レーン: K (C、カーネル背骨) / 前提: ユーザー決裁 B ([AUDIT §6](AUDIT_2026-09-10.md))
> 親: [PLAN.md](../../tasks/gui/v13/PLAN.md) / 契約: T2, T2a, T3, T4, U8 ([API_CONTRACTS](../../tasks/gui/API_CONTRACTS.md))、
> v1.2 S1 (暫定「1 本」を撤廃)、S2〜S8 ([v12 CONTRACTS](../../tasks/gui/v12/CONTRACTS.md))
> 排他: `kernel/**` `exec/**` `kapi/**` `include/memmap.h` `sdk/kapi.json` + 生成物。
> gshell 側 (`userland/gshell/**`) は K5b で W レーンと調停する。

## ゴール

凍結契約 T2/T2a のとおり、**GUI アプリを最大 4 本同時に生かす**。
PD 切替が起きるのは `gui_call(OP_WAIT)` の中だけ (T2a「譲り合いの点は OP_WAIT だけ」)。
5 本目は `ERR_FULL`。アプリ終了・kill 時はその**アプリだけ**の資源を回収する (T4/U8)。
CUI シェル (`shell.bin`) の入れ子 `exec_run` は変えない。

これは端末 (外部アプリ) と CUI 子の同居の土台であり、v1.3 の残件すべての前提になる。

### 「同時」の意味 (2026-09-10 ユーザー確認済み) — Win3.1 の GetMessage 方式

| やること | やらないこと |
|---|---|
| アプリ 4 本が**メモリに載ったまま**生きている (窓が残る) | タイマ割込みでの切替 (プリエンプション) |
| 動くのは常に **1 本だけ**。他は `OP_WAIT` の中で止まっている | 裏で別アプリが走る、スレッド、優先度、タイムスライス |
| 切替は、動いているアプリが `OP_WAIT` に入ったとき、WM が「イベントの届いた別のアプリ」を 1 本起こす (窓をクリックしたらそのアプリが次に動く) | 動いている最中の横取り |

本票で「切替」「起こす」と書くのはすべてこの意味。ROADMAP §2 の「timer interrupt を利用した
multi-task」(v2.0 で検討) は**本票の範囲外**で、その足場も作らない。

## 現状の事実 (2026-09-10 に PM が静的確認)

| # | 事実 | 根拠 |
|---|---|---|
| F1 | 生きている ring3 アドレス空間は 1 本 (`g_ring3_as`)。`exec_run` は**呼び出し側を塞ぐ** (setjmp/longjmp で子の終了まで戻らない) | `exec/exec.c:213`、`ExecContext.jmpbuf`、`run_program` (`gshell/src/lib.rs:233`) |
| F2 | ExecContext は**入れ子のスタック** (`exec_ctx_stack[MAX_EXEC_NEST=4]`)。段ごとに sbrk/heap/stack/guard を持ち、注記に「PD切り替え不要」 | `exec/exec.c:136-153`、`exec/exec.h:26` |
| F3 | 資源の所有者 = **ネスト段** (`cur_res_owner`)。FD / redirect / pipe / GUI 窓 / タイマの回収は `*_owned(exec_nest_level)` で段ごと | `fs/fd_redirect.c:21-22`、`exec/exec.c:531-540`、`kernel/gui.c:71` |
| F4 | アプリの物理ページは `pgalloc_mark_used(ca/cb)` で **0x500000 以上の固定物理帯**を使い、仮想=物理 (identity)。exec_heap/sbrk のアドレスも物理そのもの | `exec/exec.c:977-978,1064`、ExecContext 注記「アイデンティティマッピング」 |
| F5 | それでも PD はアプリごとに作れる (`paging_addrspace_create_n` + `map_user_range`)。同じ仮想 0x500000 に別物理を置く機構は**既にある** (共有ライブラリの .data/.bss がそれ) | `kernel/paging.h:191-237`、契約 T2a 可視性の注記 |
| F6 | カーネルスタックは 1 本。syscall は `int80_stub` → `ring3_syscall_dispatch(frame*)` で、`pushad` フレームがカーネルスタック上。`OP_WAIT` は gshell (CPL=0 常駐) の `op_wait` が `wm_cycle` + `sys_halt` で回す | `kernel/ring3_entry.asm:32-`、`gshell/src/handler.rs:355-` |
| F7 | gshell は既に 4 スロット (`GUI_SLOT_MAX`) と owner を持ち、`OP_INIT` は常に 0 を返す | `gshell/src/wm.rs:404`、契約 T2a |
| F8 | 起動失敗・fault・CTRL+STOP の畳み方は「1 本」前提: `ring3_fault_kill` は master CR3 復帰 → AS 破棄 → longjmp | `exec/exec.c:277-`、`exec.c:234-302` |

## 段階 K5a — 設計 (読取専用) + ホストモデル

コードを変えない。次を**根拠付きで**決めて本票の §設計 に書き、ホストで検証できる状態モデルを添える。

1. **物理ページとアドレス** (F4/F5): アプリごとに物理ページを `pgalloc` から取り、固定仮想 0x500000 へ写す。
   ローダ・`exec_heap`・sbrk・スタック・guard ページが identity を前提にしている箇所を全部列挙し、
   仮想≠物理でも成り立つ形を示す。共有ライブラリ帯 (0x400000〜) の .text 共有 / .data 別はそのまま。
2. **アプリごとのカーネル側コンテキスト** (F6): 切替はどこで、何を保存するか。候補は
   (a) アプリごとのカーネルスタック (4 × 16KB) を持ち `ESP`+`CR3`+`TSS.esp0` を入れ替える、
   (b) `OP_WAIT` の syscall フレームだけを保存して単一カーネルスタックで済ませる。
   切替を呼ぶのは gshell の `op_wait` (新 KAPI `gui_yield(next)` 等) か、カーネルの `gui_call` 出口か。
   IF の状態 (`int80_stub` の `sti`/`cli` 対) と、切替中の IRQ を明記する。
3. **所有者 ID** (F3): 所有者を「ネスト段」から「アプリ ID (1 = シェル、2〜5 = アプリ)」へ。
   `res_owner_get/set` の意味、切替時に誰が set するか、`*_owned(id)` の回収がアプリ単位で
   閉じること。CUI 側 (入れ子 exec_run) では従来どおり段 = ID になるよう互換を示す。
4. **起動・切替・終了** (F1/F8): いまの `exec_run` は呼び出し側 (gshell top-level) を子の終了まで
   塞ぐ。4 本を生かすには「起動したアプリが最初の `OP_WAIT` に入ったら WM へ戻る」形が要る
   (塞がない起動)。その名前・引数は設計で決める。切替は **GetMessage 方式**: `OP_WAIT` で
   止まっているアプリのうちイベントの届いたものを WM が 1 本起こすだけで、スケジューラは持たない。
   アプリの `exit` / fault / CTRL+STOP / 起動失敗のそれぞれで、その ID だけを畳んで WM へ戻る経路。
   フォーカスの無いアプリへの CTRL+STOP は届かない (T6 はフォーカス窓のアプリ宛)。
5. **メモリの勘定**: 4 本が要求どおりに入らないときは**拒否** (`ERR_NOMEM` / `ERR_FULL`)。スワップは
   しない (ユーザー方針 2026-09-10、[memory `APP_BAND_PDE`](../../tasks/memory/APP_BAND_PDE.md))。
   8MB (CUI) / 9MB / 15MB (PEGC) の各構成で、gshell + アプリ n 本の見積表を出す。
6. **SHM スロット** (F7): `OP_INIT` で 0〜3 を配り、5 本目は `ERR_FULL`。所有者 ID との対応。
7. **変えないもの**: `shell.bin` の入れ子 `exec_run`、v86、`sys_switch_shell` (T9)、
   `mkos32x --cpl0` の例外扱い。KAPI 追加は末尾追記のみ ([ABI2])、版数は PM が決める ([ABI3])。
8. **ホストモデル**: スケジューラ・所有者・スロットの状態遷移を `tools/tests/multiapp_model_host.c`
   (または Rust) に切り出し、`test_multiapp_model.py` で「5 本目拒否 / 終了で 1 本分だけ回収 /
   切替は OP_WAIT でだけ / fault で他アプリの資源に触らない」を RED→GREEN で示す。
   `make check` への登録は PM。

成果物: 本票 §設計 (追記)、ホストモデル + TDD 記録 (`tools/tests/multiapp_model_tdd.md`)、
KAPI 追加候補の一覧 (名前・引数・戻り値・エラー)。**実装はしない。** 独立レビュー (ユーザー経由) を
通してから PM が契約を凍結し、K5b を発注する。

## 段階 K5b — 実装 (K5a 凍結後に発注)

カーネル (K) と gshell (W) を分けて発注する。受入はゲスト実機:

| ID | 試験 | 合格条件 |
|---|---|---|
| G1 | `gui_demo` と `gui_bench` を同時に起動 | 両方の窓が出て、それぞれのクリックが自分に届く (`gui_bench` の `CLICK n`) |
| G2 | 片方を閉じる | もう片方は動き続け、閉じた側の FD / 窓 / タイマだけが回収される |
| G3 | 5 本目 | `ERR_FULL` で起動されず、既存 4 本は無事 |
| G4 | フォーカス窓のアプリに CTRL+STOP | そのアプリだけ畳まれる。他は無事 |
| G5 | fault するアプリ (`ring3_fault`) を 1 本混ぜる | そのアプリだけ kill、`fault_kill_count` +1、他は無事 |
| G6 | 8MB / 15MB 構成 | **8MB で GUI アプリが少なくとも 1 本立つ** (ユーザー決裁 2026-09-11: 複数は本体メモリ依存)。入らないときは `ERR_NOMEM` / `ERR_FULL` で拒否し、既存アプリは無事 (スワップしない) |
| G7 | 切替点 | **`OP_WAIT` で park されたフレームからのみ resume できる**こと (K5a §設計の印方式: park 時に `parked_from_wait`、resume は印のあるフレームだけ)。正常経路は `ring3_switch_count` の増分で確認し、**印なしの resume を拒否して `ring3_resume_bad_frame_count` だけが増える**ことを負例で確認する (独立レビュー 2026-09-10)。起動 (`exec_start`)・終了・fault・kill に伴う CR3 の遷移は `ring3_transition_count` で別勘定 |
| G9 | 同時 ready の選択規則 | 複数アプリが同時に待ち解除条件 (入力 / Paint / Timer / Quit / 期限切れ) を満たしたとき、`OP_WAIT` の中の**決定的な規則**で 1 本を選ぶ (K5a §設計 D11)。プリエンプションは足さない |
| G10 | 音の排他 (ユーザー決裁 2026-09-11) | フォーカス窓が別アプリへ移ったら、それまでのアプリの音を止め、移った先のアプリが音を出していたなら**その状態を復元**する。元の窓へ戻れば元の音を復元。**同時に鳴らさない (排他)**。アプリ終了時はその音の状態だけを捨てる (他アプリの音は無事) |
| G8 | CUI 回帰 | `shell.bin` の入れ子 exec、v86、regress 6 本が従来どおり |

## この票に含めないもの

端末アプリ本体、console の差し込み口、入力統合 (K6 以降)。設定レジストリ (S0)。

## 設計 (K5a、2026-09-10)

> 段階 K5a の成果物。**コードは 1 行も変えていない** — `kernel/` `exec/` `kapi/`
> `include/memmap.h` `sdk/kapi.json` はすべて読取専用で扱った。版数 ([ABI3]) は決めない。
> 添付: `tools/tests/multiapp_model_host.c` / `test_multiapp_model.py` /
> [`multiapp_model_tdd.md`](../../../tools/tests/multiapp_model_tdd.md)。
> 行番号はすべて 2026-09-10 の `feat/gui` (`23147da`) 時点のもの。

### D0. 全体の形

```
   [WM top-level]  gshell の standalone_loop (lib.rs:183)  owner = 1
        |  exec_start(path)                    ^  exec_park()  (アプリの OP_WAIT の中から)
        v                                      |
   [アプリ ID k]  CPL=3 で走る  ------ gui_call(OP_WAIT) ------> WM が「他に起こす相手が居る」
        ^                                                        と判断したら park
        |  exec_resume(k, wait_ret)
   [WM top-level]  次に起こす 1 本を選ぶ (イベントの届いた窓の owner)
```

- **走っているアプリは常に 1 本**。残りは `OP_WAIT` の中で止まっている (= 保存された
  CPL=3 フレームとして AppSlot に横たわっている)。
- カーネルは「誰を次に起こすか」を決めない。決めるのは WM で、根拠はイベントの
  届いた窓の owner。**スケジューラは存在しない**。
- `OP_WAIT` 以外で PD が変わる経路を作らない (契約 T2a、受入 G7)。

**「`OP_WAIT` でだけ切り替える」の対象** (独立レビュー 2026-09-10 の指摘 2)。
この規則が縛るのは **生存アプリ間の実行切替** — すでに立ち上がっている 2 本の間で
「どちらが走るか」が入れ替わること、つまり `exec_park()` と `exec_resume()` の対だけ。
`exec_start()` の `iret` (新しいアプリを立てて初めて CR3 を載せる)、正常終了 /
fault / `exec_kill()` で master へ戻す CR3 遷移は、**生存アプリの集合そのものが
変わる瞬間**であって切替ではない。これらは `OP_WAIT` の外で起きるのが正しく、
G7 の失敗扱いにしてはならない。したがってカーネルのカウンタも 2 本に分ける
(D8 の C1 / C2):

| カウンタ | 何を数えるか | G7 での扱い |
|---|---|---|
| `ring3_switch_count` | `exec_resume()` が成功した回数 = **park してある生存アプリを起こした回数** | **これが G7 の対象の回数**。ただし「`OP_WAIT` の外で増えたら失敗」ではない — resume は必ず `OP_WAIT` の外 (WM top-level) で起きる。合否は下の `ring3_resume_bad_frame_count == 0` と併せて見る (独立レビュー 2026-09-10 の指摘) |
| `ring3_transition_count` | `exec_start()` の `iret` と、終了 / fault / `exec_kill()` の master 復帰 | 別勘定。増えても G7 の失敗ではない |
| `ring3_resume_bad_frame_count` | `exec_resume()` が「`OP_WAIT` 由来の印が無いフレーム」を起こそうとして弾いた回数 | **0 でなければ G7 は失敗** |
| `ring3_park_reject_count` | `exec_park()` を `OP_WAIT` 以外の op から呼ばれて弾いた回数 | 0 でなければ WM の規約違反 |

**G7 の検査は「いま `OP_WAIT` の呼出しスタックの中か」ではない**
(独立レビュー 2026-09-10 の指摘)。`exec_resume()` を呼ぶのは **WM の top-level** で、
その時点で `OP_WAIT` のカーネルスタックは既に解けている (park の longjmp で捨てた) —
「resume の瞬間に `OP_WAIT` の中に居る」ことは設計上あり得ない。検査すべきは

> **`OP_WAIT` で park されたフレームからのみ resume できること**

である。実装は「印」で行う:

1. `gui_call(op, arg)` はハンドラを呼ぶ間だけ現在の op を控える (`g_cur_gui_op`、
   `kernel/gui.c:32-38`)。
2. `exec_park()` は `g_cur_gui_op != GUI_OP_WAIT` なら `OS32_ERR_INVAL` を返して
   `ring3_park_reject_count` を上げる (park させない)。成立したときだけ、AppSlot に
   保存する CPL=3 フレームへ **「`OP_WAIT` 由来」の印** (`parked_from_wait = 1`) を立てる。
3. `exec_resume(id, ...)` は**印のあるフレームだけ**を対象にする。印が無ければ
   `OS32_ERR_STALE` を返し `ring3_resume_bad_frame_count` を上げる。resume したら印を消す。

こうすると G7 は 2 つのカウンタで判定できる:
`ring3_resume_bad_frame_count == 0` (印の無いフレームを起こしていない) かつ
`ring3_switch_count` が期待回数。WM の行儀を信じるのではなく**カーネルが弾く**形になる。
ホストモデルの R3 (`ma_gui_call` が `in_op_wait` を立て、`ma_park` がそれを見て
`MA_PARKED` に落とし、`ma_resume` が `MA_PARKED` のものしか起こさない) は
この形をそのまま写したもの。

この形をホストで動かせる状態機械にしたものが `tools/tests/multiapp_model_host.c` で、
以下の D3/D4/D6 の規則はそこで 60 個の検査として固定してある。

### D1. identity 前提の全列挙と、仮想≠物理で成り立つ形

いまは「アプリの仮想 = 物理」なので、同じ仮想 0x500000 に 4 本は置けない。
per-app 物理へ移すために触ることになる箇所は次の 14 か所ですべて。

| # | identity を前提にしている箇所 | 根拠 | 仮想≠物理での形 |
|---|---|---|---|
| I1 | ローダの読み込み先が `file_buf = (u8 *)load_base` で、そこへ `vfs_read` | `exec/exec.c:804-809` | AS を作り per-app 物理を 0x500000〜へ写した**後で CR3 をそのアプリ PD に載せてから読む**。VFS / kmalloc / ドライバ / ISR はすべて PDE 0 = 全 PD 共有 (`kernel/paging.h:138`) なので、この間もカーネルは普通に動く |
| I2 | ヘッダ分の前詰め `memmove` と bss の `kmemset` | `exec/exec.c:985-986` | 同上 (CR3 = アプリ PD の下で行う) |
| I3 | argv/argc/api をアプリのスタックへ**仮想番地で直書き** (`str_area` `argv_area` `new_esp` `u_esp`) | `exec/exec.c:1119-1182, 1313-1314` | 同上 |
| I4 | 物理ページの確保が固定帯 `[0x500000, mem_end)` (`exec_child_claim` → `pgalloc_mark_used`)、解放も同じ式 | `exec/exec.c:369-384, 976-978, 1060-1066` | 廃止。アプリごとに `pgalloc_alloc_n()` で**必要な枚数だけ** 3 本の連続領域 (本体+sbrk / exec_heap / ユーザスタック) を取り、AppSlot に `(phys, pages)` を 3 組記録して終了時に `pgalloc_free_n`。`EXEC_DYN_RESERVE` の穴 (`exec.c:115-126`) は「子が全部持っていくから空けておく」ための細工なので、**役目を終えて消える** |
| I5 | `paging_addrspace_map_user_range()` が **phys = virt 固定** (`pfn << PAGE_SHIFT` を 2 回渡す) | `kernel/paging.c:741-743` | 物理ベースを取る版を足す (D8 の P1)。1 ページ版 `paging_addrspace_map_user()` は既に phys 引数を持つ (`kernel/paging.h:221-222`)。共有ライブラリの .data がこの機構で既に動いている (`kernel/shlib.c:240-245`) |
| I6 | `paging_addrspace_create_n()` がアプリ PT を **master の identity PTE で初期化**する | `kernel/paging.c:634-642` | 作った直後にアプリ帯 `[0x400000, 帯上端)` の PTE を**全部 0 (非 present) に落としてから** per-app 物理で張り直す。**落とし忘れると物理 0x5xxxxx が素通しで見える** (= 他アプリのページや pgalloc の作業域が CPL=3 から読める)。ここが本設計で最も静かに壊れる箇所 |
| I7 | `exec_heap` の base が仮想番地で、実体は静的 `KHeap` 1 個 | `exec/exec_heap.c:15`、`kernel/kmalloc.h:16-21` | 番地はアプリ仮想のままでよい。`mem_alloc` は wrap の中 = **そのアプリの CR3 の下**でしか呼ばれない (`exec.c:672` の `kapi_invoke` は CR3 = アプリ PD で走る) ので、そのまま解決する。KHeap の 4 変数を AppSlot に持ち、切替のたびに save/restore する — 既存の `exec_heap_save_state()` / `exec_heap_restore_state()` (`exec_heap.c:51-71`) がそのまま使える |
| I8 | `exec_exit` と longjmp 復帰が `exec_heap_reset()` と `paging_set_page(guard_a/guard_b, ..., PAGE_RW)` を呼ぶ | `exec/exec.c:511-517, 1044-1053`、`exec.c:452-461` | どちらも **master CR3 に戻した後**に走る (`kapi_sys_exit` は `exec.c:575` で先に master へ戻す) ので、per-app では**別物理を叩く**。`exec_heap_reset` はページごと返すので不要、guard の present 戻しもアプリ PT ごと破棄するので不要。**両方削る** |
| I9 | sbrk 上限がトランポリンページの単一グローバル `sbrk_heap_limit` | `exec/exec.c:1297-1298`、`sdk/crt/syscalls.c:140-144` | トランポリンページはカーネル .bss (PDE 0、全 PD 共有 RO+USER、`exec.c:109`) なので per-app にはできない。しかし**走るのは常に 1 本**なので、`exec_start` / `exec_resume` の中でそのアプリの `guard_a` を書き込めばよい (いま launch 時に 1 回書いているのを切替ごとに書くだけ)。アプリ側の `heap_ptr` は自分の .bss にあるので (`syscalls.c:128`) 既に per-app |
| I10 | `ring3_ptr_ok` の許可範囲が `g_ring3_band_top` 由来 (`RING3_HEAP_TOP` / `RING3_STACK_BOTTOM`) | `exec/exec.c:176-211, 339-354` | 帯上端と枚数を AppSlot に持ち、切替時に `ring3_band_set()` 相当で差し替える。`g_ring3_as` `g_ring3_active` も同じ扱い (`exec.c:213-214`) |
| I11 | shlib の .data 原本コピーが**物理番地直書き** (`kmemcpy((void *)phys, (const void *)g_data_master, ...)`) | `kernel/shlib.c:236-238` | `g_data_master` は共有ライブラリ帯の末尾 = **アプリ固有 PDE の中** (`include/memmap.h:298-299`) なので、アプリ CR3 の下では別物を指す。**attach は master CR3 の下で済ませてから CR3 を載せ替える** — 順序だけが要件で、`shlib.c` 自体は無改造。`SHLIB_MAX_ATTACH` は既に 4 (`shlib.c:54`) で 4 本ぶん足りる |
| I12 | `sdk/link/app.ld` の `. = 0x500000;` | `sdk/link/app.ld:8` | **変更不要**。全アプリが同じ仮想番地に載るのが目的そのもの。`mkos32x` の `load_addr` 照合 (`exec.c:854-872`) もそのまま |
| I13 | `include/memmap.h` の `MEM_APP_BAND_*` / `MEM_EXEC_*` | `include/memmap.h:271-278, 328-330` | **変更不要**。1 本のときの仮想レイアウトを 1 バイトも動かさない (回帰ゼロ) |
| I14 | `exec_ctx_stack[MAX_EXEC_NEST]` が「段のスタック」で、長さ 4 | `exec/exec.h:26`、`exec/exec.c:153` | 「段のスタック」から「**ID で引く表**」へ。非シェル ID は 2〜5 の 4 本なので、シェル (ID 1) と合わせて**長さ 5 以上が要る** — 今の 4 では ID 5 が入らない |

**結論**: 仮想レイアウト (`app.ld` / `memmap.h` / `RING3_*` の式) は一切動かさない。
動くのは「物理をどこから取るか」(I4) と「アプリ PT に何を書くか」(I5/I6)、
そして「カーネル側の *現在のアプリ* を表す変数を切替のたびに入れ替える」(I7/I9/I10) の 3 点。

#### 起動時の順序 (ここだけは順番が意味を持つ)

1. AppSlot を確保し ID を決める (D3)
2. ヘッダを読むために**まず master CR3 のまま**先頭 1 ページぶんを読む
   — `OS32Header` の `text_size` / `bss_size` / `heap_size` が無いと必要枚数が決まらない
3. 必要枚数を算出 → `pgalloc_alloc_n()` × 3 → 足りなければ `EXEC_ERR_NOMEM` で**拒否**
4. `paging_addrspace_create_n()` → **アプリ帯の PTE を全部落とす** (I6) → per-app 物理で張り直す
   (本体+sbrk / exec_heap / スタック)。guard は張らない (非 present のまま = ガードになる)
5. **master CR3 のまま** `shlib_addrspace_attach()` (I11)
6. VRAM / SHM / フォント / Unicode 表 / GFX バックバッファ / トランポリンを USER で張る
   (`exec.c:1212-1272` のまま。すべて PDE 0・PDE 3+ の共有帯なので per-app 化と無関係)
7. `paging_load_cr3(as.pd_phys)` → 本体を読み込み・bss クリア・argv 構築 (I1/I2/I3)
8. トランポリンの `sbrk_heap_limit` にこのアプリの `guard_a` を書く (I9)
9. `res_owner_set(id)` → TSS.ESP0 = 現在のカーネル ESP → `iret`

手順 2 の「先頭 1 ページだけ先に読む」は現行に無い動き
(`exec.c:809` はいきなり全部読む)。**現行のように全部読んでから枚数を決めることは
できない** — 読む先の物理がまだ無いため。K5b の実装で最初に効いてくる差分。

### D2. 切替機構 — (a) と (b) の比較、および推奨

| 観点 | (a) アプリごとのカーネルスタック (4 × 16KB) | (b) syscall フレームだけ保存 (単一カーネルスタック) |
|---|---|---|
| 追加で要る RAM | **64KB 固定** (8MB 構成では小型アプリ 0.1 本分) | **1 アプリ 52B** (pushad 8 語 + iret 5 語)。4 本で 208B |
| 切替の実体 | `ESP` を差し替え + `CR3` + `TSS.ESP0`。切替先の C の呼び出しフレームの続きへ return する | 保存フレームを**いまのカーネルスタックへ書き戻して** `popad; iretd` — `int80_stub:65-81` の末尾がそのまま使える |
| `OP_WAIT` の扱い | WM のハンドラの中で止まったまま待てる (戻ってこられる) | WM のハンドラから**抜ける** (longjmp)。`OP_WAIT` の戻り値は resume 側が決めて保存フレームの EAX に書く |
| WM に課す規約 | 特になし | **`exec_park()` を呼んでよい地点を op_wait のループ先頭 1 点に固定**する必要がある (途中で抜けると WM 状態が中途半端になる)。契約 S8 (X4 の bounded-work) と同じ性質の規約 |
| ISR ネストの見積 | カーネルスタックが 4 本に割れるので、`include/memmap.h:106-108` の「リング 3 を入れたらスタック量を measure し直せ」を**スタックごとに**やり直すことになる | 従来どおり 1 本 (16KB, `MEM_KSTACK_BASE`)。見積の前提が動かない |
| 壊れたときの現れ方 | ESP を間違えると別アプリのカーネルスタックを静かに踏む | フレームを間違えれば即 #GP/#PF → `ring3_fault_kill` でそのアプリだけ死ぬ |
| 既存コードへの改造 | `int80_stub` は無改造だが、V86 (`v86_entry.asm` が `TSS.ESP0` をオフセット直書き、`kernel/tss.c:12-16`) と CPL=0 の入れ子 exec が「どのスタックの上か」を意識し始める | `int80_stub` の末尾を `ring3_resume(frame *)` として切り出して再利用。V86 も CPL=0 入れ子も従来どおり |
| プリエンプションへの足場 | **なる** | ならない |

**推奨: (b)**。決め手は最後の 2 行。本票の「同時」の定義はプリエンプションの足場も
作らないことなので、(a) の唯一の長所 (割込み文脈からでも切替えられる) は要らない。
そのうえで (b) は追加 RAM が 3 桁少なく、V86 と CPL=0 入れ子と ISR ネストの前提を
1 つも動かさない。

#### 切替を呼ぶ場所

| 遷移 | 呼ぶ主体 | 実体 |
|---|---|---|
| park (アプリ → WM top-level) | **gshell の `op_wait`** (`userland/gshell/src/handler.rs:368-390` のループ先頭) が新 KAPI `exec_park()` を呼ぶ | 現フレーム (`ring3_syscall_dispatch` が受け取った `frame`、`exec.c:606`) を AppSlot へ写し、`ring3_in_syscall = 0`、`res_owner_set(1)`、CR3 を master へ、`exec_longjmp` で `exec_start` / `exec_resume` の setjmp 点へ |
| resume (WM top-level → アプリ) | **gshell の top-level** (`lib.rs:183-202` の周期) が `exec_resume(id, wait_ret)` を呼ぶ | setjmp → AppSlot の per-app 変数を復元 (I7/I9/I10) → `cli` → `TSS.ESP0 = 現 ESP` → `CR3 = as.pd_phys` → 保存フレームを積んで `popad; iretd` |

**カーネルの `gui_call` 出口では切り替えない。** カーネルは op の意味を知らない
(`kernel/gui.c:37-38`) ので、「他に起こす相手が居るか」を判断できるのは WM だけ。
判断を WM に置き、機構だけをカーネルに置く。

#### IF (割込み許可) の扱い

- `int80_stub` は入口で `sti` (`kernel/ring3_entry.asm:50`)、出口で `cli`
  (`同:63`) してから `popad; iretd`。**この対は動かさない**。
- `exec_park()` はディスパッチャの奥 (IF=1) から呼ばれ、`exec_longjmp` は EFLAGS を
  復元しない。よって longjmp 先も IF=1 のまま。ただし**fault 経由の復帰は例外ゲートで
  IF=0 のまま来る**ので、`exec_start` / `exec_resume` の setjmp 復帰点は
  `exec_run` と同じく無条件に `_enable()` する (`exec/exec.c:1035-1039` と同じ理由・同じ形)。
- `exec_resume` の CPL=3 復帰は `cli` → `TSS.ESP0` → `CR3` → `iretd` を**割込み禁止で
  一続きに**行う (`exec.c:1331-1353` の既存ブロックと同じ作法)。`iretd` が保存済み
  EFLAGS (IF=1) を復元するので、アプリ側の IF は変わらない。
- **切替中の IRQ**: `cli` の前は普通に入る。`cli` の後は `iretd` までの間に 1 つも
  入らないので、`TSS.ESP0` を書き替えてから CPL=3 に降りるまでの窓は存在しない。
  IRQ ハンドラはすべて PDE 0 (全 PD 共有) にあるので、CR3 がどのアプリ PD でも動く
  — これは今 CPL=3 アプリが走っている間に既に成立している事実。
- **`hlt` はカーネル側に残す**。park したアプリは `sys_halt` を呼んでいない
  (WM のループを抜けた) ので、待つのは WM top-level の周期 (`lib.rs:198`) だけ。
  CPL=3 の KAPI 呼び出しが IF=1 で走る前提 (`ring3_entry.asm:44-49`、既知の落とし穴
  §4-19) は変わらない。

#### (b) の弱点と、それを塞ぐ規約

`exec_park()` は WM のハンドラの途中から longjmp するので、その時点で WM が
書きかけの状態を持っていると宙に浮く。塞ぎ方は**呼べる地点を 1 点に固定する**こと:

> **park 規約**: `exec_park()` を呼んでよいのは `op_wait` のループ先頭、
> `wm_cycle()` が 1 周を終えた直後・`ring::pending` を読む前だけ。
> それ以外の op / X1 / X2 / X4 からは呼ばない。

これは契約 S8 (X4 の bounded-work) と同じ性質の規約で、K5b で契約へ書き足す。

### D3. 所有者 ID

- **意味を変える**: `res_owner_set/get` (`fs/fd_redirect.c:19-22`) の値を
  「exec のネスト段」から「**アプリ ID**」にする。1 = シェル帯 (CUI シェル / gshell、
  `kernel/gui.h:26` の `GUI_SHELL_OWNER` と同じ値)、2〜5 = アプリ。
- **配り方**: 空き ID は**必ず小さい方から**。これで CUI の入れ子 `exec_run` は
  シェルから 2, 3, 4 と並び、**段 = ID** という従来の見え方がそのまま残る
  (模型ケース 8a)。GUI アプリと CUI の入れ子は**同じ 1 つの池**を使うので、
  GUI 4 本のときは入れ子の 5 本目も `ERR_FULL` (模型ケース 8h)。
- **誰が set するか**: `exec_start` が新 ID へ、`exec_park` が 1 へ、
  `exec_resume` が対象 ID へ、`exec_exit` / `ring3_fault_kill` / `exec_kill` が
  1 (または CUI の親 ID) へ。set する箇所は現行の 3 か所
  (`exec.c:448, 557, 1097`) の置き換えで、増えるのは park/resume の 2 つだけ。
- **配列長**: `exec_ctx_stack[MAX_EXEC_NEST=4]` は ID 5 を持てない。
  ID で引く表にして**長さ 5 以上**にする (I14)。

#### `*_owned(id)` の回収 — 現状の全列挙と、アプリ単位で閉じるか

`exec_exit` の回収の並び (`exec/exec.c:531-553`) をそのまま棚卸しした。

| 回収 | 現状 | アプリ単位で閉じるか |
|---|---|---|
| `fd_redirect_reset_owned(owner)` (`exec.c:531`) | `fs/fd_redirect.c:129-138`。owner 一致のものだけ戻す | **閉じる**。ただし表は FD 0/1/2 の **3 本しかない** (`fd_redirect.c:16`) ので、2 本のアプリが同時に stdout をリダイレクトすることはできない。GUI アプリはリダイレクトしないので v1.3 では実害なし — **限界として明記** |
| `vfs_close_owned(owner)` (`exec.c:536`) | `fs/vfs_fd.c:222-232`。owner 一致・protect でない FD だけ閉じる。タグは open 時 (`vfs_fd.c:139`) | **閉じる**。無改造 |
| `pipe_free_owned(owner)` (`exec.c:539`) | `fs/pipe_buffer.c:49-57`。タグは alloc 時 (`同:42`) | **閉じる**。無改造 |
| `shm_cleanup_all()` (`exec.c:542`) | `kernel/shm.c:195-202`。**所有者を見ずに全ブロックを解放する** (GUI 予約ブロックだけ除外) | **閉じない**。アプリ A の終了がアプリ B の `shm_alloc` を巻き上げる。**`shm_free_owned(id)` が要る** (ブロックに owner を持たせる。1 バイト × 16) |
| `snd_cleanup()` (`exec.c:545`) | `kernel/snd_engine.c:737-745`。`bgm_persist` でなければ BGM を止める。**所有者の概念が無い** | **閉じない**。アプリ A の終了がアプリ B の BGM を止める。決裁事項 (D9-4) |
| `db_cleanup_all()` (`exec.c:548`) | `kapi/kapi_db.c:472-479`。**所有者を見ない**。ただし `db_cleanup_owned(int owner)` が **既に隣にある** (`同:462-470`、タグは `同:215`) | **呼び先を替えるだけで閉じる**。`db_cleanup_owned(id)` へ |
| `gui_owner_exit(owner)` (`exec.c:553`) | `kernel/gui.c:71-91` → WM の `reclaim_owner` (`wm.rs:700-737`) が窓・タイマ・スロットを owner で回収 | **閉じる**。無改造。タイマは WM 側の表 (`wm.rs:721-728`) にあり、カーネルにタイマ所有権は無い |

**要る変更は 3 つだけ**: `shm_free_owned` の新設、`db_cleanup_all` → `db_cleanup_owned`、
`snd_cleanup` の扱い (D9-4)。残り 4 つは既にアプリ単位で閉じている。

### D4. 起動・切替・終了の経路

#### 起動 (塞がない)

`exec_run` は**そのまま残す** (CUI の入れ子はこれを使い続ける)。並べて
`exec_start(cmdline)` を切る。違いは**どこで setjmp するか**の 1 点だけ:

| | `exec_run` (現行、塞ぐ) | `exec_start` (新、塞がない) |
|---|---|---|
| setjmp を置く場所 | 自分のフレーム (`exec.c:1032`)。longjmp するのは**子の終了時** (`exec_exit` → `exec.c:559`) | 自分のフレーム。longjmp するのは**子の最初の `exec_park()`**、または終了時 |
| 呼び出し元へ戻る条件 | 子が終わったとき | 子が最初に `OP_WAIT` に入って WM が park を決めたとき、または子が終わったとき |
| 戻り値 | `exec_exit_status` | park したなら **app_id (2〜5)**、park より前に終わったなら **0**、失敗なら負 |

`ExecContext.jmpbuf` (`exec.c:137`) の**役割は変わらない** — 「この段/この ID の
呼び出し元へ帰る点」のまま。変わるのは (i) 置き場所が段のスタックから ID の表へ
(I14)、(ii) longjmp を打つ契機に park が 1 つ増える、の 2 つ。
`exec_launch_abort()` (`exec.c:432-492`) の巻き戻し 5 段も、対象が
「段」から「ID」に変わるだけで構成は同じ (ただし I8 のとおり (2) ガード戻しと
(3) ヒープリセットは不要になり、(4) が `pgalloc_free_n` × 3 になる)。

#### 4 つの畳み方 — どれも「その ID だけ畳んで WM へ戻る」

| 経路 | 入口 | 畳む手順 | WM への戻り |
|---|---|---|---|
| 正常終了 | `kapi_sys_exit` (`exec.c:568-584`) | master CR3 → `shlib_addrspace_detach` → `paging_addrspace_destroy` → **その ID の 3 本の物理を `pgalloc_free_n`** → D3 の回収 7 種を `id` で → AppSlot を空に | `exec_longjmp(AppSlot[id].jmpbuf)` → `exec_start`/`exec_resume` の復帰点 → gshell top-level |
| fault | `ring3_fault_kill` (`exec.c:701-714`)。#PF/#GP のフレームが CS.RPL=3、または範囲外 slot (`exec.c:628-630`)、またはポインタ早期検証 (`exec.c:656-658`) | 同上 + `fault_kill_count++` | 同上 |
| CTRL+STOP | `ring3_abort_request` (`exec.c:263-268`) を IRQ1 が呼び (`drivers/kbd.c:234-237`)、`ring3_abort_check` が syscall 入口 (`exec.c:619`) / 出口 (`exec.c:686`) / IRQ1 スタブ (`kernel/isr_stub.asm:507-519`) で畳む | 同上 + `ring3_abort_count++` | 同上 |
| 起動失敗 | `exec_start` の中 (ファイル無し・ヘッダ不正・`ERR_NOMEM`・`ERR_FULL`) | `exec_launch_abort` 相当。**CR3 は載せ替えない / 回収は回さない / owner は 1 のまま** (模型ケース 10) | 普通に `exec_start` が負値を返す (longjmp しない) |

**CTRL+STOP の宛先**: 要求 `g_ring3_abort_req` (`exec.c:251`) は
「**いま走っているアプリ**」宛にしか立たない — IRQ1 の時点でカーネルが知っているのは
それだけだから (模型ケース 9a/9b)。止めてあるアプリには届かない。したがって
**止めてあるアプリを畳む口が別に要る**: `exec_kill(app_id)` (D8)。これが無いと、
resume されないまま固まったアプリを永久に畳めない。フォーカスと CTRL+STOP の
対応づけ (T6「フォーカス窓のアプリ宛」) は WM の仕事 — WM がフォーカス窓の owner を
見て、それが走っている本人なら何もしない (カーネルの要求がそのまま効く)、
別の ID なら `exec_kill(その ID)` を呼ぶ。

**`gui_owner_exit(id)` の呼び位置は変えない** (`exec.c:553`)。畳む 3 経路すべてが
`exec_exit` を通るので、WM は 1 か所で回収できる。

### D5. メモリの勘定

#### 1 本あたりの物理ページ (per-app、4KB 単位)

```
  ceil((text_size + bss_size)/4K)      本体 + data + bss
+ MEM_EXEC_SBRK_MIN / 4K = 64          sbrk 最低分 (256KB, memmap.h:328)
+ 0                                    guard_a (非 present)
+ ceil(heap_size / 4K)                 exec_heap (最低 16 = 64KB, memmap.h:329)
+ 0                                    guard_b (非 present)
+ MEM_EXEC_STACK_SIZE / 4K = 64        ユーザスタック (256KB, memmap.h:330)
+ 1                                    PD
+ band_pdes (1 または 2)               アプリ PT
+ shlib の .data/.bss 複製ページ       (shlib.c:230。libos32gui.shlib の実測が要る)
```

**仮想**レイアウトは 1 本のときと同じ。物理は上の合計だけで、
現行のように `[0x500000, mem_end)` を丸ごと押さえることはしない (I4)。

#### 配れる物理の総量

`pgalloc` の管理域は `[PGALLOC_BASE = 0x400000, sys_usable_mem_end())`
(`kernel/pgalloc.h:22`、`kernel/sys.c:155-159`) で、共有ライブラリ帯
`0x400000-0x4FFFFF` は `shlib_init` が予約済み。よってアプリに配れるのは
`[0x500000, sys_usable_mem_end())`。

| 構成 | `sys_usable_mem_end()` | アプリに配れる物理 |
|---|---|---|
| 8MB (CUI) | 0x800000 | 0x300000 = **3.00MB = 768 ページ** |
| 9MB | 0x900000 | 0x400000 = **4.00MB = 1024 ページ** |
| 15MB (PEGC) | 0xF00000 − 予約 0x4B000 (`sys_reserve_top(307200)`) = 0xEB5000 | 0x9B5000 = **9.71MB = 2485 ページ** |

**per-app 物理にすると 15MB 構成で 0xC00000 より上の RAM が初めて使える。**
仮想のアプリ帯は `MEM_APP_BAND_MAX_TOP = 0xC00000` (`memmap.h:276-278`) で頭打ちだが、
**物理**は master が 32MB まで identity で持っている
(`PAGING_BOOT_MAP_SIZE`、`kernel/paging.h:45-52`) ので、0xC00000〜0xEB5000 の
約 3MB をアプリのページとして配れる。identity のままでは絶対に届かない領域。

#### gshell + アプリ n 本の見積

前提 (**どれも未実測。K5b で測り直すこと**):
本体 (text+bss) 128KB = 32 ページ、shlib の .data 複製 8 ページ、`band_pdes` = 1。
`gshell` 自身はシェル帯 0x300000-0x3FFFFF に常駐するので、この表の外
(`MEMORY_BUDGET.md` の実測で text 103,206B / data 49,796B / bss 17,924B)。

| heap_size | 1 本の物理 | 8MB (768p) | 9MB (1024p) | 15MB (2485p) |
|---|---:|---|---|---|
| **既定 (`heap_size = 0` → 空きの折半)** = 約 1.18MB / 303p | 471p = 1.84MB | **1 本** (2 本目は `ERR_NOMEM`) | **2 本** | **4 本** (1884p、残 601p) |
| **256KB 明示** / 64p | 234p = 936KB | **3 本** (702p、残 66p) | **4 本** (936p、残 88p) | **4 本** (余裕) |
| **64KB 明示** (`MEM_EXEC_HEAP_MIN`) / 16p | 186p = 744KB | **4 本** (744p、残 24p — 実質ぎりぎり) | **4 本** (残 280p) | **4 本** (余裕) |

**この表からの帰結 (K5b の作業に直結)**: いまの `build/app.conf` は GUI アプリの
`heap_size` を全部 **0 (= 空きの折半)** にしている (`gui_demo` `gui_bench`
`v12_api_test` `filer` `lease_test`、`build/app.conf` の GUI v1.1 節)。
このままだと 8MB では 1 本しか立たない。**GUI アプリは `app.conf` に実際に使う量を
明示する**のが 4 本を成立させる条件。折半の既定そのものを変えると CUI の 1 本実行が
動くので、**既定式は変えず app.conf 側で明示する**ことを推奨 (D9-3)。

入らないときは**拒否**する。切り詰めない、スワップしない
(2026-09-10 ユーザー方針、`exec/exec.c:929-941` が既にこの形)。
拒否は `ERR_NOMEM` (物理が足りない) と `ERR_FULL` (ID / SHM スロットが尽きた) を
区別する — 前者は「もっと小さいアプリなら入る」、後者は「何をしても 5 本目は無理」。

**未確認**: `pgalloc` のメタデータと workspace が上の「配れる物理」から何ページ
引くか (`kernel/sys.c:72-106` のモデル経路)、`libos32gui.shlib` の `.data`/`.bss`
ページ数、実際の GUI アプリの text+bss。この worktree にはビルド生成物が無く、
配備もエミュレータも禁止範囲なので測っていない。

### D6. SHM スロットと所有者 ID

- 割当は**カーネルではなく WM**。アプリが `gui_call(OP_INIT)` を呼び、
  `alloc_slot(owner)` (`userland/gshell/src/wm.rs:641-656`) が空きスロットを
  小さい順に配る。満杯は `OS32_ERR_FULL` (`handler.rs:188-191`)。
  **この実装は既に 4 スロット対応で、変更不要**。
- 対応は **owner (= アプリ ID) ↔ スロット番号** の 1 対 1。
  `slot_of_owner` (`wm.rs:630-639`) が引き、`reclaim_owner` (`wm.rs:729-736`) が
  終了時に空ける。park / resume ではスロットは動かない (模型ケース 6c)。
- 番地は `MEM_SHM_GUI_BASE + slot × GUI_SLOT_SIZE`
  (`include/memmap.h:200-203`、ブロック 12〜15 を `shm.c:77-81` が予約済み)。
  カーネルはスロット番号を知らないままでよい。
- **5 本目**: 実際には ID の池 (4 本) が先に尽きるので `exec_start` が `ERR_FULL` を
  返し、`OP_INIT` まで到達しない。スロット側の `ERR_FULL` は二重の安全網として残す。
- **可視性**: SHM は全 PD 共有 + USER なので、アプリは他アプリのスロットを読める。
  契約 T2a が v1 の割り切りとして明記した点で、**本設計では変えない**
  (per-app 物理を SHM にも広げる話は T2a の注記どおり後日)。

### D7. 変えないもの と、壊れない根拠

| 変えないもの | 根拠 |
|---|---|
| `shell.bin` の入れ子 `exec_run` | `exec_run` は残す。`exec_start` は別関数。ID の配り方が「小さい方から」なので段 = ID が保たれ (模型ケース 8a-8g)、`exec_exit` は親が生きていればその段へ戻る (同 8d/8e)。**仮想レイアウトは 1 バイトも動かない** (I12/I13) ので、子から見た番地・ヒープ量・スタック量は現行と同一 |
| v86 | V86 は `TSS.ESP0` をオフセット直書きで更新する (`kernel/tss.c:12-16`)。(b) を選ぶとカーネルスタックは 1 本のままなので、この前提が動かない。V86 バッキング RAM の 640KB 連続確保 (`exec.c:118-125`) は、アプリが `[0x500000, mem_end)` を丸ごと押さえなくなる (I4) ので**むしろ取りやすくなる** |
| `sys_switch_shell` (T9) | `kernel/gui.c:108-119`。owner 1 からのみ。ID の意味が「段」から「アプリ ID」に変わってもシェル帯は常に 1 なので判定はそのまま。切替は「全アプリが畳まれた後に gshell 自身が exit する」ときにしか起きない (`lib.rs:296` → `switch_cui`) |
| `mkos32x --cpl0` の例外扱い | `OS32X_FLAG_FORCE_CPL0` の判定 (`exec.c:887`) は `want_ring3` を落とすだけ。CPL=0 で走るプログラムは AS を作らず identity のまま (`exec.c:1355-1364`) — per-app 物理は `want_ring3` の側にしか入れない |
| `gui_call` / `gui_register` / `gui_owner_exit` の署名 | `kernel/gui.h:29-37`。owner の**値の意味**が変わるだけで型も呼び位置も同じ |
| SHM の GUI 予約 (ブロック 12〜15) | `kernel/shm.c:25-33, 77-81`、`memmap.h:200-203` |
| `int80_stub` の `sti`/`cli` 対 | `kernel/ring3_entry.asm:50, 63`。(b) では末尾の `popad; iretd` を resume でも使うだけで、対は動かさない (D2) |

### D8. KAPI 追加候補 (末尾追記のみ [ABI2]、版数は PM が決める [ABI3])

`sdk/kapi.json` は**編集していない**。現行 180 スロット・version 42。
以下は候補の一覧で、確定は PM。

| 名前 | 引数 | 戻り値 | エラー |
|---|---|---|---|
| `exec_start` | `const char *cmdline` | `i32`: **>0** = app_id (2〜5) で最初の `OP_WAIT` まで進んで park した / **0** = park より前に終了した (回収済み、`gui_owner_exit` 配送済み) / **<0** = 起動しなかった | `OS32_ERR_INVAL` (呼び出し元が owner 1 でない — 契約 S2)、`OS32_ERR_FULL` (ID の池が尽きた)、`EXEC_ERR_NOMEM` (物理が足りない)、`EXEC_ERR_NOT_FOUND`、`EXEC_ERR_INVALID` (OS32X ヘッダ / load_addr 不一致) |
| `exec_resume` | `i32 app_id, i32 wait_ret` | `i32`: **app_id** = また park した / **0** = 終了した / **<0** | `OS32_ERR_INVAL` (owner 1 でない / 未知の ID)、`OS32_ERR_STALE` (畳まれた後の ID)、`OS32_ERR_FULL` は返さない |
| `exec_park` | `void` | **戻らない** (longjmp)。呼べない文脈では `OS32_ERR_INVAL` を返して普通に戻る | `OS32_ERR_INVAL` (owner 1 でない / いま走っているアプリが居ない) |
| `exec_kill` | `i32 app_id` | `i32`: 0 = 畳んだ / <0 | `OS32_ERR_INVAL` (owner 1 でない / 未知の ID)、`OS32_ERR_STALE` (走っている本人 — CTRL+STOP 経路を使う) |
| `exec_app_state` *(任意)* | `i32 app_id` | `i32`: 0 = 空き / 1 = 走っている / 2 = park 中 | `OS32_ERR_INVAL` |
| `exec_counters` *(任意)* | `void *out` (`{u32 switch_count; u32 transition_count; u32 park_reject_count; u32 live_apps;}`) | `i32` 0 / `OS32_ERR_INVAL` | 受入 G7 をゲストから読むための口。カーネルシンボルを `emu_read_mem` で直接読めば足りるので、KAPI にするかは PM 判断 |

`exec_start` / `exec_resume` / `exec_kill` は **owner 1 (シェル帯) からのみ**。
判定は `gui_register` と同じ形 (`kernel/gui.c:47-49`)。
`exec_park` も owner 判定は同じ (呼ぶのは WM のハンドラで、その時点の owner は
アプリ ID なので、**「走っているアプリが居ること」を条件にする**点だけ違う)。

#### KAPI ではない追加 (カーネル内部)

| P# | 何 | 場所 |
|---|---|---|
| P1 | `paging_addrspace_map_user_range_phys(as, vstart, vend, pstart, flags)` — `[vstart, vend)` を `pstart` からの連続物理へ USER で張る。既存の identity 版 (`paging.c:752-756`) は残す | `kernel/paging.{h,c}` |
| P2 | `paging_addrspace_clear_app_band(as)` — アプリ固有 PDE の PTE を全部 0 にする (I6)。これを忘れると identity が素通しで残る | `kernel/paging.{h,c}` |
| P3 | `shm_free_owned(int owner)` + `ShmBlock.owner` (D3) | `kernel/shm.{h,c}` |
| P4 | `ring3_resume(u32 *frame)` — `int80_stub` 末尾 (`ring3_entry.asm:65-81`) を関数として切り出したもの | `kernel/ring3_entry.asm` |
| P5 | `exec_exit` の `db_cleanup_all()` を `db_cleanup_owned(id)` へ (呼び先の差し替えのみ、実体は `kapi_db.c:462` に既存) | `exec/exec.c:548` |
| C1 | `volatile u32 ring3_switch_count` — `exec_resume()` の成功回数 (= 生存アプリ間の実行切替)。**受入 G7 が数えるのはこれ** | `exec/exec.c` (`fault_kill_count` / `ring3_abort_count` と同じくカーネルシンボルとして公開。PM の V4 検証が `emu_read_mem` で読む) |
| C2 | `volatile u32 ring3_transition_count` — `exec_start()` の `iret` と、終了 / fault / `exec_kill()` の master 復帰。G7 とは別勘定 | 同上 |
| C3 | `volatile u32 ring3_park_reject_count` — `exec_park()` を `OP_WAIT` 以外の op から呼ばれて弾いた回数。0 でなければ WM の規約違反 | 同上 |
| C4 | `g_cur_gui_op` — `gui_call` がハンドラを呼ぶ間だけ現在の op を控える (`kernel/gui.c:32-38`)。C3 と、park 時に立てる印の判定に使う | `kernel/gui.c` |
| C5 | `AppSlot.parked_from_wait` — park したフレームに立てる「`OP_WAIT` 由来」の印。`exec_resume()` はこの印のあるフレームだけを起こす | `exec/exec.c` |
| C6 | `volatile u32 ring3_resume_bad_frame_count` — 印の無いフレームを起こそうとして弾いた回数。**G7 はこれが 0 であることを見る** | 同上 |

P1/P2 は `paging_app_band_selftest()` (`kernel/paging.h:258-267` / `kernel/paging.c:893-` の系列) に
検査項目として足せる — ハードウェアに依存しないので `make check` のホスト試験
(`test_app_band_pde.py`) でも見られる。

### D9. 決裁が要る分岐 (PM / ユーザー)

| # | 分岐 | 推奨 | 理由 | 代案 |
|---|---|---|---|---|
| 1 | 切替機構 (a) 専用カーネルスタック / (b) syscall フレーム保存 | **(b)** | 追加 RAM が 64KB → 208B。ISR ネストと V86 の `TSS.ESP0` 前提が動かない。プリエンプションの足場を作らないという票の決めと一致 | (a)。将来プリエンプションを入れるなら (a) が土台になるが、v2.0 で改めて設計する方が安い |
| 2 | 所有者 ID の池を GUI と CUI で共有するか | **共有する (1 池 4 本)** | 「同時に生きているアプリは 4 本まで」が 1 つの規則で言い切れ、回収も 1 つの ID 空間で閉じる。CUI の段 = ID の互換も保てる | GUI 用 4 本と CUI ネスト用を別枠にする。4 + 4 = 8 本ぶんの資源表が要り、8MB では絶対に入らない |
| 3 | `heap_size = 0` (空きの折半) の既定を GUI モードで変えるか | **変えない。`app.conf` で明示する** | 既定式を変えると CUI の 1 本実行のヒープ量が動く (回帰)。GUI アプリ 5 本の `app.conf` 行を直す方が影響が閉じている | GUI モードのときだけ上限 (例 256KB) をかける。挙動が 2 系統になり、どちらで走ったか分からない不具合が出やすい |
| 4 | `snd_cleanup()` (`exec.c:545`) の扱い | **サウンドに owner を持たせて `snd_cleanup_owned(id)` にする** | 現状は所有者を見ないので、アプリ A の終了がアプリ B の BGM を止める (4 本同時では実際に起きる) | (i) 現状維持 + 限界として明記、(ii) GUI モードでは `snd_cleanup` を呼ばない。どちらも「音が勝手に止まる/止まらない」が残る |
| 5 | 塞がない起動の名前と戻り値 | **`exec_start(cmdline) → app_id / 0 / 負`** | `exec_run` と対で読める。「0 = もう終わっている」を返り値で表せば、WM は `gui_owner_exit` と突き合わせずに済む | `exec_spawn` (票の初版の語。PM が撤回済み)、あるいは `exec_run` に flags 引数を足す (KAPI の既存スロットは変えられない [ABI2] ので新スロットになる点は同じ) |
| 6 | `exec_kill(app_id)` を切るか | **切る** | CTRL+STOP は走っているアプリ宛にしか立たない (`exec.c:265`)。止めてあるアプリを畳む口が無いと、resume されないアプリを永久に畳めない (受入 G4 が「フォーカス窓のアプリ」を対象にする以上、フォーカスが別 ID にあるときに要る) | 切らない。代わりに「畳みたい ID を必ず一度 resume してから CTRL+STOP を効かせる」— 固まったアプリには resume が返ってこないので破綻する |
| 7 | park するのは「他に起こす相手が居るとき」だけか、`OP_WAIT` のたび毎回か | **他に起こす相手が居るときだけ** | アプリが 1 本のときの挙動が現行とビット単位で同じになる (回帰ゼロ)。毎回 park すると 1 本でも `OP_WAIT` ごとに CR3 が 2 回動く | 毎回 park。Win3.1 の GetMessage に厳密に近く公平だが、単独アプリの常用経路が遅くなる |
| 8 | `exec_run` (塞ぐ方) を CPL=3 アプリからも呼べるままにするか | **呼べるまま。ID を池から取る** | 現行の振る舞い (アプリが子を起動して待つ) を落とさない。池が共有なので 5 本目は自然に `ERR_FULL` (模型ケース 8h/11e) | GUI モードでは `exec_run` を `OS32_ERR_INVAL` で塞ぐ。既存アプリの回帰になり得る |

### D10. この設計で**測っていない**こと (K5b への申し送り、[V4])

- `libos32gui.shlib` の `.data`/`.bss` ページ数、GUI アプリの text+bss、
  `pgalloc` メタデータのページ数 — D5 の表はこれらを仮置きした算術で、実測ではない。
- `pgalloc_alloc_n()` は**連続**ページを返す。4 本ぶんを取ったり返したりしたときの
  断片化で、3 本目・4 本目の 256KB スタック (64 ページ連続) が取れなくなる可能性は
  測っていない。取れなければ `ERR_NOMEM` で拒否されるので静かには壊れないが、
  「入るはずが入らない」は起こり得る。ページ単位で張れば連続は不要になる
  (per-app 物理の副産物) ので、K5b で断片化が出たらそちらへ倒せる。
- ゲスト実機 (NP21/W) では何も動かしていない。受入は K5b の G1〜G9。

### D11. 同時 ready の選択規則 (独立レビュー 2026-09-10 の指摘 1、受入 G9)

> **改訂 (2026-09-10、独立レビュー [P2] の差し戻し)**: 初版は「入力群は源が人間だから
> 有限」を根拠に、入力のあるアプリを無制限に優先していた。**この根拠は誤りで撤回する。**
> 反例: アプリが自分の窓を 2 枚持ち、毎周 2 窓へ交互に `set_focus()` すると、
> `set_focus` (`userland/gshell/src/wm.rs:969-985`) が呼ぶ `emit_focus_change`
> (`userland/gshell/src/input.rs:855-872`) は **旧窓と新窓の両方の owner** へ `Focus`
> を流すので、そのアプリ自身に待ち行列型が湧き続ける。人間の入力は要らない。
> 現在の根拠は「入力の有限性」ではなく **turn の有界性** (D11-3) に置き換えてある。
>
> **再改訂 (2026-09-10、同 [P2] の 2 回目)**: 無限待ちは上の改訂で消えたが、
> 上限の式が誤っていた。「1 ラウンドは最大 `MAX_APPS` turn」から
> 「任意の時点から 1 ラウンドぶんで再開できる」は**導けない** — 自分が park した
> 時点では **現ラウンドの残り** と **次ラウンドで自分より先に選ばれる分** の
> 両方が待ち時間になる。反例 (D11-3a の表) では導出群のアプリが 2 ラウンド
> 続けて入力群に先を越され、**30 回目**の `OP_WAIT` でようやく走る。
> 上限は D11-3a で再導出した。**規則そのものは変えていない** (上限は有限で
> 説明できれば足りる、というユーザー方針)。

D0〜D10 は「切替の**機構**」を決めたが、**複数のアプリが同時に待ち解除条件を
満たしたとき誰を起こすか**を決めていなかった。ここで決める。
**プリエンプションは足さない** — 決めるのは「park してある中から次の 1 本を選ぶ順」だけで、
選ばれたアプリは自分が次に `OP_WAIT` に入るまで走り切る。

#### D11-1. 起床条件の棚卸し (実物のどこにあるか)

いま `OP_WAIT` を抜ける条件は `wake_ready()`
(`userland/gshell/src/handler.rs:396-417`) と `op_wait` の期限計算
(`同:355-390`) にある。sticky Quit だけがそこに現れない (リングへ積めた時点で
未読になるが、満杯だと `quit_pending` として WM 側に残る) ので足す。

| 条件 | 実物 | 契約 |
|---|---|---|
| 未読の待ち行列型がリングにある (`Key` `Text` `Button` `Pointer` `Focus` `Close` `Modal` `Quit` `Palette`) | `ring::pending(st, slot) > 0` (`handler.rs:397`、`ring.rs:22-25`) | T3 |
| sticky Quit が積めずに残っている | `session::quit[slot].pending` (`session.rs:217-237`)。**満杯でも捨てない・`dropped` に加算しない** | S5 |
| 期限切れタイマがある | `timer::has_expired(st, owner, now)` (`timer.rs:99-109`) | U5 |
| `Configure` が未通知 | `w.configure_pending` (`handler.rs:407`) | T3 (導出型) |
| 配送できる `Paint` がある (dirty ∩ 可視領域 ≠ 空) | `damage::has_deliverable_paint(w)` (`handler.rs:410`) | T3 / G4 |
| `OP_WAIT` の期限が来た | `deadline` = min(timeout, `timer::next_deadline_owner`) (`handler.rs:359-366, 381-387`) | T3 / U5 |

#### D11-2. 2 群に分ける

契約 T3 は待ち行列型 (発生時にリングへ入る) と導出型 (WM の状態から `OP_POLL` の
ときに作る) を区別している。この線をそのまま使う。

- **入力群** `input_ready(k)`: 未読の待ち行列型がある、または sticky Quit が pending。
  **源はユーザーの打鍵・クリックか WM の決定**で、有限。
- **導出群** `derived_ready(k)`: `ready(k)` かつ入力群でない
  (期限切れ Timer / `Configure` 未通知 / 配送できる `Paint` / `OP_WAIT` の timeout)。
  **源は状態と時間**で、repeat タイマやアニメーションでは**無限に湧き続ける**。

**ただし「入力群は有限」は成立しない** (上の改訂注記の反例)。アプリは自分で
待ち行列型を湧かせられるので、入力群と導出群のどちらも無限に湧きうる。
群分けは**応答性のための優先順位**であって、飢餓を防ぐ根拠にはならない。
飢餓を止めるのは D11-3 の **turn の有界性** — 群分けとは独立の仕掛けである。

#### D11-3. 規則 (これを採る)

`focus` = 最前面窓の owner (`wm.rs:556-561` の `front_owner()`。契約 U1 の
フォーカスは最前面窓と同一 — `set_focus` が `bring_to_front` する、`wm.rs:969-985`)。
`last_run` = 直前に走ったアプリ ID。`turn_used(k)` = k がこのラウンドで turn を
1 回使ったか。`input_streak` = 入力優先で turn を据え置いた**連続**回数。
どれも WM の私有状態で、カーネルは持たない。

**有界性のための 2 つの上限** (ここが差し戻しの修正点):

| 上限 | 値 | 何を止めるか |
|---|---|---|
| `INPUT_STREAK_MAX` | `MAX_APPS` = **4** | 走っているアプリが「自分に入力がある」を理由に turn を据え置ける**連続** `OP_WAIT` 回数。超えたら入力が湧いていても譲る |
| turn は 1 ラウンド 1 回 | `turn_used` | park 中のアプリは 1 ラウンドに 1 回しか選ばれない。全員が使い切ったら一斉にクリアして次のラウンド。**待ちはラウンドをまたぐ**ので、上限は 1 ラウンドぶんではない (D11-3a) |

**(1) 走っているアプリ A の `op_wait` の中で** — park するかどうか

| 状態 | 動作 |
|---|---|
| park 中に `ready` が 1 本も無い | **戻る** (現行の `wm_cycle` + `sys_halt` ループのまま)。**アプリが 1 本のときはここしか通らない = 回帰ゼロ** |
| `input_ready(A)` かつ `input_streak < INPUT_STREAK_MAX` | **戻る** (`input_streak++`)。打鍵の連続を 1 周で取りこぼさない |
| それ以外 (据え置きが上限に達した / 自分は導出だけ / 自分は ready でない) | `exec_park()` |

判定の順序が重要で、**「他に ready が居るか」を先に見る**。1 本しか居なければ
`input_streak` は伸びず、park も起きない。

**(2) WM top-level で次の 1 本を選ぶ**

```
R = { k : park 中 かつ (input_ready(k) または derived_ready(k)) }
C = { k ∈ R : !turn_used(k) }                     -- このラウンドの残り
C = ∅ なら turn_used を全員クリアして C = R        -- 新しいラウンド
C = ∅ (= R = ∅) なら 誰も起こさない                -- wm_cycle + sys_halt

I = { k ∈ C : input_ready(k) }
D = C \ I

I ≠ ∅ :  focus ∈ I  → focus
         それ以外    → I を last_run+1 から ID 昇順に巡回した最初の 1 本
I = ∅  :             → D を last_run+1 から ID 昇順に巡回した最初の 1 本
```

**(3)** 選んだ `k` を `exec_resume(k, wait_ret)`。`wait_ret` は契約 T3 のとおり
`ring::pending(slot_of(k))`。resume した時点で `turn_used(k) = 1`、
`last_run = k`、`input_streak = 0`。

#### D11-3a. 有界性 (これが飢餓しない根拠) — 2026-09-10 再導出

**保証の形**。上限は「いつからいつまで」を決めないと意味が無い。次の 3 つを前提に置く:

| 前提 | 内容 | 外れるとどうなるか |
|---|---|---|
| **起算点** | 対象のアプリ A が **`exec_park()` した `OP_WAIT`** (この回を 0 と数える) | 起算点を「ready になった瞬間」に取ると、A がまだ走っている最中も待ちに数えることになり式が合わない |
| **継続 ready** | A は起算点以降ずっと `ready` (入力群でも導出群でもよい) | ready でなくなれば候補から外れるので、そもそも保証の対象外 |
| **アプリ集合が不変** | その間に起動・終了・kill が無い (`N` = 生きているアプリ数が変わらない) | `N` が変われば上限も変わる。式に入れる `N` はその窓での最大値を取る |

**導出**。

```
1 turn                    ≤ INPUT_STREAK_MAX + 1 回の OP_WAIT
                            (据え置き上限 4 回 + park する 1 回 = 5)
現ラウンドの残り          ≤ N − 1 turn
    A は park の時点で turn_used=1。同じラウンドで 2 回目は回ってこないので、
    残るのは他の N−1 本ぶんだけ。ラウンドの更新は「未使用の ready がゼロ」で
    しか起きないので、これ以上は伸びない。
次ラウンドで A より先     ≤ N − 1 turn
    更新で全員の turn_used が落ちる。A は導出群なので入力群の最大 N−1 本に
    先を越されうるが、turn_used が 2 回目を止めるのでちょうど N−1 本まで。
    A が ready で未使用である限り候補は空にならないので、ここで再度ラウンドが
    更新されることは無い。
⇒ 最悪待ち ≤ (2N − 2) × (INPUT_STREAK_MAX + 1)
```

> **A は起算点から `(2N − 2) × (INPUT_STREAK_MAX + 1)` 回の `OP_WAIT` 以内に走る。**
> `N ≤ MAX_APPS = 4`、`INPUT_STREAK_MAX = 4` ⇒ 定数として **30**。

**(2N−2) を超える経路が無いことの確認** (レビュー指摘への回答):

| 疑い | なぜ超えないか |
|---|---|
| 同じアプリが 1 ラウンドで 2 turn 取る | `turn_used` が止める。resume した時点で立ち、ラウンド更新まで落ちない |
| A を飛ばしたままラウンドが再更新される | 更新の条件は「未使用の ready がゼロ」。A が ready で未使用ならゼロにならない |
| フォーカス優先が順序を崩す | フォーカスの近道も `!turn_used` を条件にしている。ラウンド内の**順**を変えるだけで、turn の**数**は変えない |
| ID 巡回の起点 (`last_run`) が偏る | 起点が偏っても、候補は `!turn_used` で減り続けるので 1 ラウンドの turn 数は N を超えない |
| 1 turn が 5 回を超える | `ma_should_park` が返す 0 は「他に ready が無い」か「入力据え置き < 上限」の 2 つだけ。前者は前提 (継続 ready の A が居る) で起きない、後者は 4 回で尽きる |
| 起動・終了で turn が増える | 前提 3 で除外。`exec_start` は `turn_used=1` で入る (立った直後に走るので、そのラウンドの追加 turn にはならない) |

**実測 (ホストモデル)**。式は上界であると同時に**タイト** — どちらの構成もちょうど届く:

| 構成 | `N` | 式 | 実測 |
|---|---:|---:|---:|
| ケース 15 — A が自作入力を回し、B が `Paint` 待ち | 2 | `(2·2−2)×5 = 10` | **10** |
| ケース 16 — レビュアーの反例。A が入力を消費して `Paint` を残し park、B/C/D は入力群を維持 | 4 | `(2·4−2)×5 = 30` | **30** |

ケース 16 の遷移 (起算点からの `OP_WAIT` 通し番号):
`0 → B` / `5 → C` / `10 → D` / **`15 → B` (ラウンド更新)** / `20 → C` / `25 → D` / **`30 → A`**。
太字がラウンドの切れ目で、A は 2 ラウンド続けて入力群に先を越されている。
これが「現ラウンドの残り + 次ラウンドの先行分」の実体。

**プリエンプションではない**: 上限に当たったアプリは「横取りされる」のではなく、
自分が `OP_WAIT` に入った地点で譲る。走っている最中に取り上げられることは無く、
タイマ割込みも使わない。

#### D11-4. なぜこの形か / 退けた案

| 案 | 退ける理由 |
|---|---|
| **入力優先に上限を置かない (初版)** | **差し戻しの直接原因**。アプリが自分の 2 窓へ交互に `set_focus()` すれば自分に `Focus` が湧き続け (`wm.rs:969-985` → `input.rs:855-872`)、`input_ready` が常に真になって永久に park しない。レビュアーはモデルで「`OP_WAIT` 10,000 回に対し park 判定 0 回、`Paint` 待ちの相手は停止したまま」を再現。**「入力の源は人間だから有限」は成立しない** |
| **上限を「1 ラウンドぶん」と見積もる (改訂 1 版)** | **2 回目の差し戻しの原因**。無限待ちは消えたが、上限の**式**が誤っていた。park した時点では「現ラウンドの残り」と「次ラウンドで自分より先」の両方が待ちになるので、`N × (STREAK+1)` ではなく `(2N−2) × (STREAK+1)` が上界 (D11-3a)。**規則は変えず式だけ直した** — 上限は有限で説明できれば足りる (ユーザー方針: 本格的なマルチタスクにしない) |
| **フォーカス最優先 (無条件)** | 同じ飢餓。フォーカス窓のアプリが repeat タイマ (`timer.rs:125-127`) や自作 `Focus` で ready を作り続けると他が走れない。フォーカスは `turn_used` の**下**に置く (1 ラウンド 1 回の制約を超えられない) |
| **種別の全順序** (入力 > Quit > 期限切れ > Timer > Paint) | 導出型の**中**に順位をつけると、`Paint` しか持たないアプリが Timer 持ちに永久に負ける。契約 T3 は「アプリから見て待ち行列型と導出型の区別は無い」としており、**導出型の中の順位は契約が要求していない** |
| **純ラウンドロビン (群分けなし)** | 飢餓は無いが、クリックの応答が他 3 本の周回ぶん遅れる。群分けは応答性のためだけに残し、飢餓は `turn_used` と `INPUT_STREAK_MAX` で止める — **役割を分けたのが今回の改訂の要点** |
| **期限の近い順 (EDF)** | tick 粒度が 10ms (`PIT_HZ = 100`、`include/memmap.h:347`) なので同時刻が普通に起き、結局 tie-break が要る。巡回より複雑で得るものが無い |
| **`OP_WAIT` のたび毎回 park してから選び直す** | 公平だが、アプリ 1 本のときも `OP_WAIT` ごとに CR3 が 2 回動く (回帰)。(1) の 1 行目で「他に ready が居なければ park しない」と決めたのはこのため (D9-7 と同じ判断) |
| **タイマ割込みで横取りする** | 票の「同時」の定義が明示的に禁じている。上限は「譲る地点を `OP_WAIT` に限ったまま回数を数える」だけで、横取りの足場にはならない |

**入力群を先に見てよい理由** (飢餓の根拠ではなく、応答性の理由): 入力イベントの
宛先はそもそもフォーカス窓の owner なので (`Key`/`Text` はフォーカス窓へ、
`Button`/`Pointer` はヒットした窓へ)、`I` に focus 以外が入るのは
(i) フォーカス切替直後に旧フォーカスへも `Focus` が飛んだとき
(`input::emit_focus_change`、`wm.rs:983`)、
(ii) WM が別アプリへ `Close` / sticky `Quit` を送ったとき (`session.rs:187-201`)
くらい。打鍵とクリックが最短で届くことに意味があり、**飢餓は上限の側が止める**。

#### D11-5. WM とカーネルの分担

- **規則はすべて WM (gshell) 側**。`I` / `D` の判定材料 (リング・タイマ・dirty・
  可視領域・`quit_pending`) は全部 WM の私有状態で、カーネルは 1 つも持っていない。
- **カーネルが持つのは機構だけ**: `exec_park()` / `exec_resume(id, wait_ret)` と、
  「park は `OP_WAIT` 由来のフレームでしか成立せず、resume はその印のあるフレーム
  だけを起こす」というゲート (D0 の C3〜C6)。
  `last_run` / `turn_used` / `input_streak` — **有界性のための状態も全部 WM 側**。
  カーネルに置くと「カーネルが順番を決めている」ように見えてしまい、
  スケジューラを持たないという決めがぼやける。カーネルは「誰を起こせと言われたか」
  しか知らない。
- **逆に、上限に当たったかどうかをカーネルは判定しない**。`exec_park()` は
  「WM が譲ると決めた」ことの実行であって、譲るべきかの判断はしない
  (判断を入れるとそれがスケジューラになる)。
- したがって K5b の発注は **W レーン (gshell) に D11-3 の規則**、
  **K レーン (カーネル) に D0/D2/D8 の機構とカウンタ**、と割れる。

#### D11-6. ホストモデルでの固定

`tools/tests/multiapp_model_host.c` にケース 12〜16 (計 24 検査) として入れた。
`input_ready` / `derived_ready` は WM が毎周期計算するもので、模型では試験が直接
立てる (**「何が ready か」ではなく「ready が複数あるときの選び方」だけ**を固定する)。

| ケース | 見るもの |
|---|---|
| 12 | 入力群のフォーカス最優先 / 入力群の巡回 / 導出群の巡回 / **導出群ではフォーカスを優先しない** / 入力は導出より先 / ready ゼロなら誰も起こさない |
| 13 | park 判定 5 通り (自分に入力→戻る / 他に ready 無し→戻る / 他に入力→譲る / 他も導出→譲る / 自分が ready でない→譲る) |
| 14 | 飢餓なし — 導出群だけの 4 本が 2,3,4,5 の順にちょうど 1 回ずつ走り、1 周したら先頭へ戻る |
| 15 | **レビュアーの反例 1 (無限待ち)** — A が毎周自分に `Focus` を湧かせ、B は `Paint` 待ちで park 中 (`N`=2)。(a) park は必ず起きる (b) B は必ず走る (c) B は `N`=2 の上限 **10** にちょうど届く (d) 定数 `MA_STARVE_BOUND` の内側 |
| 16 | **レビュアーの反例 2 (ラウンドまたぎ)** — A が入力を消費して `Paint` を残し park、B/C/D は入力群を維持 (`N`=4)。(a) A は必ず走る (b) 再導出した上限以内 (c) **ちょうど 30 に届く** (= 式がタイト) (d)(e) フォーカスを B / D に置いても上限を超えない |

模型の定数 `MA_INPUT_STREAK_MAX` (= `MA_MAX_APPS` = 4) と
`MA_STARVE_BOUND` (= `(2 × MA_MAX_APPS − 2) × (MA_INPUT_STREAK_MAX + 1)` = **30**) が
D11-3a の式そのもの。ケース 15c は `N`=2 の実例 (10)、ケース 16c は `N`=4 の実例 (30) を
**等号で**検査しているので、式を緩めても締めても落ちる。

RED→GREEN の記録は
[`tools/tests/multiapp_model_tdd.md`](../../../tools/tests/multiapp_model_tdd.md) の
回 9 / 回 10 (D11 初版)、**回 11 (RED: 反例 1 で park 0 回) → 回 12 (GREEN: 78 検査)**、
**回 13 (RED: 反例 2 が旧上限 20 を破る) → 回 14 (GREEN: 上限を再導出、84 検査 ALL PASS)**。

#### D11-7. この規則で**測っていないこと**

- 実際の操作感 (クリックしてから窓が反応するまでの ticks)。ゲスト未検証。
- **上限そのものの値** (`INPUT_STREAK_MAX = 4`)。D11-3a の有界性は 4 でなくても
  成り立つ (どんな有限値でも成り立つ) ので、4 は「アプリの数だけは続けて持てる」
  という説明できる線を引いただけ。**打鍵の連続を取りこぼさない最小値がいくつかは
  測っていない** — ゲストで G9 を見るときに、連打しながら他アプリの `Paint` が
  遅れる度合いを見て決め直せる。値を変えても規則は変わらない。
- **ラウンドをまたぐ待ちを詰める代案** (採らないが記録として残す)。上限 30 の
  ほぼ全部が「現ラウンドの残り + 次ラウンドで自分より先」で、その半分は
  「park した導出群のアプリが、次ラウンドでも入力群の後ろに回される」ぶん
  (D11-3a のケース 16 の遷移でいう `15 → B` 以降)。**park したアプリを次ラウンドの
  先頭に置く** (ラウンド更新時にそのアプリだけ最優先の候補にする) と、この分が
  消えて上限は `(N−1) × (STREAK_MAX+1)` = 15 まで落ちる。採らないのは、
  (i) 「先頭に置く」印がもう 1 つ増えて規則が 1 段複雑になる、
  (ii) ユーザー方針で上限は**有限で説明できれば足りる**、の 2 点。
  G9 の実測で 30 回 (= PIT 10ms 換算で最悪 0.3 秒相当。ただし 1 `OP_WAIT` が
  10ms とは限らないので**換算は目安ですらない**) が体感で問題になれば、
  ここから着手できる。
- 一斉クリアの直後に**直前に走ったアプリがもう一度選ばれうる** (ケース 15 の B が
  park 2 回目で走るのはこれ)。上の代案はこれも同時に消す。
- `last_run` を park 側で進めるか resume 側で進めるかで、1 周の順が 1 個ずれる。
  模型は resume 側 (走り出した時点) で進めている。実装もそれに揃えること。

## 決裁 (2026-09-11、ユーザー。レビュアー枯渇のため PM の材料提示に基づく)

| # | 決裁 |
|---|---|
| D11 | **凍結**。規則は D11-3、上限は D11-3a の 30 回 (前提 3 つ付き)。値 (`INPUT_STREAK_MAX = 4`) は G9 の実測で決め直してよい |
| D9-1 | (b) syscall フレーム保存 + 単一カーネルスタック |
| D9-2 | 所有者 ID の池は GUI と CUI で共有 (1 池 4 本) |
| D9-3 | `heap_size = 0` の既定は変えない。**8MB は「GUI アプリが 1 本立つ」が要件**で、複数は本体メモリ依存。入らなければ拒否 (受入 G6) |
| sbrk は二段構え (2026-09-11) | `heap_size = 0` の CPL=3 プログラムの sbrk 物理は、空きが足りれば従来式 (帯の残り = `guard_a` まで)、足りなければ最低分 `MEM_EXEC_SBRK_MIN` (256KB)。段 2 でも入らなければ従来どおり拒否。`heap_size` 明示時は変えない。採った段は `exec_sbrk_tier_last` / `exec_sbrk_tier_count[2]` で読む (KAPI にしない) |
| D9-4 | **`snd_cleanup_owned` ではなく、音の排他**: フォーカス追従で止める・復元する、同時には鳴らさない (受入 G10)。設計は K5b-K で YM2203 (FM3+SSG3) の状態退避・復元として具体化する |
| D9-5 | `exec_start(cmdline) → app_id / 0 / 負` |
| D9-6 | `exec_kill(app_id)` を切る |
| D9-7 | park は他に起こす相手が居るときだけ |
| D9-8 | `exec_run` は CPL=3 アプリから呼べるまま (ID は池から) |

8MB の意味 (PM の補足): カーネル+SQLite 3MB / gshell 帯 1MB / 共有ライブラリ帯 1MB / アプリ帯 3MB
(0x500000〜0x7FFFFF)。GUI の段階で枯渇はしない。D5 の「8MB で 1 本」は 2 本目が入るかの話で、
既定の折半で 1 本目が空きの大半を取るため。要件が 1 本なら既定は変えなくてよい。

**PM 受入 (2026-09-11、K5b-W からの提案)**: D11-3 の `should_park` の先頭に「top-level にしか出来ない
仕事 (`LAUNCH` の保留 / `launch_pending`) があるなら譲る」を**追加する**。理由: `exec_start` は WM
top-level からしか呼べず、top-level へ戻る道は park だけなので、これが無いとアプリが 1 本走っている
間は 2 本目を永久に起動できない。起動要求は有限個の事象で消費されれば条件が消えるため、D11-3a の
上界 (30) は不変。模型 (`ma_should_park`) にも同じ分岐を足した (2026-09-11)。

