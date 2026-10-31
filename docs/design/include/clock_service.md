# include/clock_service.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `include/clock_service.h`

```c
void clock_register(const ClockOps *ops);
/* include/clock_service.h: legacy ABI型と内部の西暦型を区別。 */
typedef struct { u8 year, month, day, wday, hour, min, sec; } RTC_Time;
typedef struct { int year; u8 month, day, wday, hour, min, sec; } ClockDate;
typedef struct ClockOps {
    void (*read_date)(ClockDate *);
    void (*read_legacy)(RTC_Time *);
} ClockOps;
```

- ClockOpsは協調文脈のみ。時刻キャッシュや周期IRQを追加しない。異常値の丸め・再試行も足さず、現在の読出し結果を保存する。
