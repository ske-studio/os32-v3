# sys/hal/input/input_service.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `sys/hal/input/input_service.h`

```c
/* sys/hal/input/input_service.h 等: 名前は設計名。表は必須項目を全て埋める。 */
void input_register(const InputOps *ops);     /* bootのみ、IRQ公開前 */
typedef struct InputOps {
    u32 irq;
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


- 19項目+6項目。公開転送は `input_*` / `pointer_*`。既存 `kbd_*` / `mouse_*` の実装・名前はdriversに残し、二重リングを作らない。

- `KbdDiag`/`KbdDiagLogEnt` は既存SDK共有型を利用。kbd注入APIはカーネルの既存機構であり、表へ迂回させない。

- 文脈: 転送は協調文脈/KAPI内のみ。IRQは既存driver handlerが同じ状態を更新。getkey_waitのinterruptible・park・ring3_abort_checkの位置は移さない。新たにcli/stiを足さない。

- PC-98 の exec 回収は、親に前景を返す直前に ABORTED の場合だけ `discard_stop` を呼ぶ。CUI では未読の cooked 入力を破棄し、raw には STOP・CTRL・GRPH・TAB の make/break を順序どおり残す。押下中の修飾状態は保持する。通常終了と fault では CUI の先打ちを親が読める。GUI では従来どおり中断に使った CTRL+STOP の make を取り除く。

## 入力・ポインタ・時計の登録と到達

初回の HAL 契約は InputOps・PointerOps・ClockOps の三表と共通の転送口。操作表はカーネル内部の型付き契約で、装置内の補助宣言は対象外。各機種の常駐表を製品の宣言に対してコンパイルし、登録時に全関数欄を呼べることを確かめる。登録は boot 中、入力系は IRQ 公開前で、登録自体は void。登録前の空表の応答を必須の供給に数えない。

| 構成 | キー・ポインタ | 暦時計 |
|---|---|---|
| PC-98 | pc98_d2_bind で両方必須 | 同じ初期化で必須 |
| PowerMac sh・link-sh | pm_input_boot と pm_usb_platform_init で必須 | VIA 準備成功時に必須 |
| PowerMac shell | 両表の登録要求なし | VIA 準備成功時に必須 |
| PowerMac core・link・neg1 | 登録要求なし | 登録要求なし |

必須の各経路では製品初期化関数と共通の転送関数をホストで組んで呼ぶ。ホストにない装置境界でキーを一つ供給し、登録後の input_trygetkey でその値を一度取り出す。未登録なら空表は −1 を返すため失敗する。ポインタと時計にも各一回、値が転送されたことを求める。登録を試験側で代行しない。物理 IRQ と実時間はこの観測の範囲外。同じ棚の README「pointer_service」と clock_service「登録後の暦の読出し」を参照。
