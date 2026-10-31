# kernel/ver_collect.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: VER_INFO_DESIGN (D1 で移した)
ソース: `kernel/ver_collect.c`

`x86 32-bit PM+Paging; calib=measured/default`。
型番・メーカー・MHz は unknown 相当として表示しない。較正は周波数測定でも CPUID 判定でもない

| 行 | 出す値／取り方 | 取れないとき／意味の限界 |
|---|---|---|
| PIT | `8254 tick=100Hz input=1996800Hz`。pit_get_setup() の valid/hz/clk_hz（kernel/time/pit.c） | valid=0 なら `PIT: unknown`。PIT_HZ の表示だけで実設定を代用しない |
| KBD | `8251-compatible; IRQ1 (configured)`。既存 KBD IRQ 定義を使用 | ドライバ構成を表示。kbd_diag の IRQ 数ゼロを未搭載判定に使わず、I/O 読出しも不要 |
| SER | `8251 baud=9600 mode=compat FIFO=yes/no`。serial_get_status()（drivers/char/serial.c:457） | 戻り負なら `SER: not initialized`。FIFO 搭載と VFAST 動作中を分ける |
| SND | `OPN configured; PCM=CS4231-family/none`。FM 構成＋常駐 pcm_present()（drivers/sound/cs4231/pcm_module.c:61） | FM 実在は unknown。drivers/sound/fm/fm.c:54 の BUSY 成功はチップ同定ではない。PCM の none は未検出またはモジュール未読込 |
| GFX | `98/PEGC/Cirrus WxHxB bpp; active/GUI target`。下記 getter の保存値 | 無効な寸法・不明な backend は unknown。CUI 時の target を実描画モードと呼ばない |

- drivers/sound/cs4231/pcm_cs4231.c:451–466 の検出結果は、常駐 drivers/sound/cs4231/pcm_module.c:61–64 経由で取得する。module_exports 不在なら0であり、物理的未搭載とは断定しない。個別 CS4231/A 型番の getter は増やさない。
- drivers/sound/fm/snd_engine.c:640 の snd_init は再生状態の初期化であり、FM 搭載検出ではない。

- ver_collect はゼロ初期化後、既存 getter の保存値だけを取得。PIC/KBD/FM の構成定数は formatter が既存ヘッダから読む。
- boot_image_info と kapi_mem_stat の既存取得関数を直接呼ぶ。公開 KAPI をカーネルから再入呼出ししない。
