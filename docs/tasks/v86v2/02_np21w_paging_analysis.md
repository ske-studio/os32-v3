# 02. NP21/W のページング保護実装の検証 (Phase 0-3)

> 実施日: 2026-08-08
> 対象: `~/np21w-src`（ai-debug fork）の i386c CPU コア
> 目的: 前回「NP21/W が PTE の U/S ビットを無視する」とされた主張の事実確認

---

## 1. 結論

**前回の主張「NP21/W の CPU エミュレータが PTE の U/S ビットチェックを正しく行わない」は、
現行ソース上では裏付けられない。U/S チェックは正しく実装されている。**

観測された「V86 (Ring3) がカーネルコード領域を実行できてしまう」現象は、
**OS32 側が自分でカーネルのページに `PTE_USER` を付けていた**ことで完全に説明できる。

> ⚠️ **重要な留保**: 本調査が読んだのは **2026-08 時点の np21w-src** である。
> 前回の作業は 2026-05 であり、その間にエミュレータ側が修正された可能性は否定できない
> （後述 §5 のとおり、該当箇所には修正の痕跡と読めるコメントがある）。
> **「前回の観測が間違いだった」と断定はできない。実験で確かめる**（§6）。

---

## 2. 検証 1 — ページウォークの U/S チェック

`src/i386c/ia32/paging.c:811-828`（4KB ページ経路）

```c
/* make physical address */
paddr = (pte & CPU_PTE_BASEADDR_MASK) + (laddr & CPU_PAGE_MASK);

bit  = ucrw & (CPU_PAGE_WRITE | CPU_PAGE_USER_MODE);
bit |= (pde & pte & (CPU_PTE_WRITABLE | CPU_PTE_USER_MODE));   /* ★ PDE と PTE の AND */
bit |= CPU_STAT_WP;

if (!(page_access_bit[bit]))        /* または !(page_access & (1 << bit)) */
{
        VERBOSE(("paging: page access violation."));
        err = 1;
        goto pf_exception;           /* → #PF */
}
```

- `ucrw` 側のアクセス属性（`CPU_PAGE_WRITE` = 1<<0, `CPU_PAGE_USER_MODE` = 1<<3）と
  ページ側の属性（`CPU_PTE_WRITABLE`, `CPU_PTE_USER_MODE` = 1<<2）、および `CPU_STAT_WP` を
  合成した 5 ビットのインデックスで `page_access` テーブルを引く、**テーブル駆動の正攻法**。
- **`pde & pte`** で PDE と PTE の論理積を取っている。これは i386 の正しい挙動
  （実効権限は PDE と PTE の AND）。
  前回「PDE USER=1 時に PTE USER も要求される」と**制約扱い**されていたが、
  これは**制約ではなく正しい仕様どおりの動作**である。
- 違反時は `CPU_CR2 = laddr` を設定し `err` に W/R・U/S ビットを組み立てて `PF_EXCEPTION` を送出
  （`paging.c:838-845`）。

## 3. 検証 2 — TLB ヒット経路でも権限を再チェックしている

`src/i386c/ia32/paging.c:950-963`

```c
if ((laddr & TLB_TAG_MASK) == TLB_GET_TAG_ADDR(ep)) {
        bit  = ucrw & (CPU_PAGE_WRITE|CPU_PAGE_USER_MODE);
        bit |= ep->tag & (CPU_PTE_WRITABLE|CPU_PTE_USER_MODE);
        bit |= CPU_STAT_WP;
        if (page_access_bit[bit]) {
                if (!(ucrw & CPU_PAGE_WRITE) || TLB_IS_DIRTY(ep)) {
                        return ep;           /* 権限 OK のときだけヒット扱い */
                }
        }
}
return NULL;                                 /* → フルページウォーク → #PF */
```

TLB キャッシュで権限チェックを素通しする、という典型的な手抜きも**していない**。

## 4. 検証 3 — CPL の追随と、V86→Ring0 割り込み時のプッシュ順序

`CPU_STAT_USER_MODE` はキャッシュ値で、更新箇所は 3 か所のみ（`ia32.c:326`, `system_inst.c:1265,1298`）。

```c
/* src/i386c/ia32/ia32.c:320-327 */
void CPUCALL set_cpl(int new_cpl) {
        int cpl = new_cpl & 3;
        CPU_STAT_CPL = (UINT8)cpl;
        CPU_STAT_USER_MODE = (cpl == 3) ? CPU_MODE_USER : CPU_MODE_SUPERVISER;
}
```

`set_cpl()` は `load_cs()`（`segments.c:149`）から呼ばれる。

**当初立てた仮説**: 「割り込み配送でカーネルスタックへプッシュする際、まだ CPL=3 のままなら
プッシュがユーザモードアクセス扱いになり、カーネルスタックに `PTE_USER` が無いと #PF → #DF →
トリプルフォルトになるのではないか」

**→ 否定された。** V86→Ring0 の割り込み配送は `exception.c:559-570` で
**`load_cs()`（＝`set_cpl(0)`）を先に実行してからプッシュしている**:

```c
load_ss(ss_sel.selector, &ss_sel.desc, cs_sel.desc.dpl);
CPU_ESP = new_sp;

load_cs(cs_sel.selector, &cs_sel.desc, cs_sel.desc.dpl);   /* ← ここで set_cpl(0) */
CPU_EIP = new_ip;

if (is32bit) {
        if (CPU_STAT_VM86) {
                PUSH0_32(CPU_GS);                          /* ← プッシュはこの後 */
                PUSH0_32(CPU_FS);
                PUSH0_32(CPU_DS);
                PUSH0_32(CPU_ES);
                ...
```

