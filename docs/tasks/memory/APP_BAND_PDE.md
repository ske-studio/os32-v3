# アプリ帯の可変 PDE 化 — 実装票

> 状態: **受入待ち (2026-09-10〜)** — 実装 `b8dab24` は v2.1 に入り、`paging_app_band_selftest` は kselftest として毎起動で走る (2026-09-29 に確認)。**§5 のゲスト受入 (8MB / 15MB の両構成、`heap_test` の overlap、`ring3_guard` の否定試験) は記録が無いまま** — v2.1.x 保守の残件。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (2026-09-10) / それまでの状態: **受入待ち (2026-09-10)**

実装は `b8dab24` で着地 (ホスト TDD は `make check` の `check-memory-host`)。§5 のゲスト受入は未記録。
担当: コーダー (Opus 5、worktree 隔離) / 検証: PM + ローカル AI

## 1. 何を解くのか

**1 アプリに渡せるメモリが、搭載 RAM に依らず約 2.5MB で頭打ち**になっている。
原因は構造であって空き RAM 不足ではない。

x86 32bit ページングでは 1 つの PDE が 4MB を覆う (PDE 番号 = 仮想アドレス >> 22)。
OS32 は **PDE 1 (`0x400000`-`0x7FFFFF`) の 1 枚だけ**をアプリごとに差し替え、
残り 1023 本は master の写しを共有している (`kernel/paging.c:561-566`)。
その 1 枚に共有ライブラリ帯 (1MB)・プログラム本体・sbrk・exec_heap・
CPL=3 スタック (256KB) が全部載る。

実測 (2026-09-10、15MB 構成、`install.bin` 相当):
`RING3_HEAP_TOP` 0x7BF000 − 本体末尾 − `MEM_EXEC_SBRK_MIN` 256KB − ガード
= **avail 2,605,056 バイト**。8MB 構成でも 15MB 構成でも同じ値になる。

**目標**: 要求量に応じてアプリ固有 PDE を 4MB 単位で増やし、空き RAM がある限り
渡す。足りなければ拒否する (`a6b2461` で入れた振る舞いを維持)。

## 2. 現行の不変条件 (壊してはいけない)

`kernel/paging.c:63-67` の 5 本の `STATIC_ASSERT` が現在の前提を固定している。

```c
STATIC_ASSERT(APP_BAND_PDE == (MEM_APP_BAND_BASE >> 22), app_band_pde_matches);
STATIC_ASSERT(APP_BAND_PDE < PAGING_PT_COUNT, app_band_pde_in_range);
STATIC_ASSERT((MEM_SHLIB_BASE >> 22) == APP_BAND_PDE, shlib_base_in_app_band);
STATIC_ASSERT((MEM_EXEC_LOAD_ADDR >> 22) == APP_BAND_PDE, exec_load_in_app_band);
STATIC_ASSERT(((MEM_APP_BAND_TOP - 1) >> 22) == APP_BAND_PDE, app_band_top_in_pde);
```

- **共有ライブラリ帯 `0x400000`-`0x4FFFFF` は先頭 PDE に居続ける**。
  `.text` は全 PD 共有・read-only、`.data`/`.bss` はアプリごと (K3/C3)。
- **`paging_map_phys()` は master の `page_tables[]` に書く**。アプリ固有 PDE の
  範囲にこれを使っても走行中のアプリからは見えない。アプリ AS への写像は
  `paging_addrspace_map_user*()` を通すこと。
- **USER は当該アプリの PD の PDE にだけ伝播**させる。master 側の PDE には
  立てない (実効権限は PDE ∧ PTE、`addrspace_map_user_page`)。
- PD / アプリ PT のバッキングは identity で読める領域から取る
  (`kernel/paging.c:488-496` の前提)。

## 3. 変更の範囲

