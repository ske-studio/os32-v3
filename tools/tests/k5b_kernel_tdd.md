# K5b-K TDD 記録 — アプリ 4 本の同時実行 (カーネル側、KAPI v44)

対象票: [`docs/archive/gui_v13/TASK_K5B_kernel.md`](../../docs/archive/gui_v13/TASK_K5B_kernel.md)
設計の正典: [`docs/archive/gui_v13/TASK_K5_multiapp.md`](../../docs/archive/gui_v13/TASK_K5_multiapp.md) §設計 D0〜D11 + 決裁 (2026-09-11)

新しい挙動ごとに **RED を先に見てから GREEN にした**。RED は「実装前の状態を
再現したビルド」で実際に走らせて記録している (後付けの偽装ではない)。RED 用の
ハーネスはスクラッチパッドに置いた使い捨てで、リポジトリには入れていない —
やり方は各回の「RED の作り方」に書いてあるので、同じ手順で再現できる。

| 回 | 対象 | RED | GREEN |
|---|---|---|---|
| 1 | P1/P2/P6 (per-app 物理の写像) | 実装前の `kernel/paging.{c,h}` に対して試験を組むと**コンパイルが通らない** | `tools/tests/test_app_band_pde.py` EXIT=0 |
| 2 | 所有者 ID による回収 (P3) | `shm_free_owned` から owner の判定を落とすと 4 検査が落ちる | `tools/tests/test_owner_reclaim.py` 27 検査 ALL PASS |
| 3 | AppSlot と印 (D0 の C1〜C6) | `appslot_park_check` の OP_WAIT ゲートと `appslot_resume_check` の印判定を落とすと 11 検査が落ちる | `tools/tests/test_multiapp_impl.py` 98 検査 ALL PASS |

---

## 回 1 — per-app 物理の写像 (P1 / P2 / P6)

**新しい挙動**: 同じ仮想 0x500000 にアプリごとに別の物理を載せる (設計 D1 の I5/I6)。

- `paging_addrspace_map_user_range_phys()` (P1) — 仮想 ≠ 物理で範囲を張る
- `paging_addrspace_clear_app_band()` (P2) — アプリ PT の identity コピーを全部落とす (**I6**)
- `paging_addrspace_free_user_range()` (P6) — 張った物理だけを PTE を辿って返す

**RED の作り方**: `tools/tests/app_band_pde_host.c` の新しい節 H を、実装前の
`kernel/paging.c` / `kernel/paging.h` のスナップショットに対してビルドする
(ホストハーネスの `paging_host_source.c` の差し替え元を旧ファイルにし、
`-I` の先頭に旧 `paging.h` を置いたディレクトリを入れる)。

**RED の出力** (2026-09-11):

```
app_band_pde_host.c:238:19: error: implicit declaration of function
    ‘paging_addrspace_clear_app_band’ [-Werror=implicit-function-declaration]
app_band_pde_host.c:255:19: error: implicit declaration of function
    ‘paging_addrspace_map_user_range_phys’; did you mean
    ‘paging_addrspace_map_user_range’? [-Werror=implicit-function-declaration]
app_band_pde_host.c:279:19: error: implicit declaration of function
    ‘paging_addrspace_free_user_range’ [-Werror=implicit-function-declaration]
cc1: all warnings being treated as errors
```

**GREEN**: `python3 -B tools/tests/test_app_band_pde.py` → EXIT=0

```
PASS: pde count rule (heap_size 0 = 1 pde, ram-capped, max clamped)
PASS: 1 pde layout identical to the current one
PASS: 2 pdes get private PTs, USER never reaches the master PDE/PT
PASS: bad count / out of pages roll back leaving master untouched
PASS: clear_app_band drops I6 identity, map_range_phys maps virt != phys
PASS: free_user_range returns only what it mapped, never shared PTs
PASS: app band selftest, keep/clone selftests still green
HOST ILP32 + TARGET GNU89 PASS
```

節 H が固定した不変条件:

- 生成直後のアプリ PT は master の identity コピーで、`clear_app_band` の後は
  **1024 エントリ全部が 0** (I6 = 「物理 0x5xxxxx が素通しで見える」を落とす)
- master の PT / PDE は 1 ビットも動かない。PDE は PT を指したまま present/RW、USER だけ落ちる
- `map_user_range_phys` は仮想 ≠ 物理で張り、USER は **そのアプリ PD の PDE にだけ**伝播する
- 物理 0 / 非整列 / 逆順は全範囲を未変更で拒否する
- `free_user_range` は張った枚数ちょうどを返し、PTE を 0 に戻し、
  二度呼んでも 0 枚 (二重解放しない)、共有帯 (VRAM) を渡しても 1 枚も解放しない

ブート時 `kselftest` から呼ばれる `paging_app_band_selftest()` にも同じ検査を
足した (ビット 4096 = P2、8192 = P1、16384 = P6)。

---

## 回 2 — 所有者 ID による回収が 1 本分で閉じる (D3 / P3)

**新しい挙動**: `shm_cleanup_all()` (所有者を見ない) を `shm_free_owned(id)` に置き換え、
`exec_exit` の回収 7 種を全部アプリ ID で回す。これが無いと、アプリ A の終了が
アプリ B の SHM ブロックを巻き上げる (4 本同時では実際に起きる)。

**RED の作り方**: `kernel/shm.c` の `shm_free_owned` から
`if (shm_block_owner[i] != owner) continue;` の 1 行を削り (= 旧 `shm_cleanup_all`
と同じ「所有者を見ない」挙動)、`tools/tests/owner_reclaim_host.c` をその
`shm.c` に対してビルドする。

**RED の出力** (2026-09-11):

```
  FAIL 3b ID 3 の SHM ブロックは使用中のまま
  FAIL 3c 属性を戻したのは ID 2 の 2 ブロックだけ
  FAIL 4c 無関係な ID の SHM はシェルの回収でも残る
  FAIL 5a owner 0 の回収は何も解放しない
FAILURES (4/27)
RED exit code: 1
```

**GREEN**: `python3 -B tools/tests/test_owner_reclaim.py` → 27 検査 ALL PASS

実物の `fs/fd_redirect.c` / `fs/pipe_buffer.c` / `kernel/shm.c` をそのまま
コンパイルしている (回収のロジックを試験側に写していない)。見ているもの:

