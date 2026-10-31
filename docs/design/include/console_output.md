# include/console_output.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `include/console_output.h`

- 緊急出力は後述のリンク時固定表。登録以前の例外でも従来のUART既定ポートで出力できる。

```c
typedef struct ConOutputOps { int (*putchar)(char); } ConOutputOps;
void con_output_register(const ConOutputOps *); /* 1出力先、bootのみ */
typedef struct EmergencyOps {
    void (*puts_polled)(const char *);
    int (*gate_active)(void);
} EmergencyOps;
extern const EmergencyOps emergency_output; /* platformがリンク時に供給 */
```

- 協調ログは呼出側の `!serial_gate_active()` 判定を保持。例外/二重障害/teardown失敗は従来どおりgateを迂回してpolledへ。
- 緊急表はIRQ/例外/IF=0で呼べる。割当・ロック・通常console・hlt・校正依存待機を使わない。`drivers/char/serial.c:875–905` の有限spin、改行順、io_waitを維持。
- driver依存の全体撤去ではない。serialfs等drivers側からserialへの呼出し、D2外の装置専用APIは残す。