| ファイル | 変更 |
|---|---|
| `kernel/paging.h` | `struct addrspace` に PDE 枚数と PT 物理配列を持たせる。`APP_BAND_PDE` は「先頭」の意味に変える |
| `kernel/paging.c` | `paging_addrspace_create` に枚数を渡す。`destroy` は全 PT を解放。`addrspace_map_user_page` / `_range` の判定を「先頭 ≤ pdi < 先頭+枚数」に。`STATIC_ASSERT` を新しい不変条件に書き換え |
| `exec/exec.c` | ヘッダの `heap_size` + 本体 + sbrk + スタックから必要枚数を算出し、AS 作成時に渡す。`RING3_USTACK_TOP` / `RING3_HEAP_TOP` を枚数から導く |
| `include/memmap.h` | `MEM_APP_BAND_TOP` を「既定の」上端に格下げし、最大枚数の定数を足す |
| `kernel/kselftest.c` | 複数 PDE の AS で USER 写像と権限が正しいかの項目を追加 |

## 4. 決めること (実装者が票に書き戻す)

1. **最大枚数**。上の共有帯とぶつからないこと。デバイス窓は Cirrus リニア窓
   `0x1000000` (PDE 4)、PEGC リニア窓 `0xF00000` (PDE 3 の中) にある。
   したがってアプリ帯を伸ばせるのは **PDE 1〜2 (`0x400000`-`0xBFFFFF`) が安全**で、
   PDE 3 まで伸ばすと PEGC の窓と衝突する。**まず最大 2 枚**を推奨する。
2. **PT のバッキングをどこから取るか**。枚数分の PT (4KB × 枚数) がアドレス空間ごとに
   要る。identity で読める領域から取る前提を保つこと。
3. **枚数が減る方向の再利用**。`exec_heap_restore_state()` で親へ戻るときの扱い。
4. **CPL=0 の子** (`--cpl0`) は master AS のままなので影響を受けないことの確認。

## 4-A. 決めたこと (コーダー書き戻し、2026-09-10)

実装は worktree `agent-ad3a748e21fd338e3` の作業ツリー。コミット・配備はしていない。

1. **最大枚数 = 2** (`MEM_APP_BAND_MAX_PDES`)。帯は `0x400000`-`0xBFFFFF`。
   票の推奨どおり。天井の根拠を定数にした: `MEM_APP_BAND_DEVICE_FLOOR`
   (= `0x00F00000`、PEGC のリニア窓)。`kernel/paging.c` の
   `STATIC_ASSERT(MEM_APP_BAND_MAX_TOP <= MEM_APP_BAND_DEVICE_FLOOR)` で
   はみ出しを止め、値が `include/pegc.h` の `PEGC_LINEAR_BASE` と一致することは
   `gfx/backend_pegc.c` 側の `STATIC_ASSERT` が検査する ([C4] 三層定数。
   memmap.h から pegc.h を読むと 9821 の事情が core に漏れるので分けた)。
   併せて `MEM_APP_BAND_MAX_TOP <= PAGING_BOOT_MAP_SIZE` も固定した — 帯が
   静的 bootstrap PT の外へ出ると master 側 `page_tables[]` が NULL になりうる。

   **枚数を増やす条件は「OS32X ヘッダの `heap_size` を明示していて、それが
   1 枚に収まらない」ときだけ**。`heap_size == 0` は必ず 1 枚を返す
   (`paging_app_band_pdes()`)。これが「枚数 1 で現行と完全に同じレイアウト」を
   一番安く保証する線引きで、既存バイナリは 1 バイトも動かない。