- ID 2 と ID 3 が redirect / pipe / SHM を持った状態で **ID 2 だけ**を畳むと、
  ID 2 の分だけが消え、ID 3 の資源は 1 つも触られない
- SHM ブロックに確保時の ID タグが付き、返ったブロックのタグは消える
- GUI 予約ブロック (`SHM_RESERVED`、契約 T2) は誰の回収でも無傷
- 所有者 0 (タグなし) の回収は何も解放しない
- `shm_lock` 済みのブロックも所有者の回収で返る

FD (`vfs_close_owned`) と DB (`db_cleanup_owned`) は既に
`tools/tests/test_vfs_fd_sqlite.py` / `tools/tests/test_kapi_db_owned.py` が
同じ形で見ているので重複させていない。

---

## 回 3 — AppSlot と「OP_WAIT 由来」の印 (D0 の C1〜C6、受入 G7)

**新しい挙動**: `exec_ctx_stack`(段のスタック) を **ID で引く表** (`exec/appslot.c`) に
置き換え、park は `gui_call(OP_WAIT)` の中からだけ、resume は
「OP_WAIT で park された印のあるフレーム」に対してだけ成立させる。
WM の行儀を信じるのではなく**カーネルが弾く**。

**RED の作り方**: `exec/appslot.c` から
(a) `appslot_park_check` の OP_WAIT ゲート (`if (!g_cur_op_is_wait || !a->in_op_wait) {...}`) と
(b) `appslot_resume_check` の印判定 (`if (!a->parked_from_wait) {...}`) を削り、
`tools/tests/multiapp_impl_host.c` をその `appslot.c` に対してビルドする。

**RED の出力** (2026-09-11):

```
  FAIL 3a gui_call の外では park できない
  FAIL 3e OP_WAIT の中でだけ park できる
  FAIL 17a 印の無いフレームは OS32_ERR_STALE で拒否される
  FAIL 17b 拒否のたび ring3_resume_bad_frame_count が増える
  FAIL 17c 拒否は ring3_switch_count を増やさない
  FAIL 17d 拒否されたアプリは park のまま、WM は top-level のまま
  FAIL 17e 印のあるフレームは起こせる
  FAIL 17f 成功は switch_count だけを増やす
  FAIL 17g resume した時点で印は消える (二度は起こせない)
  FAIL 17h OP_WAIT 以外の op からの park は 2 回とも弾かれる
  FAIL 17i 弾いた回数が ring3_park_reject_count に載る
FAILURES
RED exit code: 1
```

**GREEN**: `python3 -B tools/tests/test_multiapp_impl.py` → 98 検査 ALL PASS
+ `TARGET i386-elf GNU89 -Werror COMPILE PASS`

内訳: K5a のホスト模型 (`tools/tests/multiapp_model_host.c`) の **84 検査**を
番号・検査名ごと写し、票 K5b が追加で求めた「印なし resume の負例」を
ケース 17 (14 検査) として足した = 98。ケース 1〜16 は模型と 1 対 1 なので、
落ちた検査名でそのまま「模型のどの規則が実装で崩れたか」が分かる。

模型との意図した差 (3 点。詳細は `multiapp_impl_host.c` の冒頭コメント):

1. 期待値は模型の `MA_ERR_*` ではなく **実物の `OS32_ERR_*` / `EXEC_ERR_*`**
2. ケース 6 (SHM スロット) と 12〜16 (同時 ready の選択規則) は設計 D6 / D11-5 が
   **gshell (WM) の領分**と決めた部分でカーネルには無い。ハーネス側に模型と
   同じ規則を置き、その下で park / resume だけを実物に通している
3. ケース 17 は票 K5b の追加要求 (C6 の負例) + CUI の入れ子の子は park できないこと

---

## 実行コマンド (`make check` への登録は PM)

```bash
python3 -B tools/tests/test_app_band_pde.py     # 回 1 (既存の的に節 H を追加)
python3 -B tools/tests/test_owner_reclaim.py    # 回 2 (新規)
python3 -B tools/tests/test_multiapp_impl.py    # 回 3 (新規)
```

`build/sdk.mk` の `check:` へは PM が足す (票の指示どおりこちらでは触っていない)。
足すときの並びの例:

```make
check-owner-reclaim-host:
	@python3 -B tools/tests/test_owner_reclaim.py

check-multiapp-impl-host:
	@python3 -B tools/tests/test_multiapp_impl.py
```

## この記録で**測っていないこと** ([V4])

- **ゲストは未検証**。NP21/W では 1 度も動かしていない (配備・エミュレータは
  この役の禁止範囲)。受入 G1〜G10 は PM とテスターの担当。
- 実際の GUI アプリの text+bss / `libos32gui.shlib` の .data ページ数は測っていない。
  D5 の見積表は仮置きのままで、8MB で GUI アプリが 1 本立つか (受入 G6) は実機の話。
- `pgalloc` の断片化。3 領域は連続で取れなければページ単位に倒すようにしたが、
  実際にどちらの経路を通るかは実機でしか分からない。

---

# 追記: 回 4 — sbrk 物理の二段構え (ユーザー決裁 2026-09-11)

対象票: [`docs/archive/gui_v13/TASK_K5B_kernel.md`](../../docs/archive/gui_v13/TASK_K5B_kernel.md) 作業 8
決裁: `docs/archive/gui_v13/TASK_K5_multiapp.md` 末尾の決裁表「sbrk は二段構え (2026-09-11)」

| 回 | 対象 | RED | GREEN |
|---|---|---|---|
| 4 | sbrk 物理の二段構え | 段の判定を K5b-K の「常に最低分」へ戻すと 4 検査が落ちる / 枚数の勘定を外すと 2 検査が落ちる | `tools/tests/test_sbrk_tier.py` 27 検査 ALL PASS |

## 何を変えたか

K5b-K (`38266a7`) は per-app 物理にしたとき、`heap_size` 未指定の CPL=3
プログラムの sbrk に張る物理を最低分 `MEM_EXEC_SBRK_MIN` (256KB) へ固定した。
identity だった頃は帯の残りぜんぶ (≒1.4MB) が黙って sbrk に使えたので、
`malloc` を多用する CUI プログラム (`less` 等) が割を食う。決裁は二段構え:

