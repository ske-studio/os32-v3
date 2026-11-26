# sys/hal/time/clock_service.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `sys/hal/time/clock_service.h`

```c
void clock_register(const ClockOps *ops);
/* sys/hal/time/clock_service.h: legacy ABI型と内部の西暦型を区別。 */
typedef struct { u8 year, month, day, wday, hour, min, sec; } RTC_Time;
typedef struct { int year; u8 month, day, wday, hour, min, sec; } ClockDate;
typedef struct ClockOps {
    void (*read_date)(ClockDate *);
    void (*read_legacy)(RTC_Time *);
} ClockOps;
```

- ClockOpsは協調文脈のみ。時刻キャッシュや周期IRQを追加しない。異常値の丸め・再試行も足さず、現在の読出し結果を保存する。

## 登録後の暦の読出し

登録要否は入力サービスの「入力・ポインタ・時計の登録と到達」に従う。PowerMac は pm_via_prepare の準備失敗時には登録せず、初期状態の空表はゼロの日付を返す。成功経路ではホストで装置に既知の日時を与え、製品初期化後の clock_read_date で西暦年と月日を読む。read_legacy は 2 桁年から始まる 7 バイトの公開配置を守る。