2. **PT のバッキングは従来どおり `pgalloc_alloc_page()`**。identity で読める
   領域から取る前提はそのまま (PD 1 ページ + PT 枚数ぶん)。ただし帯が伸びた
   ぶん、**master が使う側の下限を上げた**:
   - `kernel/paging.c` `reserve_table()` の走査開始を `MEM_APP_BAND_TOP` →
     `MEM_APP_BAND_MAX_TOP` に。据え置くと master の動的 PT が
     `0x800000`-`0xBFFFFF` に落ち、2 枚使うアプリがそれを USER で恒等マップして
     自分のページテーブルを書き換えられる (= 任意物理への読み書き)。
   - `kernel/pgalloc.c` の model workspace 下限、`kernel/memory_boot.c` の
     model/legacy 切り替えしきい値も同じ理由で `MEM_APP_BAND_MAX_TOP` に。
     15MB 構成は従来どおり model (workspace 0xEFE) のまま。12MB 前後の構成は
     legacy allocator に落ちるようになった (8MB が既にそうなっていたのと同じ道)。

   帯を伸ばしてよい物理上限は **子プロセスの claim 範囲 A の末尾** にした
   (`ring3_band_ram_top()` = `exec_child_claim()` の A 末尾)。そこまでは exec が
   `pgalloc_mark_used` で予約するので、アプリのヒープと pgalloc の動的確保
   (V86 バッキング / PD / PT) が同じページを二重に使うことがない。15MB では
   a_end = 0xDBF000 なので 2 枚に届き、12MB 以下では 1 枚のまま。

3. **枚数が減る方向**: アドレス空間は `exec_exit` / `ring3_fault_kill` /
   `exec_launch_abort` で毎回破棄されるので、枚数は AS の寿命に閉じている。
   帯の上端は `exec/exec.c` のモジュール変数 `g_ring3_band_top` が持ち、
   AS を壊す 3 か所と、帯を広げた後に `EXEC_ERR_NOMEM` で戻る 2 か所で
   `ring3_band_set(1)` により既定へ戻す。`exec_heap_restore_state()` には
   触っていない (親は CPL=0 のシェルか CPL=0 の子で、アプリ帯を使わない)。
   CPL=3 のネストは M1 の時点で単一 AS 前提のままで、そこは変えていない。

4. **CPL=0 の子 (`--cpl0`)** は `want_ring3 == 0` なので `g_ring3_band_top` に
   触れず、`heap_top_cpl0` (mem_end 由来) を使う従来の経路のまま。AS も作らない
   (master のまま) ので影響を受けない。シェル (Level 0) も同様。
   唯一の共通変更は読み込み上限 `max_size` で、CPL=3 側の見積もりを最大枚数に
   広げた結果 `read_top = min(heap_top_cpl0, RING3_HEAP_TOP_MAX)` になった。
   8MB 構成では `heap_top_cpl0` の方が小さいので値は従来と同じ。

### 4-B. 積み残し (PM 判断待ち)

- **`EXEC_DYN_RESERVE` の穴とアプリ帯の重なりは既存の問題として残る**。
  8MB 構成では今でも claim 範囲 A の末尾 (a_end) が `RING3_HEAP_TOP` (0x7BF000)
  より下にあり、穴 [a_end, guard_b) がアプリの exec_heap と重なっている。
  今回は帯を伸ばす条件を a_end で切ることで**新しい重なりを作らない**ようにした
  だけで、8MB の既存の重なりは直していない (票 §6 の範囲外と判断)。
- ゲスト検証 (kselftest / `ring3_guard` 否定試験 / `heap_test` overlap /
  8MB・15MB) は一切していない。票 §5 のとおり PM の担当。

## 5. 受入条件

- ホスト TDD を先に書く (RED → GREEN)。`tools/tests/` に `*_host.c` + `test_*.py` +
  `*_tdd.md` の 3 点セット。`make check` に登録する
- 枚数 1 のときに**現行と完全に同じレイアウト**になること (回帰ゼロ)
- 枚数 2 で `heap_size` 4MB 超の要求が通ること。空き RAM が足りなければ
  `EXEC_ERR_NOMEM` で拒否されること (`a6b2461` の振る舞いを維持)
- アプリ固有 PDE の外 (共有帯) に USER が漏れないこと。master から見た実効権限が
  supervisor のまま保たれること
- ゲスト検証は PM が行う: kselftest、`emu_agent` 回帰 6/6、`ring3_guard` の
  否定試験 (guard / shlib / pegc / cirrus)、`heap_test` の overlap check、
  8MB / 15MB の両構成

## 6. やらないこと

- スワップは採らない (速度面の不利が大きすぎる。2026-09-10 ユーザー判断)
- 共有ライブラリ帯の位置は動かさない
- CPL=0 の子のレイアウトは変えない
