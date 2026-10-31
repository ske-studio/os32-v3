# exec/appslot.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `exec/appslot.c`

### T2a. 複数アプリ時のスロット割当と譲り合い — 確定 2026-09-04

今の契約 (割当・配送・譲り合い・見え方) は [09_exec「GUI アプリのスロットと譲り合い」](../../09_exec.md#gui-アプリのスロットと譲り合い) へ移した。

<a id="park-origins"></a>

**park の由来 (5 種)**: OP_WAIT / kbd待ち / polling / sys_yield に加え、
GUI STOP の WM 返却要求だけが **正常 syscall 完了後**に退避する。
第5由来は WAIT_POLL の内部印 `parked_from_stop`、resume は保存 EAX を維持し、
注入リングを読まない。X4 自体は退避も宛先解決もしない。