- **段 1 (従来式)** — 3 領域 (本体+sbrk / exec_heap / スタック) + PD + アプリ PT を
  「sbrk 上端 = `guard_a`」で見積もった総ページ数が `pgalloc_free_pages()` に
  収まるなら、`sbrk_end = guard_a`。K5b-K 以前とまったく同じ範囲を張る。
- **段 2 (最低分)** — 収まらなければ `sbrk_end = code_end + MEM_EXEC_SBRK_MIN`。
- 段 2 でも収まらなければ `appslot_start_admit()` が `EXEC_ERR_NOMEM`。既存の経路で、
  切り詰めもスワップもしない。
- `heap_size` を明示したプログラムは**この分岐に入らない** (K5b-K のまま最低分)。
  要求した `exec_heap` を必ず渡すのが先で、sbrk を伸ばす余地はそこに無い。
- CUI (`exec_run` の入れ子) と GUI (`exec_start`) はどちらも `exec_launch()` の
  同じ場所を通るので、規則は 1 つ。

判定は `exec/exec.c` の 2 関数に切り出した:

```c
static u32 exec_ring3_pages(u32 load_base, u32 sbrk_end, u32 exec_heap_size,
                            u32 band_pdes);
static int exec_sbrk_pick_tier(u32 load_base, u32 code_end, u32 guard_a,
                               u32 exec_heap_size, u32 band_pdes,
                               u32 free_pages, u32 *sbrk_end);
```

`exec_ring3_pages()` は D5 の勘定式そのもの
(`(sbrk_end-load)/4K + exec_heap/4K + stack/4K + 1 + band_pdes`) で、
勘定の場所 (`appslot_start_admit` の直前) と段の判定が**同じ 1 本の式**を見る。

## どちらの段で走ったか (観測点)

KAPI にはしない。`fault_kill_count` と同じカーネルシンボルで、`emu_read_mem` で読む:

```c
volatile u32 exec_sbrk_tier_last;      /* 直近の CPL=3 起動が採った段 (1 or 2) */
volatile u32 exec_sbrk_tier_count[2];  /* [0] = 段 1 の累計、[1] = 段 2 の累計 */
```

数えるのは 3 領域を実際に張り終えた起動だけ (`app_map_region` が 3 本とも
成功した直後)。途中で失敗した起動は数に入らない。番地は毎ビルドの
`build/out/kernel.map` を見ること (固定値を控えない → `POLICY_DEBUG` §2)。

## 試験 (`tools/tests/test_sbrk_tier.py` + `sbrk_tier_host.c`)

`test_pgalloc_model.py` が `exec_child_claim` を切り出すのと同じ流儀で、
**`exec/exec.c` の当該 2 関数をテキストのまま切り出して**ホストへ差し込み、
`exec/appslot.c` と一緒に ILP32 freestanding でコンパイルして走らせる。
並行して書いた別式ではなく、出荷するコードそのものを見ている。
レイアウト (`code_end` / `guard_a` / `exec_heap_size`) は `exec_launch()` と
同じ式でハーネスが 1 つ組む (帯 1 枚、text+bss = 64KB)。

3 性質 = 27 検査:

| 節 | 性質 |
|---|---|
| case 1 (8 検査) | 空きが十分 → 段 1。`sbrk_end == guard_a` で、最低分より広い |
| case 2 (8 検査) | 空きが 1 ページ足りない → 段 2。`sbrk_end == code_end + 256KB` ちょうど。帯の残りが最低分より狭い縁では `guard_a` で頭打ちにし、その場合は従来式と同じものを張ったので段 1 と数える |
| case 3 (11 検査) | 段 2 でも足りない → `EXEC_ERR_NOMEM`。生存アプリ数・現在のアプリ・資源の所有者・既存 2 本の `state` と `pages` がどれも変わらない。段 1 の枚数では拒否される空きで段 2 の枚数なら立つ (= 二段構えが効く場面) |

### RED の作り方 (2 通り、どちらも実際に走らせた)

RED 用のハーネスは置いていない。`exec/exec.c` / `exec/appslot.c` を一時的に
K5b-K の状態へ戻して同じ試験を回す。

1. `exec_sbrk_pick_tier()` の本体から段 1 の判定を落とし、常に
   `*sbrk_end = code_end + MEM_EXEC_SBRK_MIN` を返す (= K5b-K の挙動)。
   → `1c` `1d` `1e` `1f` の 4 検査が FAIL、EXIT≠0。
2. `appslot_start_admit()` の `if (free_pages != 0 && pages > free_pages)` を外す。
   → `3d` `3j` の 2 検査が FAIL、EXIT=1。

どちらも戻して GREEN (27 検査 ALL PASS、`EXIT=0`) を確認した。

### 実行コマンド

```bash
python3 -B tools/tests/test_sbrk_tier.py    # 回 4 (新規)
```

`build/sdk.mk` は票の指示どおり触っていない。`check-memory-host` の並びに
足すのは PM の担当。

## この追記で**測っていないこと** ([V4])

- **ゲストは未検証**。NP21/W では 1 度も動かしていない。8MB 構成で GUI アプリを
  立てたときに実際どちらの段になるか、`less` の `malloc` が段 1 で楽になるかは
  実機の話 (受入 G6 と併せてテスターの担当)。
- 全体ゲート (`make all` / `make external` / `make check`) は回していない。
  ここで通したのは `make kernel` (EXIT=0) と上の 1 本だけ。
- 段 1 は「収まるなら張る」ので、空きをほぼ使い切る起動を許す。その直後の
  V86 バッキングや PT の動的確保が痩せる可能性は残る (決裁どおりの実装で、
  余白は取っていない)。実機で足りなくなるようなら余白の議論は改めて。

---

# 追記: 回 5 (節 R) — 実機初回起動の差し戻し「FATAL: shell.bin load failed」

対象票: [`docs/archive/gui_v13/TASK_K5B_kernel.md`](../../docs/archive/gui_v13/TASK_K5B_kernel.md) (差し戻し 2026-09-11)
実機の観測 (PM、NP21/W、15MB 構成): `kselftest_pass=44 kselftest_fail=0` でカーネルは
起動しているのに、text VRAM 1 行目が `FATAL: shell.bin load failed` + shlib のロード
報告、EIP = `kernel_main+0xd4c` (`kernel/kernel.c:582` の停止ループ)。

