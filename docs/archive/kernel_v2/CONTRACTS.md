# v2 リング3 実装 — 凍結インターフェース契約

> 発行: PM (2026-09-01) / 状態: **完了記録 (2026-09-03)**
> 策定: 2026-09-01 / 親: [PLAN.md](PLAN.md)
> 位置づけ: **実装中は動かさない約束**。複数コーダーがこの契約に対して
> 独立に実装できるようにするための凍結点。変更には PM 承認と全レーンへの
> 周知を要する (勝手に変えると他レーンのビルドが黙って壊れる)。

体制: コーダー1 = リング3 背骨 (直列)、コーダー2 = Rust GUI (独立)、
検証モデル = os32-cycle 実行のみ、PM = 契約維持 + 検証ツール作成 + gate。
根拠設計: [M1_RING3.md](M1_RING3.md) [M2_KAPI_TRAMPOLINE.md](M2_KAPI_TRAMPOLINE.md)
[M3_VERIFY.md](M3_VERIFY.md)。

---

## C1. セレクタと GDT (M1)

| idx | selector | access | DPL | 用途 |
|-----|----------|--------|-----|------|
| 0 | 0x00 | - | - | null (既存) |
| 1 | 0x08 | 0x9A | 0 | `KERNEL_CS` (既存) |
| 2 | 0x10 | 0x92 | 0 | `KERNEL_DS` (既存) |
| 3 | 0x18 | 0x89 | 0 | TSS (既存, `GDT_TSS_IDX`, **動かさない**) |
| 4 | 0x23 | 0xFA | 3 | `USER_CS` (= 0x20 \| RPL 3) ★M1 追加 |
| 5 | 0x2B | 0xF2 | 3 | `USER_DS` (= 0x28 \| RPL 3) ★M1 追加 |

- `GDT_ENTRIES` 4 → 6。user は TSS の後ろ (idx 4/5) に足す (TSS 番地を動かさない)
- リミット/粒度は既存 code/data と同じ `0xFFFFFFFF` / `0xCF` (フラット)
- 保護はページ単位 (`PTE_USER`)。セグメントはフラットのまま

## C2. メモリ共有境界 (D6 / M1)

| 領域 | 共有 | PTE | CPL=3 |
|------|------|-----|-------|
| 0x100000-0x3FFFFF (カーネル/SQLite/シェル) | 全 PD 共有 | RW | 不可 (USER なし) |
| `MEM_SHM_BASE` (256KB) | 全 PD 共有 | RW+USER | 可 (アプリ間受け渡し) |
| ホットデプロイ窓 (物理末尾 256KB) | 全 PD 共有 | RW | 不可 |
| VRAM 0xA8000 (グラフィック) | 全 PD 共有 | RW+USER | 可 (直接描画) |
| 0x400000 帯 (code/data/heap) | **PD ごと** | RW+USER | 可 |
| ユーザスタック (0x400000 帯上端) | **PD ごと** | RW+USER | 可 |

- 全 PD で `0x100000-0x3FFFFF` は**同一 PT を指す** (共有し忘れると即クラッシュ)
- `0x400000` 帯の PDE だけアプリ固有 PT に差し替え

## C3. USER トランポリンページ (M2)

- 1 枚の全 PD 共有 USER ページ。`RX+USER` (実行可・**書込不可**)
- レイアウト (KernelAPI 構造体と同一オフセット):

```
0x00  magic   (u32)  本物と同じ値
0x04  version (u32)  本物と同じ値
0x08  fn[0]   (u32)  = STUB_BASE + 0*8
0x0C  fn[1]   (u32)  = STUB_BASE + 1*8
...   fn[i]          = STUB_BASE + i*8
STUB_BASE:            スタブ列 (8 バイト/個)
  B8 <slot:imm32>     mov eax, slot
  CD 80               int 0x80
  C3                  ret
  (計 8 バイト)
```

- スタブは固定テンプレート。per-slot コード生成は不要 (カーネルが slot だけ差し替え)
- 168 スロットで table 688B + スタブ 1344B ≈ 2KB、1 ページに収まる

> **実装 (2026-09-03):** `STUB_BASE = page + align4(sizeof(KernelAPI))` (= 688)。
> 当初式 `align4(8 + 4*KAPI_FUNC_COUNT)` (= 680) は KernelAPI 末尾の**データ
> フィールド 2 本** (`sbrk_heap_limit` / `shm_base`, fn 表直後 offset 680/684) を
> 見落としており、スタブがデータフィールドと衝突した。struct 全体の後ろに
> 置くよう補正。`exec/exec.c: ring3_trampoline_init()`。

## C4. int 0x80 ABI (M2)

- IDT ベクタ **0x80**、ゲート DPL=3。他の例外/IRQ ゲートは DPL=0 のまま
- 入口: `eax` = slot 番号
- **範囲チェック**: `slot >= KAPI_FUNC_COUNT` は即 kill
- 引数: ユーザスタック (cdecl)。カーネルが `kapi_argsize[slot]` バイトを
  カーネルスタックへコピーして `wrap_<slot>` を call
- 戻り値: `eax` に格納して iret
- **ポインタ引数の保護をここで一元化** — これが [ABI4] 解消の実装点

