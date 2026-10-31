# kdebug/recover_wire.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: REALHW_RECOVER_ADDENDUM.md・RECOVER_ROUTE_TEST.md・RECOVER_T4B_INJECT.md (D1 で移した)
ソース: `kdebug/recover_wire.h`

## REALHW_RECOVER_ADDENDUM

- shell共有opは3=status・4=now、kdebug_callの既存slotを使いC89の専用共有ヘッダに40 byte要求/結果を置く；PAUSEDはnowのみ。snapshot/armの別コマンドは設けずtrial_beginから自動実行する。

## RECOVER_ROUTE_TEST

新しい private op `KDEBUG_OP_RECOVER_ROUTE_TEST` (12U、3/4/5〜10/11 と衝突しない) に既存の 40B KdebugRecover を使う。サブコマンド `recover route-test 0 --approve` が承認そのもの (`--approve` 無しは usage)。

- shell の専用形の session は文字列 `0` のみを受け付ける。入口関数の宣言は private wire ヘッダに置く (recover.h は宣言の追加だけでも hwdev 契約 CRC が変わるため不変とする)。

## RECOVER_T4B_INJECT

- `recover_wire.h` に私有 op `KDEBUG_OP_RECOVER_VIRTUAL=11`（既存 GF 5〜10 の後）と版付き 40B 要求を追加。既存 KAPI・KdebugRecover・slot の変更なし。

```c
typedef struct KdebugRecoverVirtual {
    u32 version, action, session, case_id, reserved[6]; /* 40B、reservedは0 */
} KdebugRecoverVirtual;
/* begin: configure → bind_plan → trial_begin(p, hash, p->trial_limit)
 * step: trial_step(owner, 保存token, true, virtual_operation, context)
 * end: trial_end(owner, 保存token, REC_NORMAL); now は既存口を使う。 */
```

- [C4]：wire 定数は subsystem の recover_wire.h、共通上限は recover.h、仮想 case・秒数・poll 値は recover_virtual.c の名前付き local 定数。Shared Layer 追加なし。

- 私有 wire の不正は `KDEBUG_RECOVER_VIRTUAL_INVALID=-1`、形式が正しい未実装要求は `KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED=-2` とし、KDEBUG=0 の既存 NOSYS=-10 は変えない。
