# kernel/ime/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN・MEMMAP_V3・SLIM_KERNEL_DESIGN (D1 で移した)
ソース: `kernel/ime/`

### fep_module

- kernel/ime/fep_module.cのshift getterとfallback/関数表をinputへ。`getkey_wait(1)`→負値→abort_check、con_sink時の生バイト、拡張キー除外を維持。fep.mod import名・世代は変更しない。

| 段 | 内容 |
|---|---|
| FD 起動 (SQLITE_ONLY、D21) / 実失敗 (MINIMAL、D26) | SQLITE_ONLY は `db_*` 公開・FEP は §5-6 の**API 別 stub**。MINIMAL は `db_open` が `OS32_ERR_NOSYS` 系の 1 値 |

| 問い | 決定 / 案 |
|---|---|
| **未搭載時の stub (API 別)** — SQLITE_ONLY・MINIMAL で効く | `ime_is_active` = 0、`ime_get_mode` = 0、`ime_set_mode` / `ime_toggle` / `ime_set_render` = 無操作、`ime_switch_dict` = `OS32_ERR_NOSYS`、`ime_feed_key(k)` = **素通し (無入力の負値もそのまま返す、T4c-2の実装契約を保持)**、`ime_getkey` / `ime_getchar` = 生のキー入力に落とす (ブロッキング)、**`ime_trygetkey` = `kbd_trygetkey()` の生キー (非ブロッキング、無ければ −1)、`ime_trygetchar` = その文字** (CUI エディタの `vz_kbhit` が `ime_trygetkey` を回している `apps/edit/sys_keyboard.c:27`、`ime.c:851`)、`ime_user_*` = `OS32_ERR_NOSYS`。スロット順は維持 |

### ime

`static int ime_candidate_key(u8 ascii, u8 scancode)`
`return ime_input_key(ascii, scancode);`。g_imeと候補表示staticは元ファイルに残す

- [候補確定と透過キーの配送](ime_delivery.md): 共有の確定列・1件保留、取消と park/resume の契約。
