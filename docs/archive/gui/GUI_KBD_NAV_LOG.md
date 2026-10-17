# GUI 文書の作業記録 — GUI_KBD_NAV_LOG.md

> 状態: 完了記録
> 2026-10-17 に現行票から抽出。以下は当時の記録であり、未確認事項の現在の扱いは元の票を参照。

## 診断リングの観測

`/api/key` で KANA・KANA・CAPS・a・ESC の順に送った:

```
seq=1 code=72 make  key=72 KANA mods=KANA
seq=2 code=F2 break key=72 KANA mods=KANA
seq=3 code=71 make  key=71 CAPS mods=CAPS|KANA
```

- **NP21/W のカナ・CAPS は機械式ロックの模擬**: 1 回目の押下で make (ロック)、2 回目で break (解除)。
- 今のドライバ (方式 A、make で反転・break を無視) では 2 回目の後も `mods=KANA` のまま = **ロックを外しても OS32 はカナ ON と思う**。
  NP21/W の上では**方式 B (make で ON、break で OFF) が要る**。実機が同じかは下の手順で確かめる (NP21/W の模擬が実機に倣っている可能性は高い)。
- `kbdstat -w` の出力は `/api/cmd` の EOT 待ちが先に切れて返らない (画面には出る)。NP21/W では本体の画面 (`/api/tvram`) で読む。


## K2 実行記録

`/api/key` だけで: CTRL+ESC → DOWN → RETURN で **File Manager が開く** / GRPH+SPACE で窓メニュー (Restore が灰色、初期カーソルは Move) /
Minimize で窓が消えタスクバーにボタンが残る / GRPH+TAB で戻る / 窓メニューの Move → 矢印 + RETURN で窓が動く / カナで `[M]`、テンキー 3 の連打でポインタが右下へ /
GRPH+f･4 で閉じる / 窓が無い状態の GRPH+f･4 で Shut Down の確認 → ESC で戻る / Start → CUI mode の確認ダイアログ。**一部合格 (ボタン操作は未確認)**。
マウスキーでボタンを押す (5 / - / +) は NP21/W では未確認 (ホスト試験 K1 ④ のみ)。

注意 (試験の手順): `/api/mouse` の `ax/ay` を使った後は **`abs=off`** に戻す。絶対座標の上書きが残っていると、gshell はそれを実マウスとして扱い、マウスキーの位置を毎周巻き戻す (ポインタが動かないように見える)。
途中で見つけた不具合: 2026-09-23 以降 **GUI アプリが起動直後に kill されていた** (WM の中の mouse_poll が KAPI の出力の門で弾かれる) — docs/POLICY_DEBUG.md §4-61、wt/pump-guard で修正。

