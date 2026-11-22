# include/input_service.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `include/input_service.h`

```c
/* include/input_service.h 等: 名前は設計名。表は必須項目を全て埋める。 */
void input_register(const InputOps *ops);     /* bootのみ、IRQ公開前 */
typedef struct InputOps {
    int (*getchar)(void), (*getkey)(void), (*trygetchar)(void);
    int (*trygetkey)(void), (*peekkey)(void), (*trygetchar_local)(void);
    int (*getchar_kapi)(void), (*getkey_kapi)(void), (*getkey_wait)(int);
    int (*has_key)(void), (*is_pressed)(int), (*trygetrawkey)(void);
    u32 (*modifiers)(void), (*dropped_count)(void);
    u8 (*shift_state)(void);
    void (*gui_mode)(int), (*discard_stop)(void);
    int (*diag)(KbdDiag *), (*diag_log)(u32, KbdDiagLogEnt *, int);
} InputOps;
/* SHIFTビットの値を変えない。modifierとの同一性を仮定しない。 */
```

> [要確認] include/input_service.h の現行 InputOps には、この原文の表に加えて u32 irq がある。

- 19項目+6項目。公開転送は `input_*` / `pointer_*`。既存 `kbd_*` / `mouse_*` の実装・名前はdriversに残し、二重リングを作らない。

- `KbdDiag`/`KbdDiagLogEnt` は既存SDK共有型を利用。kbd注入APIはカーネルの既存機構であり、表へ迂回させない。

- 文脈: 転送は協調文脈/KAPI内のみ。IRQは既存driver handlerが同じ状態を更新。getkey_waitのinterruptible・park・ring3_abort_checkの位置は移さない。新たにcli/stiを足さない。

- PC-98 の exec 回収は、親に前景を返す直前に ABORTED の場合だけ `discard_stop` を呼ぶ。CUI では未読の cooked 入力を破棄し、raw には STOP・CTRL・GRPH・TAB の make/break を順序どおり残す。押下中の修飾状態は保持する。通常終了と fault では CUI の先打ちを親が読める。GUI では従来どおり中断に使った CTRL+STOP の make を取り除く。