| 回 | 対象 | RED | GREEN |
|---|---|---|---|
| 5 | `ext2_read_file()` が `max_size` を越えて書く | 実装前の `fs/ext2_file.c` に戻すと 4 検査中 3 検査が FAIL (`resolved[]` が読んだファイルの中身で潰れる) | `tools/tests/test_ext2_read_bound.py` 4 検査 ALL PASS |
| 5' | シェル帯 (ID 1) が per-app 経路に入らない | — (境界を固定する追加検査。K5b-K でも GREEN) | `tools/tests/test_multiapp_impl.py` ケース 18 (13 検査) ALL PASS |

## 原因 (推測ではなく実ソースと `kernel.map` で確定)

`fs/ext2_file.c:34` (K5b-K 前からある行) —

```c
ret = ext2_read_block(ctx, phys, &dst[total_read]);   /* 端数ブロックでも 1KB 書く */
```

`ext2_read_block()` は `dev_blk_read_lba` を 512B × 2 回呼ぶだけで、**`to_copy` に
関係なく必ず `EXT2_BLOCK_SIZE` = 1024 バイト書く**。`ext2_read_file()` は
`remaining` を `max_size` で頭打ちにしているが、書き込みの長さは頭打ちにしていない。
`max_size` が 1KB の倍数でない呼び出しは最大 1023 バイト溢れる。

K5b-K 前は exec が「ファイル全体をロード番地へ」読んでいたので、溢れ先は
そのプログラム自身の帯の中で無害だった。K5b-K が D1 の手順 2 のためにヘッダを
**先読み**するようにし (`exec/exec.c:996`)、宛先を

```c
static u8 hdrbuf[OS32X_HDR_V2_SIZE + 64];   /* = 108 バイト、カーネル .bss */
```

に変えた瞬間に顕在化した。`build/out/kernel.elf` の `.bss` は

```
00150900 0000006c b hdrbuf.2      <- ここへ 1024 バイト書かれる
00150980 00000100 b resolved.3    <- 解決済みパス。まるごと潰れる
00150a80 00000018 b g_exit_jmpbuf
00150ab4 00000004 b ring3_tramp_page
00150ac0 00002000 b ring3_tramp_raw   <- 先頭 0x240 バイトまで潰れる
```

で、`hdrbuf` の 108 バイトの直後に **`resolved[]` (解決済みパス) が居た**。
ヘッダ先読みが `resolved` をファイルの中身で塗り潰し、続く本体読み込み
`vfs_read(resolved, file_buf, read_max)` (`exec/exec.c:1298`) がゴミのパスを
引くので `sz <= 0` → **`EXEC_ERR_NOT_FOUND`**。`/sys/shell.bin` も FDD
フォールバック (`SYS_SHELL_BIN_FDD` は同じ綴り) も同じところで落ちるため、
`kernel/kernel.c:582` の `FATAL: shell.bin load failed` に落ちる。
`EXEC_ERR_NOMEM` の経路 (`[DBG] NOMEM: ...`) は**通っていない** — 画面に
その行が 1 つも出ていないことと符合する。

`14000` (実機の 2 行目) は別物ではなく、`kernel/shlib.c:191` の

```
[shlib] /sys/lib/libos32gui.shlib v1 loaded: 101 funcs, text 20 pg, data 4 pg @414000
```

が 80 桁で折り返した尻尾 (`@4` が桁 78-79、`14000` が次行)。`FATAL:` の 28 文字が
桁 0-27 を上書きしたので `shlib v1 loaded:` から見えている。**デバッグ出力ではなく
正規のロード報告なので残す**。

## 直したもの

1. `fs/ext2_file.c` — 端数ブロックは `ext2_g_blk` を中継して `to_copy` だけ写す
   (`ext2_read_stream` が前からやっている約束と同じ。`ext2_g_aux` は `ext2_bmap`
   が使うので不可 — gotcha §4-24)。ブロック境界ちょうどの読みは経路が変わらない。
2. `exec/exec.c` — 読み込み失敗時の `exec_cpl0_release()` を `!is_shell` で囲った。
   claim しているのは CPL=0 の**子**だけで、シェルは通っていない (現状は
   `g_cpl0_children <= 0` の門で無害だが、左右が揃っていなかった)。
3. `exec/appslot.c` / `.h` — `appslot_launch_is_app(is_shell, hdr_flags)` を足し、
   `exec_launch` の `want_ring3` をこれに置き換えた。シェル帯とアプリ帯の
   分かれ道が 1 か所になり、ホストで押さえられる (ケース 18)。

`is_shell` 経路の物理配置は **K5b-K でも既に元のまま**だった (PM の見当は外れ):
`MEM_SHELL_LOAD_ADDR` / `MEM_SHELL_MAX_SIZE` / `MEM_SHELL_STACK_TOP` /
`kapi->sbrk_heap_limit = guard_b`、`appslot_start_admit` も
`paging_addrspace_create_n` も `exec_cpl0_claim` も通らない。ここは変えていない。

## RED の作り方 (実際に走らせた)

```bash
git show HEAD:fs/ext2_file.c > fs/ext2_file.c     # K5b-K 時点へ戻す
python3 -B tools/tests/test_ext2_read_bound.py
```

```
FAIL overrun: max_size=108 wrote past +108 (offset +108)          EXIT bound_header=1
FAIL overrun: max_size=65536 wrote past +1500 (offset +1500)      EXIT bound_tail=1
  R3 block-aligned reads unchanged                                EXIT bound_aligned=0
FAIL: resolved[] clobbered: "()*+,-./0123456789:;<=>?@ABC..."     EXIT exec_bss_neighbour=1
SUMMARY 1/4 PASS
```

R4 の FAIL 文字列が実機の症状そのもの — `resolved[]` が読んだブロックの中身で
潰れている。直してから `SUMMARY 4/4 PASS`。

## 追加した検査

