# K1: `gui_call` / `gui_register` KAPI と SHM 予約、共有ヘッダ (KAPI v41)

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: K (C, カーネル背骨) / 前提: なし (今すぐ着手可)
> 親: [TASKS.md](../../tasks/gui/TASKS.md) §4 / 契約: T1, T2, T2a, T4, T5, T7 / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §9.2
> 排他: `kernel/**` `exec/**` `include/memmap.h` `sdk/kapi.json` + 生成物、`os32_kapi_shared.h`、`os32_gui_shared.h` (新規)

## ゴール

アプリ (0x500000 帯、CPL=3) が gshell の WM を呼ぶ**唯一の入口**をカーネルに切る。
カーネルは要求の中身を知らない: `gui_call(op, arg)` を gshell が登録したハンドラへ
渡すだけ。契約の数値 (op 番号・SHM レイアウト・イベント種別・エラー番号) を
**共有ヘッダに確定**させ、W / C レーンが写せる状態にする。これが他レーンの着手条件。

## 作業

1. **`sdk/include/os32/os32_gui_shared.h`** (新規): TASKS.md §4 の表を C の定数と
   構造体にする。`GuiSlotHeader` (16B: `proto_version u16` / `flags u16` (bit0 OVERFLOW) /
   `seq u32` / `ring_head u16` (アプリが書く) / `ring_tail u16` (WM が書く) / `dropped u16` / 予備 2B)、
   `GuiEvent` (16B、契約 U2 の配置: 共通ヘッダ `kind u8 @0, sub u8 @1, serial u16 @2,
   window u32 @4` (index:16 | generation:16) + ペイロード union 8B @8)、`GuiReq*` / `GuiResp*` (op ごと、512B 以内、
   文字列は `u8 len; u8 s[255]`)。**大きさ (16 / 512) と各フィールドのオフセット**を
   `STATIC_ASSERT` (`offsetof`) で固定し、同じ数表を `docs/tasks/gui/PROTO_LAYOUT.md` に
   書き出す (PM の `check_gui_proto.py` が C 側の正典として読む)。C89 なので union +
   共通ヘッダで書く。
2. **SHM 予約** (契約 T2): `include/memmap.h` に `MEM_SHM_GUI_BASE = MEM_SHM_BASE + 0x30000`、
   `MEM_SHM_GUI_SIZE = 0x10000`、`GUI_SLOT_SIZE = 0x4000`、`GUI_SLOT_MAX = 4`。
   `kernel/shm.c` の初期化でブロック 12〜15 を使用済みに固定 (`shm_alloc` が配らない)。
   PTE は既に RW+USER (v2 C2)。
3. **KAPI 追加 (v41、末尾追記、[ABI2])**:
   | 関数 | 内容 |
   |---|---|
   | `i32 gui_call(u32 op, u32 arg)` | 登録済みハンドラへ転送。未登録なら `OS32_ERR_NOSYS`。呼び出し元の owner (`res_owner_get()`) をハンドラに渡す (契約 T4 の所有者タグ) |
   | `i32 gui_register(void *handler, void *pump)` | shell 帯 (owner 1, CPL=0) からのみ。それ以外は `OS32_ERR_INVAL`。`pump` は K2 が使う (今は保存だけ) |
   | `i32 gfx_stats(void *out)` | H1 からの依頼。`GFX_Stats` 型を `os32_kapi_shared.h` に |
   | `i32 gfx_lease_palette(int first, int count, const u8 *rgb)` | H1 からの依頼 |
   | `u32 kbd_dropped_count(void)` | `drivers/kbd.c` の満杯分岐 (`kbd_count >= KBD_BUF_SIZE`) で `kbd_dropped++` し、累計を返す (契約 T3) |
   ハンドラの C 署名: `i32 (*GuiHandler)(u32 op, u32 arg, int owner)`。
   ポインタ引数 (`out`, `rgb`) はディスパッチャの既存検証 (アプリ帯 / SHM) が効く。
4. **エラー番号**: `OS32_ERR_STALE -11` / `OS32_ERR_VERSION -12` / `OS32_ERR_FULL -13` を
   `os32_kapi_shared.h` に追加。`ERR_ARG` は既存 `OS32_ERR_INVAL`。
5. **所有者による回収の口** (契約 T4 / U8): `exec_exit()` の owner 別回収に
   「`gui_owner_exit(owner)` が登録されていれば呼ぶ」を足す。WM (W1) がそこで
   ウィンドウ・サーフェス・タイマ・スロットを回収する。`gui_register` の第 3 引数にするか
   別 KAPI にするかは K1 が決めてよい (末尾追記なら契約内)。
6. **手順は CLAUDE.md の「To add a KernelAPI function」**: `kapi.json` は**テキスト追記**
   (json.dumps で書き戻さない。2026-09-04 に 139 行の差分事故あり)、
   `python3 sdk/gen_kapi.py && python3 sdk/kapi_rust_gen.py`、`KAPI_VERSION` 41、
   `docs/KAPI_SPEC.md` / `README.md` / `docs/INDEX.md` / `CLAUDE.md` の v41 表記、
   `make clean && make all` ([ABI3])。

> **版番号 (2026-09-06 調停、同日改訂)**: GUI は最終的に **v42** として確定 (ime_* の追記後、ユーザー承諾)。開発中の記述は v41 のまま残す。ネットワークは v43。ネットワークの Host Services
> ([../network/LINK_PLAN.md](../../tasks/network/LINK_PLAN.md)) も KAPI を足すが、そちらは v42 に
> ずらした ([KAPI_SPEC §3-2 の予約表](../../KAPI_SPEC.md) が版番号割り当ての正典)。GUI (K1) を
> 先に実装するので v41、ネットワークが後発で v42。KAPI は append-only なので実体は衝突しない。

## 鉄則

- カーネルは op の意味を解釈しない (WM の仕事)。`gui_call` は転送 + owner 付与だけ。
- **WM からアプリへのコールバック経路を作らない** (契約 T1: 再入の根絶)。ハンドラは
  同期に戻るだけ。
- 共有ヘッダに載せた値は以後末尾追記のみ。W / C が写した後に番号を動かすと黙って壊れる。
- `kapi.json` の既存スロットに触らない。

## 完了条件

- `hal_test` を拡張し、`gui_call(OP_INIT, 0)` が未登録で `-10`、`gui_register` を
  アプリ (owner 2) から呼ぶと `-9` を返す。
- shell から `gui_register` した後、テスト用のダミーハンドラ (K1 が `userland/tests/gui_call_test.c`
  に置く: op をそのまま返す) を通して `gui_call(5, 7)` が `5` を返し、owner が 2 で届く。
- SHM ブロック 12〜15 が `shm_alloc` で配られない (`db_test` 等の既存 SHM 利用者に回帰なし)。
- `make check` (check_kapi_version / check_constraints) を通る。KAPI_SPEC v41 の項目数が
  `kapi.json` の生成結果と一致する (`check_kapi_version.py` が照合。本票発行時点の追加は 6 関数:
  `gui_call` / `gui_register` / `gfx_stats` / `gfx_lease_palette` / `sys_switch_shell` /
  `kbd_dropped_count`。増減があれば実物に合わせる)。

## 検証手順

```
make clean && make all && make deploy-kernel   # NP21/W 停止中
os32-cycle run hal_test
os32-cycle run gui_call_test
os32-cycle run db_test
```
