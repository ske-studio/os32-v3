# userland/lib/gfx/libos32gfx_core.c — ユーザー描画面の接続

> 状態: **現行 (2026-10-31)**

描画面を query / lease して描画ライブラリに接続する。

## 落とし穴

- **描画形式の初期化漏れ**: attach で PACKED8 を設定しないと、漢字描画がプレーン形式に落ちて表示されない。
  `userland/lib/gfx/libos32gfx_core.c` は CLIENT 面を query / lease し、取得した形式から `gfx_packed` を設定する (`gfx_refresh()`)。
  init と attach の両方で同じ接続処理を使い、世代変更や失敗時の lease 寿命も守る。
  `tools/tests/gfx_attach_host.c` の `run()` が接続・再接続と失敗を検査する。

## 設計 (D1 で移した)

> 出所: MEMMAP_V3

SDK はそのポインタへ直接描く (`libos32gfx_core.c` の `kapi_query()`)

> [要確認] 原文の `libos32gfx_core.c` は現行では記述子の参照を写す行。返された lease の番地を描画先へ接続する処理は同ファイルの 113 行以降。

## checked attach と帰路の照合

> 出所: TASK_T2D_T2H (§2-3・§2-5 e6・e7) の契約を今のソースと照らして要約 (2026-11-18)

### attach (`libos32gfx_attach_checked`・`gfx_refresh`)

- CPL3 で v70 以上の KAPI があれば、CLIENT の query → lease で描画先を得る。CPL0 (gshell を含む) と v70 未満は互換の `gfx_get_framebuffer` を使う (USER の互換の橋は呼ばないので二重に取らない)。
- query の ref (sid・世代) が保持中の token の ref と同じなら token と view を再利用し、query 1 回だけで戻る。違えば (再 init の後) 旧 token を返し、旧 pointer を消してから lease し直す。旧 VA が他の token に使われていても新しい VA を使う。
- desc の検査: 世代・幅・高さ・pitch が 0 でない、`height <= bytes / pitch`、形式と面数の組 (PLANAR4 は 4 面、PACKED8 は 1 面)。view の検査: token・base が 0 でない、bytes が desc と同じ、各面が `base + plane_offset` に一致し面の外の planes は 0。表示の高さは `gfx_screen_info` から取り (200 行でも登録は 400)、幅と形式が desc と一致すること。
- 取得の後の検査に落ちたら、取った token を返して描画を止める (`gfx_ready` = 0、fb は全欄 0)。部分的な成功を描画に使わない。
- surface・sprite のプールは framebuffer に依らず 1 回だけ初期化する。FULL で attach に失敗しても初期化し、再 attach で生きているプールをリセットしない。
- attach 1 回につきシリアルに `[libos32gfx] attach rc=... ready=...` を 1 行出す (console へは出さない。CLIENT に描いてしまうため)。

### 帰路の照合 (`libos32gfx_check`)

- 待ち (`libos32gfx_getchar`・入力があった `libos32gfx_trygetchar`・`libos32gfx_yield`・`libos32gfx_halt`) から戻るたびと `gfx_present` の前に呼び、query で世代を照合する。一致なら何もしない、不一致なら上の手順で取り直す。取り直しに失敗したら描画を止め、待ちの結果 (入力のキー) はそのまま返す。
- shutdown (`libos32gfx_shutdown`) の後は check も待ちの帰路も CLIENT を取り直さない。明示の attach・init で再開する。
- 描画の入口 (clear・fill・blt・surface・sprite・font・save・restore・present・raster) は `gfx_ready` が 0 なら何も書かない。
- アプリの静的リンクの実体と `libos32gui.shlib` の実体は**別々に** token を持つ。両方を使うアプリは、どちらかを再開する前に両方を check する。片方だけ取り直しに失敗したら、その実体は自分の token を返して描画を止める (もう一方は描ける)。Rust の `os32api` の `check()` も失敗で自分の実体を detach する。
- 生の pointer を保存して再 init をまたいで使うのは契約の外。

検査: `tools/tests/test_gfx_attach.py` (kernel 側と SDK の core・描画 C を連結、3 backend・反復取得・世代変更・8 本満杯・不正 view の巻き戻し・描画の停止)、`tools/tests/test_gfx_reattach.py` (core と描画を静的と shlib の 2 組リンクし、再 init・200 行の後の両世代と片側の失敗)、`tools/tests/test_gui_reattach.py` (Rust の client・gdi)。