| 節 | 性質 |
|---|---|
| R1 | `max_size` が 1KB の倍数でない読み (exec のヘッダ先読みと同じ 108 バイト) が `max_size` を 1 バイトも越えない |
| R2 | ファイル末尾が端数ブロック (1500 / 1 / 1025 バイト) でも、返した長さより先を書かない |
| R3 | ブロック境界ちょうどの読みは従来どおり (経路を変えていない) |
| R4 | 108 バイトのヘッダバッファの直後に解決済みパスを置いた **実機と同じ並び**で、パスが生き残る |
| 18a-d | `appslot_launch_is_app()`: シェルは `flags` に関わらずアプリ帯を使わない / 子は従来どおり (`--cpl0` は identity) |
| 18e-k | 空き 1 枚 (GUI アプリは `EXEC_ERR_NOMEM`) でもシェルは起動し、空きページ・ID の池・資源の所有者をどれも動かさない |
| 18l-m | gshell ⇔ CUI shell の載せ替えを繰り返しても ID 1 / 段 1 のまま |

## 実行コマンド

```bash
python3 -B tools/tests/test_ext2_read_bound.py     # 回 5 (新規、4 検査)
python3 -B tools/tests/test_multiapp_impl.py       # ケース 18 を追加 (111 検査)
```

`build/sdk.mk` に `test_ext2_read_bound.py` (`check-vfs-mount-dev-host`) と、
K5b-K で登録されていなかった `test_multiapp_impl.py` / `test_owner_reclaim.py`
(`check-multiapp-model-host`) を足した。不要なら PM が外す。

## この追記で**測っていないこと** ([V4])

- **ゲストは未検証**。NP21/W では 1 度も動かしていない。実機の初回起動が直ったか
  どうかはテスターの再配備待ち。`kernel.map` の番地は動いている
  (`hdrbuf.2` = 0x150900 → 0x150aa0)、kselftest の読み出しは新しい地図で。
- 全体ゲート (`make all` / `make external` / `make check`) は回していない。
  通したのは `make kernel` (-Wall 警告ゼロ)、`tools/check_constraints.py` (EXIT=0)、
  および `test_ext2_read_bound` / `test_multiapp_impl` / `test_owner_reclaim` /
  `test_sbrk_tier` / `test_app_band_pde` / `test_vfs_mount_dev` の 6 本。
- ヘッダ先読みの宛先が 108 バイトのままである点は変えていない。溢れは FS 側で
  塞いだので安全だが、他の FS ドライバが同じ癖を持ち込まない保証はコードには無い
  (`fatfs` の `f_read` と `iso9660` / `hostdrv` は確認済みで、いずれも指定長を守る)。

---

# 追記: 回 6 (ケース 19) — CPL=3 アプリ生存中は `--cpl0` の子を拒否 (申し送り A1)

対象票: [`docs/archive/gui_v13/TASK_K5B_kernel.md`](../../docs/archive/gui_v13/TASK_K5B_kernel.md) 末尾「申し送り A1」/ **ユーザー決裁 2026-09-11**

| 回 | 対象 | RED | GREEN |
|---|---|---|---|
| 6 | `--cpl0` の子 × 生きている CPL=3 アプリ | `appslot_cpl0_admit()` から生存アプリの判定を落とす (実装前 = 素通し) と 26 検査中 **10 検査が FAIL** | `tools/tests/test_multiapp_impl.py` 137 検査 ALL PASS |

## 何が壊れていたか

`--cpl0` の子 (`OS32X_FLAG_FORCE_CPL0`) は `exec/exec.c` の `exec_cpl0_claim()` で
アプリ帯 `[MEM_EXEC_LOAD_ADDR, mem_end)` を identity で**丸ごと** `pgalloc_mark_used` し、
`exec_cpl0_release()` で丸ごと `pgalloc_free_n` する。K5b-K 以後は CPL=3 アプリの
per-app 物理も同じ pgalloc から取るので、GUI アプリが park 中に `--cpl0` の子を立てると

- (a) 子が生きているアプリの物理を上書きし、
- (b) 子の終了で生きているアプリのページまで free する。

## 直したもの

1. `exec/appslot.{c,h}` — `appslot_cpl0_admit(int is_shell)` を追加。
   `is_shell` は対象外 (シェル帯はアプリ帯を使わない)、それ以外は
   `appslot_live() > 0` なら `OS32_ERR_FULL`。**状態は 1 つも変えない**
   (`appslot_launch_is_app` / `appslot_start_admit` と同じ「判定だけ」の流儀)。
2. `exec/exec.c` — `exec_launch` の `want_ring3 == 0` の枝 (= シェル or `--cpl0` の子) に
   `else if (appslot_cpl0_admit(is_shell) < 0)` を置き、`OS32_ERR_FULL` を返す。
   位置は `want_ring3` を決めた直後 = **`exec_cpl0_claim()` より前**で、
   `paging_addrspace_create_n` / `exec_heap_save_state` / `ring3_band_set` の
   どれよりも手前。claim も alloc も 1 つも行わないので `exec_restore_band` も要らない。

戻り値は `OS32_ERR_FULL` (`sdk/include/os32/os32_kapi_shared.h:311`、「スロット / 資源が満杯」)。
専用番号は増やしていない。`exec_launch` の既存の拒否と流儀を揃えてある —
ID の池が尽きたときの `appslot_start_admit` も `OS32_ERR_FULL`、物理不足は
`EXEC_ERR_NOMEM` (`exec_status_t`)、ヘッダ不正は `EXEC_ERR_INVALID`。`exec_run` は
`exec_launch` の戻り値をそのまま返すので、CUI シェル側の負値の扱いは変わらない。

## RED の作り方 (実際に走らせた)

```bash
# exec/appslot.c の appslot_cpl0_admit() から判定行を外す (= 実装前の素通し)
sed -i 's/    if (appslot_live() > 0) return OS32_ERR_FULL;//' exec/appslot.c
python3 -B tools/tests/test_multiapp_impl.py
```

```
  FAIL 19g 走行中のアプリが 1 本でも居れば --cpl0 の子は ERR_FULL
  FAIL 19h 拒否で AppSlot は 1 つも変わらない
  FAIL 19i 拒否は claim も alloc もしない (帯を押さえない)
  FAIL 19j 拒否で資源の所有者も現在の ID も動かない
  FAIL 19k 拒否は池を消費しない (次の空きは 3 のまま)
  FAIL 19m park 中のアプリが 1 本でも居れば判定で弾かれる
  FAIL 19n park 中のアプリが 1 本でも居れば --cpl0 の子は ERR_FULL
  FAIL 19o park 中のアプリは印ごと無傷で、池も空いたまま
  FAIL 19p park 中でも拒否は帯を押さえない
  FAIL 19q 4 本生きていれば --cpl0 の判定でも弾かれる
FAILURES
```

