# drivers/video/geforce/backend_geforce.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/geforce/backend_geforce.c`

GUI とグラフィック内コンソールは CPU 描画→HAL 転送。98 のハードウェア文字合成は GF に無い。通常のテキスト CUI は 98 側に残す。
`GfxBackend` の probe/prepare/init/query/present_rect/set_palette/enter/leave/shutdown/modes を埋める。fill_rect/blit は NULL、TEXT_OVERLAY・vsync・page-flip 能力は無し、CPU 転送の PACKED8 だけ。
パレットは既存固定色/lease 方針を通った OS32 の 256 色表を使い、診断 C の 8 色表を本番へ持ち込まない（`O/gfx/gfx.h:94–98`）。モード一覧は 1 行、`gfx_init_200` の packed=480 選択を保つ。
prepare は識別/資源確認だけ。init で `video_start`→初回転送→98 セル確認→route01 確認まで行い、初回 enter は無操作。void init 後の probe=0 で既存失敗出口へ渡す（`O/gfx/gfx_core.c:932–945`）。
順番は **route00→ID/定数長/base/資源/写像確認→Command=0x0102 設定・生の 16bit 読戻し確認→ROM 採取・解読→A2 の legacy 無効化/解錠→main0〜4→基準値採取→mode 計算→共有列→必須 CHECK→初回転送/セル→route01**。A1 は本番の全経路から外す。
route01 とセルは上層の切替後に判定し、共有列の終了条件に route01 を混ぜない。510/TEST sense/scratch、50C bit28・CR1A bit3/5・680A24 全bit の照合は復活させず、AR/DAC の副作用読取りも増やさない（[照合の設計](../../../hwdev/gf/display/checks.md#落とし穴)）。

GUI=1拒否・GUI≠1許可（省略時0）、GUI 起動・CUI 描画アプリ起動の各場合で、起動 1 回につき最初の gfx_init で 1 回だけ devinit、dirty/pitch/palette、CLIENT は PEGC/GF 一方だけ、pc98 fallback、decode不明時98無書込み、shutdown→再init、部品欠落/KDEBUG=0。NP21/W は GF 無し拒否と98/PEGC/Cirrus退行

GF_ACCEL S11: video_start 成功→wc_start→初回転送。wc_start=0 は UC 継続、負は共通失敗出口。同一 active init は無操作。停止では active/routed を落としてから共通 stop を呼ぶ。identity が一致する通常停止では WC lease を起動中保持し、再開始の wc_start は前回結果を返して再取得しない。最後の正常 present は既存 BAR0 drain を完了済みであり、identity/point 失敗後の追加 BAR0 read は行わない。

再 init の probe で稼働中の identity 不一致を発見した場合も、failed を立てて共通 stop を完了してから false を返す。HAL が PC98 へ選択を切り替える前に WC lease と MTRR を戻す。gfx_core と他の backend の選択処理は変更しない。

初回のアプリ present 完了後に `[gf4] scanout` を 1 回記録する (active な init の再呼出しでは繰り返さず、停止後の init で再開)。`head=0`・`front`・`fb`・`expected_pitch` は HAL の描画先、`start`・`pitch`・`depth`・`config` は PCRTC/CR13/19/42/28 の読戻し。depth は格納ビット数 (8/16/32)、output は `gf4_route_type`、dpms は `gf4_dpms` の値。connector/or は DCB の選択、dac/test は OR A の DACCLK/TEST_CONTROL、sr1/cr17/cr1a は blank・CRTC enable・同期の読戻し。present は cpu または upload、bytes はこの present の成功転送量。非ゼロ front の upload も成功時に present_bytes/commits を数える。present の inactive/poll/transfer 失敗は停止前に段と rc を記録し、失敗した転送の後に追加の BAR 読取りは行わない。診断読取り失敗は rc を記録し、同じ init 内で繰り返さない。

転送失敗の rc は CPU の point、upload、期限取得の戻り値を保持する。F5 の温度取得・監視初期化の失敗は温度監視だけを停止し、`[gf5] therm off rc=… reason=sensor|auto|init` を一度記録する。F5 poll / present の失敗にはしない (nouveau `nvkm/subdev/therm/temp.c:162–189`)。ファン制御と時計取得の失敗の扱いは従来どおり。
