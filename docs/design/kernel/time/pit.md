# kernel/time/pit.c — PIT 周期設定

> 状態: **現行 (2026-10-31)**

保存されたクロックからタイマの reload と周期を設定する。

## 落とし穴

- **クロックの決め打ち**: 1.9968MHz 用 reload を 2.4576MHz 機で使うと、100Hz のつもりが約 123Hz になる。
  `kernel/time/pit.c:26` は検出済みクロックを `pit_compute` に渡す。100Hz の reload はそれぞれ 19968 / 24576。
  `make check-pit-clock-host` は両クロックの算術を検査するが、実機の実時間は tick 差と単調時計で別に照合する。

## 内部設計と契約 (D1)

```c
/* kernel/time/pit.c: s_pit を所有し、状態公開後にハードへ渡す */
int pit_init(unsigned int hz) {
    struct pit_setup s;
    int rc = pit_compute(sysclk_hz(), hz, &s);
    if (rc != 0 && pit_compute(sysclk_hz(), (unsigned int)PIT_HZ, &s) != 0)
        return rc; /* 両失敗なら状態/I/O 不変 */
    s.mode = (u8)PIT_MODE_TIMER0;
    s_pit = s;
    platform_timer_program(s.reload);
    return rc; /* fallback 成功時も元の rc */
}
```

|所有者／文脈|移動・維持する中身|
|---|---|
|kernel/time/pit.c（idt.c から論理部移動）|`s_pit`、pit_get_setup、pit_init を所有。pit_compute と既定 PIT_HZ 再計算、元の rc、両方失敗時は状態/I/O 不変|