RED では実際に「生きているアプリが居るのに `--cpl0` の子が立ち、帯 512 枚を claim して
池の ID 3 を消費する」ところまで進む — 症状 (a)(b) の再現そのもの。

**GREEN**: `python3 -B tools/tests/test_multiapp_impl.py` → EXIT=0、`ALL PASS` (137 検査)。

## 追加した検査 (ケース 19、26 検査)

ハーネス側に `exec/exec.c` の CPL=0 経路を**順番のまま**写した
`ma_start_cpl0()` / `ma_cpl0_claim()` / `ma_cpl0_release()` / `ma_exit_cpl0()` を置き、
「池の admit → `want_ring3` の判定 → cpl0 の admit → claim → commit」を通す。
拒否が claim より前にあることは、`H.free_pages` と `H.cpl0_children` が動かないことで見る。

| 節 | 性質 |
|---|---|
| 19a-e | 生存アプリ 0 本: 従来どおり ID 2 で立ち、帯を丸ごと claim / 終了で丸ごと返る |
| 19f-k | **走行中**のアプリ 1 本: `OS32_ERR_FULL`。AppSlot・空きページ・`cpl0_children`・資源の所有者・現在の ID・ID の池がどれも動かない |
| 19l-p | **park 中**のアプリ 1 本: 同じく `OS32_ERR_FULL`。park の印 (`parked_from_wait`) ごと無傷 |
| 19q-r | 4 本 (満杯) でも判定で弾かれる (`appslot_cpl0_admit` 単体で確認。`ma_start_cpl0` の戻りは池の枯渇と同じ `OS32_ERR_FULL`) |
| 19s-u | 全部閉じれば再び立つ (「GUI のアプリを閉じてから使えば足りる」が成り立つ) |
| 19v-x | **シェル (exec ネスト段 0) は対象外** — アプリが park 中でも `shell.bin` / `gshell.bin` の載せ替えは通り、帯も枚数も池も動かない |
| 19y-z | `--cpl0` **でない** CUI の入れ子 `exec_run` は従来どおり立つ (この変更で塞がっていない) |

## 実行コマンド

```bash
python3 -B tools/tests/test_multiapp_impl.py     # ケース 19 を追加 (137 検査)
make kernel                                      # EXIT=0
python3 -B tools/check_constraints.py            # EXIT=0
```

## この追記で**測っていないこと** ([V4])

- **ゲストは未検証**。NP21/W では 1 度も動かしていない (コーダーはエミュレータ禁止)。
  テスターの再配備待ち。`kernel.map` は動いている (`appslot_cpl0_admit` = 0x0012abf4)。
- 全体ゲート (`make all` / `make external` / `make check`) は回していない。通したのは
  `make kernel` と `test_multiapp_impl` / `test_multiapp_model` / `test_owner_reclaim` /
  `test_sbrk_tier` / `tools/check_constraints.py`。
- `make kernel` の警告: **`exec/exec.c` と `exec/appslot.c` からは 1 件も出ていない**。
  フルビルドのログには既存の警告 (`include/pc98.h` の `TVRAM_BPR` 再定義、
  `kernel/v86_bios.c` の `-Warray-bounds`、`kernel/console.c` の暗黙宣言など) が
  36 件残っているが、いずれも本変更の前からあるもので触っていない。
- **`--cpl0` で作られている在庫バイナリは 1 つも無い** (下記)。したがって「アプリを
  閉じてから使え」の実害はいまのところゼロだが、これは実機で確かめていない。

## `mkos32x --cpl0` は今どこで使われているか (依頼の列挙)

`--cpl0` を渡しているビルド規則は **リポジトリにも submodule にも 1 つも無い**。

| 場所 | 内容 |
|---|---|
| `sdk/mkos32x.py:190` | `--cpl0` → `OS32X_FLAG_FORCE_CPL0` (0x0004) をヘッダの `flags` に立てる。**唯一の生成側** |
| `build/programs.mk` / `userland/*/Makefile` | `--cpl0` を渡している行は無い (`ring3_hello` / `ring3_fault` / `hello_r3` / `faultprobe_r3` はいずれも CPL=3 側の検証用) |
| `apps/` (submodule) | `deploy.yaml` / `Makefile` / `app.conf` を含め `cpl0` の記述なし |
| `game/` (submodule) | 同上、記述なし |
| 消費側 | `exec/appslot.c:113` `appslot_launch_is_app()` (`want_ring3` を落とす) と `kernel/shlib.c:181` (CPL=0 プログラムは shlib の master をそのまま共有) |

つまり `--cpl0` は現在「手で `sdk/mkos32x.py --cpl0` を叩いたときだけ立つエスケープ
ハッチ」で、標準の配備物には 1 本も無い。gshell 配下でこの拒否に当たるのは、その手製
バイナリを GUI アプリが生きている間に起動したときだけ。

---

# 追記: 回 7 (ケース 20) — `exec_abort_clear` で CTRL+STOP の宛先を付け替える (KAPI v45)

対象票: [`docs/archive/gui_v13/TASK_K5B_gshell.md`](../../docs/archive/gui_v13/TASK_K5B_gshell.md)
「決裁が要る点 A1」/ **ユーザー決裁 2026-09-11 の A1**

| 回 | 対象 | RED | GREEN |
|---|---|---|---|
| 7 | CTRL+STOP の要求を降ろす口 (契約 T6) | (a) `appslot_abort_clear()` を消した実装前の `exec/appslot.c` では**ハーネスがコンパイルできない** (`gcc -Werror` が非ゼロ終了) / (b) 空実装 (`return 0;` だけ) にすると 20 検査中 **4 検査が FAIL** (20c / 20h / 20o / 20q) | `tools/tests/test_multiapp_impl.py` 156 検査 ALL PASS |

## 何が足りていなかったか

