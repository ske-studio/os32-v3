# kdebug/gfinit_wire.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md・GF6200_4C_DESIGN.md・GF6200_S7_SHELL.md・HWDEV_CORE_API.md・REALHW_RECOVER_ADDENDUM.md (D1 で移した)
ソース: `kdebug/gfinit_wire.h`

## GF6200_4A_DESIGN

- op: INSPECT（PROM取得・静的検査のみ）、SIZE_BARS、PREPARE、RUN_ONE、VERIFY、ABORT。任意番地へ値を書く op は設けない。

- 固定長要求 `version,size,flags,session,script_id,max_steps,status,fail_pc,fail_reg,steps`（u32 のみ、予約語は0）。ユーザーポインタや任意 opcode 列は受けない。要求は部品内へコピーして検査する。

private な `kdebug/gfinit_wire.h` を shell (`cmd_gfinit.c`) が直接読み、既存の kdebug_call の op 5〜10 を使う。KAPI の版・slot・生成元は変えない。

## GF6200_4C_DESIGN

既存の 40 B 私有要求は維持し、ファイル指定だけレジスタ列を含む私有 payload を使う。

## GF6200_S7_SHELL

- `userland/shell/cmd_recover.c:5,52` と `kdebug/recover_wire.h:3` がprivate wire＋既存kdebug_callの先例。導入版78・slot253を使い、KAPIは変えない。

## 2 wire とコマンド

- 新 `kdebug/gfinit_wire.h` に既存op・版・旗・40B構造体をそのまま移す。u32定義後に読めるGNU89互換privateヘッダとする。

- [C4] GF_INIT_SCHEDULE=4096も一本化し、gfinit_decode.hから参照。旧:7との二重定義が残らないことを確認する。

```c
/* kdebug/gfinit_wire.h: 既存の順序・数値・意味を維持 */
struct gfinit_request {
    u32 version, size, flags, session, script_id, max_steps;
    u32 status, fail_pc, fail_reg, steps;
};
```

- main 範囲は既存 GF_INIT_MAINS=5 を gfinit_decode.h から private gfinit_wire.h に移し、shell と部品で共用する（wire 配置・op・KAPI は不変）。

## HWDEV_CORE_API

公開の入口は既存の kdebug_call のまま、shell は private な `kdebug/gfinit_wire.h` を直接読む。sdk/kapi.json・KAPI の版・slot・SDK の生成物は変えない。

## REALHW_RECOVER_ADDENDUM

- private opは5=INSPECT・6=SIZE_BARS・7=PREPARE・8=RUN_ONE・9=VERIFY・10=ABORT、版1の40byte要求とし既存kdebug_callを使う