したがって**カーネルスタックへのプッシュはスーパバイザアクセスとして行われる。
割り込み配送のために `PTE_USER` は不要**。

（参考: コールゲート経路 `ctrlxfer.c:750` は逆にプッシュが先だが、
`rv = (cs_sel->desc.dpl == 3) ? CPU_MODE_USER : CPU_MODE_SUPERVISER;` と
**遷移先の特権レベル**を明示的に渡しており、これも正しい。）

---

## 5. では、なぜ「V86 がカーネルを実行できた」のか

**OS32 側が自分でカーネルのページを user アクセス可にしていたから。**

前回のシリアルログ（`/mnt/c/os32/os32_serial_log.txt`）に証拠が残っている:

```
[V86-DIAG] ESP0 PDE=0x0014A027 PTE=0x00169067 PWU
[V86-DIAG] IPL  PDE=0x0014A027 PTE=0x0051F007 PWU
[V86-DIAG] IDT  PDE=0x0014A027 PTE=0x00148067 PWU
[V86-DIAG] PG0  PDE=0x0014A027 PTE=0x00500007 PWU
[V86-DIAG] BDA  PDE=0x0014A027 PTE=0x00500007 PWU
```

- PTE `0x...067` → `0x67` = P(1) | RW(2) | **US(4)** | A(0x20) | D(0x40)
- PDE `0x...027` → `0x27` = P | RW | **US** | A
- 表示の `PWU` は Present / Writable / **User**

**カーネルスタック (ESP0) と IDT のページに U/S ビットが立っている。**
`pde & pte` の両方に USER があるので、V86 (Ring3) からのアクセスは**正当に許可される**。
エミュレータのバグではなく、設定どおりの動作である。

そしてこれは意図的だったことが `V86_STATUS.md` §9.3 に記録されている:

> | PDE USER=1 時に PTE USER も要求 | IDT/TSS/ISR領域に PTE_USER がないとトリプルフォルト |
> | **カーネル帯 (0x110000-0x16FFFF) に PTE_USER を付与** |

### 循環していた構造

```
トリプルフォルトが出る
   ↓
回避策としてカーネル帯に PTE_USER を付与
   ↓
V86 がカーネルメモリを実行できるようになる
   ↓
「エミュレータが U/S を無視している」と診断
   ↓
その前提でカーネルを 0x110000 へスライド（A20 用に 64KB を犠牲に）
   ↓
A20 の PTE リマップも失敗 → リバート4連発
```

**最初の「トリプルフォルトが出る」の真因が特定されないまま、回避策の上に診断が積み上がっていた。**

---

## 6. 再挑戦への影響

| 項目 | 前回 | 今回の方針 |
|---|---|---|
| カーネルロードアドレス | `0x110000` へスライド | **`0x100000` のまま**（main の現状を維持）。スライドは根拠を失った |
| カーネル帯の `PTE_USER` | 付与していた | **付与しない**。user にするのは V86 ゲストの 1MB と VRAM のみ |
| Phase 4「PTE U/S 準拠化」 | 必須と想定 | **不要の可能性が高い**。実験で確認してから判断（優先度を下げる） |
| A20 の PTE リマップ | 「エミュレータが非対応」で断念 | 前提が崩れたので**再評価する** |

### 残る最大の未解決問題

**「カーネル帯に `PTE_USER` を付けないとトリプルフォルトになる」のは、なぜだったのか。**

§4 のとおり割り込み配送は正しくスーパバイザで行われるので、`PTE_USER` は不要なはず。
にもかかわらずトリプルフォルトになったのなら、原因は別にある。候補:

1. OS32 側が V86 セッション用に**PDE を USER 化したことで、同じ PDE 配下の他のページの実効権限が
   変わった**と誤解し、実際には別の要因で落ちていた
2. `01_prior_session_analysis.md` §6 の**未解明な決定論的 #PF (`addr=0x800166FD`)** が本体で、
   それが #DF → トリプルフォルトに発展していた
3. 2026-05 当時の np21w のバージョンには実際にバグがあり、その後修正された

→ **Phase 1 で、カーネル帯に `PTE_USER` を付けずに V86 遷移を行い、実際に何が起きるかを見る。**
MCP でフォルト地点を凍結できるようになっていれば（Phase 0-0）、今度は真因まで辿れる。

---

## 7. 参照

- `~/np21w-src/src/i386c/ia32/paging.c:750-845`（`paging()`）, `:937-966`（`tlb_lookup()`）
- `~/np21w-src/src/i386c/ia32/paging.h:126-133`（`ucrw` フラグ定義）
- `~/np21w-src/src/i386c/ia32/ia32.c:320-327`（`set_cpl`）
- `~/np21w-src/src/i386c/ia32/segments.c:137-151`（`load_cs`）
- `~/np21w-src/src/i386c/ia32/exception.c:485-575`（V86→Ring0 割り込み配送）
- `~/np21w-src/src/i386c/ia32/ctrlxfer.c:745-830`（コールゲート経路）
- `archive/feat-vdm:docs/V86_STATUS.md` §9.2/§9.3（前回の「NP21/W 固有の制約」）
- `docs/tasks/v86v2/01_prior_session_analysis.md` §6, §7