CTRL+STOP は IRQ1 (`drivers/kbd.c:235` → `ring3_abort_request` → `appslot_abort_request`)
の時点で **走っているアプリ** の `AppSlot.abort_req` を無条件に立てる。IRQ1 の時点で
カーネルが知っているのはそれだけだからで、宛先は選べない。ところが契約 T6 の宛先は
**フォーカス窓のアプリ**。4 本同時実行では両者が食い違い、WM (gshell) がフォーカス窓の
ID を `exec_kill` で畳むと、走っている本人の `abort_req` が立ったままなので

1. フォーカス窓のアプリ … `exec_kill` で畳まれる
2. 走っている本人 … 次の syscall 境界 (`ring3_syscall_dispatch` 入口の
   `ring3_abort_check`) で畳まれる

と **2 本死ぬ**。K5b-W は「フォーカス窓の owner が走っている本人のときだけ効かせる」で
凌いでいた (`multiapp::abort_targets_current`) が、それでも「走っている本人の要求が
立ったまま残る」穴は閉じていない (票 K5b-W の A1「残る穴」)。

## 直したもの

| 場所 | 変更 |
|---|---|
| `sdk/kapi.json` | 末尾にスロット 186 `exec_abort_clear` を追記 ([ABI2])、`version` 44 → 45 |
| `sdk/include/os32/os32_kapi_shared.h` | `KAPI_VERSION` 45 |
| `exec/appslot.c` / `.h` | `int appslot_abort_clear(void)` — 実体。owner 1 以外は `OS32_ERR_INVAL` |
| `exec/exec.c` / `.h` | `i32 exec_abort_clear(void)` — KAPI の口。`appslot_abort_clear()` へ委譲 |
| `build/app.conf` | `userland/gshell` の要求 KAPI 版 44 → 45 (他の行は触っていない) |
| `docs/KAPI_SPEC.md` | 表題 v45、§3-2 の予約表に v45 の行、§4 に `0x2F0 exec_abort_clear` とデータフィールドのオフセット繰り下げ (0x2F4 / 0x2F8) |

**最終署名**: `i32 exec_abort_clear(void)`

- `res_owner_get() != APP_ID_SHELL` (= `gui_register` と同じ判定) → `OS32_ERR_INVAL`。
- それ以外は、要求を負っているアプリの `abort_req` を降ろして **0**。要求が無ければ
  何もせず 0 (走っているアプリが 1 本も居ないときも 0)。
- 触るのは `abort_req` **だけ**。`state` / `in_op_wait` / `parked_from_wait` / ページ /
  資源の所有者 / 現在の ID はどれも動かさず、1 本も畳まない。
- 要求を負えるのは「走っている 1 本」だけなので対象は高々 1 本。ただし **WM が
  top-level (owner 1) に戻るのは park の後**なので、そのときスロットの状態は
  `APP_STATE_PARKED` になっている — 状態では絞らず「要求を負っている ID」で探す
  (ケース 20e/20f がこの順序を固定している)。

**WM 側 (W レーン) の使い方**: フォーカス窓の owner が走っている本人でなければ、
`exec_abort_clear()` で本人の要求を降ろしてから `exec_kill(フォーカス窓の ID)`。
どちらも WM top-level (owner 1) から呼ぶ — `exec_kill` の `appslot_kill_check` が
`g_cur == APP_ID_SHELL` を要求するので、もともと呼べるのはそこだけ。

## RED の作り方 (実際に走らせた)

```bash
# (a) 実装前 = 関数が無い状態。ハーネスがビルドできない。
#     exec/appslot.c から appslot_abort_clear() の定義を丸ごと削って
python3 -B tools/tests/test_multiapp_impl.py
#  → subprocess.CalledProcessError: gcc ... returned non-zero exit status 1

# (b) 口だけ在って中身が無い状態 (`return 0;` のみ、owner の判定も無し)
python3 -B tools/tests/test_multiapp_impl.py
#  → FAIL 20c owner 1 以外からは OS32_ERR_INVAL
#     FAIL 20h 要求が降りている
#     FAIL 20o 意図しない 1 本が死なない
#     FAIL 20q 要求が無いときは 1 本も動かさない
#     FAILURES
```

(b) の 20o が落ちるのが A1 の本体 —「降ろせないので、次の安全地点で
**意図しない 1 本が死ぬ**」がそのまま検査に出る。

## 追加した検査 (ケース 20、19 検査)

| 検査 | 内容 |
|---|---|
| 20a-b | 下ごしらえ: 4 本のうち 3 を起こし、CTRL+STOP を 3 に立てる (owner = 3) |
| 20c-d | **owner 1 以外からは `OS32_ERR_INVAL`** で、要求も降りない (`gui_register` と同じ判定) |
| 20e-f | park で WM top-level (owner 1) へ戻る。要求は **park をまたいで残る** |
| 20g-h | owner 1 から呼ぶと 0 を返し、`abort_req` が降りている |
| 20i-l | 降ろすだけ — 1 本も畳まず、別の ID (2 と 5) も対象の ID も `state` / `parked_from_wait` は無傷 |
| 20m-o | 起こし直して安全地点を通しても畳まれない (**A1 の残る穴がふさがる**) |
| 20p-q | 要求が無いときに呼んでも 0 で、1 本も動かさない |
| 20r-s | アプリが 1 本も居なくても 0 で、何も起きない |

## 実行コマンド

```bash
python3 sdk/gen_kapi.py && python3 sdk/kapi_rust_gen.py   # 再生成 ([ABI1])
python3 tools/check_kapi_version.py               # EXIT=0 (v45、関数表も一致)
python3 -B tools/tests/test_multiapp_impl.py      # ケース 20 を追加 (156 検査) ALL PASS
make kernel                                       # EXIT=0、-Wall の警告 0
```

## この追記で**測っていないこと** ([V4])

- **ゲストは未検証**。NP21/W では 1 度も動かしていない (コーダーはエミュレータ禁止)。
  「フォーカスが別アプリのときの CTRL+STOP でフォーカス窓だけが死ぬ」は実機の受入待ち。
- 全体ゲート (`make all` / `make external` / `make check`) は回していない (テスター担当)。
  通したのは `make kernel` と `check_kapi_version` / `test_multiapp_impl`。
  [ABI3] の `make clean` → `make all` も**回していない** (KernelAPI 構造体が伸びたので
  テスターが回す必要がある)。
