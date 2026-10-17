# PEGC480_REALHW_EVIDENCE_2026-10-01

状態: **完了記録**。文書整理前の記録。現行の判断・未確認事項は元票を参照。

## 実機での受け入れ (C) の記録 (2026-09-29 22:21、実機エージェント、672f849)

`gfxmode pegc` → 再起動 → `pegcchk 5`:
```
[pegc] enter 09a8=81 msk=0 dsp=1 lcd=1 clk=3 ext=1 800l=1 fifo_to=0 vs_to=0
[pegc] exit  09a8=81 msk=0 dsp=1 lcd=1 clk=0 ext=0 800l=0 fifo_to=0 vs_to=0
R mode     : 640x480 while in (expect 640x480), pattern drawn
R back     : CUI after 532 ticks
pegcchk done
```
期待値と完全一致。続く `v86 -g` は `R 31h : AX=310d BX=0100`・`R 30h back … AX=0500 (ROM)` で、OUT 200 行の port・value・cs:ip は d5cd3a5 の記録と同じ。
`[selftest] 225/225 passed`。後片付けで `/etc/system.cfg` を元どおり無しに戻し、再起動して 672f849 で応答。**GUI は起動していない (目視は未実施)**。


## v3 のカーネルでの実機ログ (2026-10-01、実機エージェント、`737f6e4`、画面は見ていない)

`gfxmode pegc` → シリアルから `reboot`。boot.log `[pegc] hsync=24k 09a8=80 bios054c.b5=0 bios0459.b0=0` / `[pegc] gdcclk=2.5M clk1=0 clk2=0 bios054d.b2=0 pitch400=40`、`hal_test` = `backend pegc (packed 8bpp)` 640x480、`pegcchk 3` = `[pegc] enter 09a8=81 msk=0 dsp=1 lcd=1 clk=3 ext=1 800l=1 fifo_to=0 vs_to=0` / `[pegc] exit 09a8=80 … clk=0 ext=0 800l=0 fifo_to=0 vs_to=0` / `R mode 640x480 … pattern drawn` / `R back CUI after 334 ticks`。9/29 の記録 (起動時 `hsync=31k 09a8=81 bios054c.b5=1`、exit `09a8=81`) と違うのは、**この日の実機が 24kHz で起動していた**ため (起動時の `09A8h` bit0 = 0)。enter は 31kHz / 480 ラインへ入り、exit は起動時の値 (80) へ戻す — 段 2 の設計どおり。この日は実機を 24kHz モードで起動していた (ユーザー確認 2026-10-01)。実機は `GFX=pegc` のまま。

