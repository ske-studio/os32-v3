# TASK_GFX_MODES — 表示の色数と解像度を「形式」と「ドライバの能力の表」で持つ

> 上位: [ROADMAP](../../ROADMAP.md)・能力の問い合わせ [API_CONTRACTS](../../decisions/gui/API_CONTRACTS.md) G5・規則 [C4] ([POLICY_DEV](../../POLICY_DEV.md) §2)

> 状態: **受入完了 (2026-10-26)** — G0・G2〜G6b が main。色数は形式から (`GFX_PAL_ENTRIES`)、各バックエンドが出せるモードを表で申告し、HAL が表から選んで FB・問い合わせの大きさを決め、予約に収まらないモードは断って今のモードへ戻る (高解像度の窓)。アプリへの口・KAPI は不変、Cirrus のモジュールの ABI 値だけ変わる (kernel と cirrus.mod を対で配る)。延ばした試験は DEFERRED_TESTS の GFX-MODES-C480。

## 0. ユーザー指示 (2026-10-26)

- 「本質的には 256color、65536color、24bitColor のようにデバイスごとに共通のパレット数が増えていくとデバイス数 x 表現色数になってしまいませんか? デバイス側のドライバで使える表現色のバリエーションをコントロール出来るようにするのが筋では? また、高解像度対応を今後する時に解像度も同じ理屈であるべき」
- 「大きい修正のオーダーも先に出せるなら出してください。直しは小さいのから大きいの順で。大きいものの粒度は気をつけて」
- 「高解像度時の窓は作るが高解像度版の実装はないで」— 窓 = 形式の層に形式を足せる・ドライバの表にモードを足せる・HAL が表から選んで大きさを決める・予約に収まらないモードは選ぶときに断って今のモードへ戻る、まで。高解像度版の実装 (起動時の予約とバックバッファを広げる = 番地の配置と配置の世代が変わる、ドライバの高解像度の init) はこの票ではしない。

## 1. 今の形と問題

- アプリへの口は形式で書けている: `GFX_FMT_PLANAR4` / `GFX_FMT_PACKED8` と `gfx_screen_info()` (幅・高さ・bpp・形式、契約 G5)。
- ドライバの側は装置ごとに直に書いている: `CIRRUS_WIDTH/HEIGHT`・`CIRRUS_PAL_COUNT`・`PEGC_HEIGHT_480/400`・`PEGC_PALETTE_COUNT`・`GFX_HEIGHT`・バックバッファの `MEM_GFX_BB8_HEIGHT` (480 固定)。このままだと名前が「装置 × 色数 × 解像度」で増える (票 TASK_MAGIC_NUM2 の N14 で Cirrus のパレット数を PEGC の名前に寄せかけ、装置ごとの名前に戻したのが発端)。

## 2. 方針 (ユーザー指示から)

- **形式の層 (装置に依らない)**: 形式ごとに bpp・インデックスか直接の色か・パレットの項目数を持つ。パレットの項目数は形式から決まる (8bpp のインデックスは 256)。16bpp・24bpp は形式を足すだけにする。
- **ドライバの能力の表 (装置ごと)**: 各ドライバが出せるモード {幅, 高さ, 形式} の一覧と、装置で本当に違う値 (DAC の成分の幅: 98 の 16 色は 4bit、VGA は 6bit、PEGC は 8bit) を申告する。ドライバの本体に解像度・色数の数を直に書かない。
- **HAL がモードを選ぶ**: 選んだモードから、フレームバッファ・バックバッファ・面の大きさを決める。
- 範囲: 今ある 640×400/480・16 色/256 色を表で申告する形に直すところまで。高解像度・16bpp 以上の実装はしない (足せる形にするだけ)。

## 3. 段

| 段 | 中身 | 設計 | 実装 | 状態 |
|---|---|---|---|---|
| G0 | 小さい直し: Cirrus と PEGC のパレット数を、形式から決まる 1 つの名前にする (機械語は変えない) | 不要 | sol | **main** (675bb395、astra 取り込み可、機械語の差 0) |
| G2 | 形式とモードの記述 (include/gfx_format.h・gfx_hal.h・新しい gfx_mode.h、cirrus の契約一覧)。cirrus.mod の ABI の値が変わり対で配り直す | [GFX_MODES_DESIGN](../../decisions/gui/GFX_MODES_DESIGN.md) | sol | **main** (astra 取り込み可、.o の差は Cirrus の 3 関数、Cirrus の ABI 値が変わる — kernel と cirrus.mod を対で配る) |
| G3 | 98 の 16 色のバックエンドがモードの表を申告 (gfx/backend_pc98.c) | 同上 | sol | **main** (astra 取り込み可、PM のゲスト gfx-modes-n16 合格) |
| G4 | PEGC のバックエンドの表 (gfx/backend_pegc.c) | 同上 | sol | **main** (astra 取り込み可、PM のゲスト gfx-modes-p480・p400 合格) |
| G5 | Cirrus のバックエンドの表 (gfx/backend_cirrus.c) | 同上 | sol | **main** (astra 取り込み可、PM のゲスト: Cirrus が無いときの PEGC への戻り。C480 は節目) |
| G6a | HAL のモードの選択と init / init_200 / prepare への反映 (gfx/gfx_core.c) | 同上 | astra | **main** (Opus 取り込み可・PM が変異 1 本を直す、PM のゲスト n16・p480・p400 合格) |
| G6b | フレームバッファと query への反映、容量の照合 (gfx/gfx_core.c・gfx.h・gfx_internal.h) | 同上 | astra | **main** (Opus 取り込み可・P3 の 2 件を astra が直す — 98 は DISPLAY の記録が足りなくても前と同じく描ける、PM のゲスト n16・p480・p400 合格) |

段は小さい順の直列 (G0 → G2 → G3 → G4 → G5 → G6a → G6b、試験の照合先が重なるので並べない)。起動時の予約と面の型板は memmap の容量のまま変えない (表との照合はモジュールを読み込んだ後のモードの選択のときだけ)。高さの正典は `gfx_current_height`、問い合わせの高さは今の振る舞い (GUI を抜けた後・init の前も PEGC と Cirrus は 480) を変えない。ゲストは PM が NP21/W で (98 の 16 色・PEGC 480 と 400 の CUI からの出入り)。Cirrus を有効にする構成の試験 C480 は節目の関門で (DEFERRED_TESTS)。PEGC の 256 色 400 ラインは表に載せない (PM 決定、今の振る舞いのまま)。
