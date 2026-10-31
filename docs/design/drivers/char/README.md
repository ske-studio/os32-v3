# drivers/char/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `drivers/char/README`

### rtc

```c
/* drivers/char/rtc.c側adapter: readは1回。80/2000/1900は既存定数を使用。 */
static void pc98_read_date(ClockDate *d) {
    RTC_Time t; rtc_read(&t);
    d->year = t.year + (t.year < RTC_YEAR_PIVOT ? RTC_YEAR_BASE_NEW : RTC_YEAR_BASE_OLD);
    d->month=t.month; d->day=t.day; d->wday=t.wday;
    d->hour=t.hour; d->min=t.min; d->sec=t.sec;
}
```

- RTCのBCD・ビット送受信・2桁年補正はdriver内。read_legacy=rtc_readで7Bの旧ABIを厳密維持。

- `const ClockOps pc98_clock = { pc98_read_date, rtc_read };` はdrivers/char/rtc.cに置き、platformはexternの表を登録する（static adapterをファイル外から参照しない）。