> **実装 (2026-09-03):** 純粋な範囲検証だけでは (a) 可変長引数 (`kprintf` の
> `%s` ポインタ) を守れず、(b) wrap 実行中に #PF を longjmp で抜けるとカーネル
> 状態不整合の恐れがあるため、**2 段構え**にした:
> - **(核) ring3 syscall フォールトガード**: dispatcher がユーザメモリに触れる
>   前後を `ring3_in_syscall` フラグで囲む。この間の #PF/#GP は wrap 内 (CPL=0)
>   でも `ring3_fault_kill` でアプリだけ kill (`isr_handlers.c` の CPL=3 判定を
>   `CS.RPL==3 || ring3_in_syscall` に拡張)。可変長 %s も含め全ポインタ deref を捕捉。
> - **(補助) 早期範囲検証**: `kapi_argptr[slot]` の立つ固定ポインタ引数を
>   `ring3_ptr_ok` (アプリ帯 [0x400000,0x7C0000)/スタック/SHM/VRAM/NULL) で
>   wrap 前に検査し範囲外は kill。よくある不正ポインタを入口で弾き不整合を軽減。
> `exec/exec.c: ring3_syscall_dispatch()` / `kernel/isr_handlers.c`。

## C5. 生成器の出力形式 (sdk/gen_kapi.py, M2)

既存の生成物 (struct / wrap / init / rust / argsize は無かった) に追加:

| 記号 | 型 | 内容 |
|------|-----|------|
| `KAPI_FUNC_COUNT` | マクロ | 関数スロット数 (現 **168**) |
| `kapi_argsize[KAPI_FUNC_COUNT]` | `u16[]` | 各スロットの引数バイト数。型は 4B 換算、可変長 (`...`) は固定分のみ |
| `kapi_argptr[KAPI_FUNC_COUNT]` | `u16[]` | 各スロットの固定引数のうちポインタ型のビットマスク (bit k = 引数 k がポインタ)。早期範囲検証 (C4) が使う |

- append-only ([ABI2]) を維持。新スロットは配列末尾に増える

> **実装 (2026-09-03):** `kapi_argptr` は M2e で追加 (当初 C5 は argsize のみ)。
> 生成は冪等・rust/inc に差分なし。`kapi.json` の型に `*` を含む固定引数の
> ビットを立てる。`sdk/gen_kapi.py`。

## C6. 検証シンボルと契約 (M3)

| 記号/契約 | 定義 |
|-----------|------|
| `fault_kill_count` | `u32`。CPL=3 アプリをフォールトで kill した回数。`emu_read_mem` で読む |
| `os32-cycle fault-test` | `RESULT: OK/FAIL` を 1 行返す (M0 の RESULT 規約と同じ) |
| `faultprobe` | 各ケース後の回復を SHM か既定番地に記録 |

---

## C7. 衝突ゾーンの所有権

| 所有 | ファイル |
|------|----------|
| コーダー1 (背骨) 排他 | `kernel/gdt.c` `kernel/paging.c` `kernel/pgalloc.c` `kernel/idt.c` `exec/exec.c` `sdk/gen_kapi.py` `kernel/*ring3*` (新規) |
| コーダー2 (GUI) 排他 | `userland/rust/libos32gui/**` (新規) |
| PM 作成 | `tools/check_privileged.py` `userland/tests/faultprobe/**` |

- 他レーンが排他ファイルへの配線を要するときは **PM 経由で背骨に依頼**
- `exec.c` / `idt.c` は M1 と M2 の両方が触るが、いずれもコーダー1 の排他なので
  同時編集は起きない

---

## C8. Rust GUI の依存方針 (D / コーダー2)

**libos32gui は外部クレートを入れず、描画は既存 libos32gfx を FFI で叩く。**
2026-09 に Web 調査済み: no_std の retained-mode ウィジェット OSS で採用に
足るものは無く (`embedded-gui` は作者自ら experimental/"bad" と明記)、
`embedded-graphics` は成熟しているが OS32 の libos32gfx (スプライト・ベジェ・
ダーティ矩形・ページフリップ、PC-98 調整済み) と重複し統合面で劣る。

- **依存に加えてよいクレート: なし** (`os32_lz4` と同じく std/alloc なしで自作)
- 描画プリミティブ: `libos32gfx` を `extern "C"` で呼ぶ。Rust 側で再実装しない
- コーダー2 が作るのは libos32gfx に無い層のみ:
  ウィンドウ管理 / Z オーダー / メッセージディスパッチ / ウィジェット
- 再発明を避ける相手は外部 OSS ではなく in-repo の libos32gfx (メモリ
  os32-no-wheel-reinvention の原則)

---

## 参照

- 親計画: [PLAN.md](PLAN.md)
- 各段設計: [M1_RING3.md](M1_RING3.md) [M2_KAPI_TRAMPOLINE.md](M2_KAPI_TRAMPOLINE.md) [M3_VERIFY.md](M3_VERIFY.md)
- 現状の定数: `include/memmap.h`、`sdk/include/os32/os32_kapi_shared.h` (`KAPI_VERSION`, `KAPI_ADDR`)
- 消える制約: `docs/CONSTRAINTS.md` [ABI4] (M3 完了で削除)
