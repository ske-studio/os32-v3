# sdk/kapi.json — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・DEP_DIR_D2_DESIGN・VER_INFO_DESIGN (D1 で移した)
ソース: `sdk/kapi.json`

body付きIDE/formatは既存の write_allowed、範囲検査、cnt==0、cast の順を保ち、
IDE init のKAPIは void（実装は int）なので戻り値を従来どおり捨てる。

全kbd entry（diag/log/local/rawを含む）はtarget=input_*、全mouse entryはtarget=pointer_*。
- serialの8操作はtarget=port_*。serial_diagのbodyはkapi_serial_diagのまま。kbd_inject/pending、rshell_set_activeはそのまま。
- rtc_read bodyを `clock_read_legacy((RTC_Time *)rtc_time);`、mouse_poll bodyを `pointer_poll((MouseState *)info);` とする。outのsizeof型は共通型へ移すだけ。

> [要確認: 計画の時制?]

```c
int __cdecl kapi_sys_ver_info(void *out, u32 size);
```

```json
{"name":"sys_ver_info", "ret":"int",
 "args":["void *out", "u32 size"],
 "out":[{"arg":"out", "len":"size"}],
 "target":"kapi_sys_ver_info"}
```