- W レーン (gshell) はまだ `exec_abort_clear` を呼んでいない。`build/app.conf` の要求版を
  45 に上げただけで、`userland/gshell` のコードには 1 バイトも触っていない。

---

# 追記: 回 8 (ケース 4) — 3 領域の外で取る付随ページを勘定に入れる (K7)

票: [`docs/archive/gui_v13/TASK_K5B_kernel.md`](../../docs/archive/gui_v13/TASK_K5B_kernel.md) 「K7」

## 何が壊れていたか

PM 実測 (2026-09-11、NP21/W 8MB = `ExMemory 7`)。`gui_demo` の起動が

```
[shlib] no memory for 4 data pages
Error: shlib data attach failed (out of memory)
```

で落ち、`exec_sbrk_tier_last = 1` (= 段 1 を採っていた)。

`exec_ring3_pages()` が数えていたのは **3 領域 (本体+sbrk / exec_heap / スタック) + PD + アプリ PT**
だけで、その直後に同じ pgalloc から取る `shlib_addrspace_attach()` の `.data/.bss` 複製 (4 ページ) が
入っていなかった。8MB のアプリ帯の空きは

```
(MEM_APP_BAND_TOP - MEM_EXEC_LOAD_ADDR) / PAGE_SIZE = (0x800000 - 0x500000) / 4096 = 768
```

で、段 1 の枚数は

```
(RING3_HEAP_TOP - MEM_EXEC_LOAD_ADDR)/4096 - 1  (= 703 - 1、ガード 1 枚を除いた本体+sbrk+exec_heap)
+ RING3_USTACK_SIZE/4096 (= 64) + 1 (PD) + 1 (アプリ PT) = 768
```

= **空きとちょうど同じ**。段 1 が通ってしまい、3 領域を張り終えた時点で空きが 0、次の 4 ページが
取れなかった。`code_end` に依らず 768 になるので、8MB では shlib を使う CPL=3 アプリが必ずこうなる。

## 直したもの

| 場所 | 変更 |
|---|---|
| `kernel/shlib.{c,h}` | `shlib_data_pages()` — attach 1 回あたりの `.data/.bss` ページ数 (未ロードなら 0) |
| `exec/exec.c` | `exec_ring3_extra_pages()` を追加し、`exec_ring3_pages()` の合計に足す |

足す場所を `exec_ring3_pages()` 1 か所にしたので、`exec_sbrk_pick_tier()` の段 1 判定・段 2 の枚数・
`appslot_start_admit()` に渡す `need_pages`・`AppSlot.pages` のすべてに同時に効く。段 2 も同じ式なので、
sbrk を削って作った空きを使い切らない。shlib 未ロード (CUI だけの機械) では 0 なので K7 以前と同じ。

## RED の作り方 (実際に走らせた)

`tools/tests/sbrk_tier_host.c` にケース 4 を足し、`exec_ring3_extra_pages()` を **実装する前**に走らせる
(ホスト側の `shlib_data_pages()` スタブは在るが、切り出した `exec_ring3_pages()` がまだ呼ばない):

```
  FAIL 4b 付随ページはそのまま枚数に足される
  FAIL 4c 付随ページを足すと段 1 は 8MB の空きを超える
  FAIL 4d だから段 1 を選ばず段 2 へ倒す (K7 の本体)
  FAIL 4e 段 2 の sbrk 上端は従来どおり code_end + 最低分
  FAIL 4i 段 1 の枚数 (付随込み) なら admit は NOMEM
  FAILURES
```

このとき `4a` (段 1 の枚数 == 768) は **ok** で、不具合の算術そのものが試験に出ている。

## 追加した検査 (ケース 4、12 検査)

| 検査 | 内容 |
|---|---|
| 4a | 8MB の空き 768 に段 1 の 3 領域 + PD + PT が**ちょうど**収まる (不具合の正体) |
| 4b-4c | 付随 4 ページはそのまま枚数に足され、段 1 は 768 を超える |
| 4d-4e | だから段 1 を選ばず段 2 へ倒れ、sbrk 上端は `code_end + MEM_EXEC_SBRK_MIN` |
| 4f-4g | 段 2 は付随込みでも収まり、3 領域の後に 4 ページが残る |
| 4h-4i | `appslot_start_admit` は段 2 の枚数なら ID を返し、段 1 の枚数なら `EXEC_ERR_NOMEM` |
| 4j | 付随ページを含めても収まる空き (15MB 以上) なら従来どおり段 1 |
| 4k-4l | shlib 未ロードなら枚数も段の選択も K7 以前と同じ (回帰なし) |

判定関数は `test_sbrk_tier.py` が `exec/exec.c` から**テキストのまま**切り出す流儀のまま
(`WANTED` に `exec_ring3_extra_pages` を追加)。並行して書いた別式ではなく出荷するコードを見る。

## 実行コマンド

```bash
python3 -B tools/tests/test_sbrk_tier.py       # ケース 4 を追加 (39 検査) ALL PASS
python3 -B tools/tests/test_multiapp_impl.py   # ALL PASS (回帰なし)
python3 -B tools/tests/test_multiapp_model.py  # ALL PASS
python3 -B tools/tests/test_owner_reclaim.py   # OK
python3 -B tools/tests/test_pgalloc_model.py   # OK
python3 -B tools/tests/test_app_band_pde.py    # PASS
python3 -B tools/tests/test_paging_bounds.py   # PASS
python3 -B tools/tests/test_memory_boot.py     # OK
i386-elf-gcc <カーネルフラグ> -Wextra -c exec/exec.c kernel/shlib.c   # 警告 0
```

## この追記で**測っていないこと** ([V4])

- **ゲストは未検証**。NP21/W では 1 度も動かしていない (コーダーはエミュレータ禁止)。
  8MB で `gui_demo` が実際に立つこと (受入 G6) は実機待ち。
- `make` を 1 度も回していない (コーダー禁止)。通したのは上のホスト試験と、
  `exec/exec.c` / `kernel/shlib.c` を単体でクロスコンパイラに通したことだけ。
- 段 2 へ倒れた分だけ 8MB のアプリの sbrk は 256KB に固定される。その狭さで
  `gui_demo` が足りるかは実機でしか分からない。
