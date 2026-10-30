---
name: os32-emu-config
description: NP21/W ini の限定変更と構成切替。
---

# NP21/W の構成変更

ini を変更できるのは PM のみ。原本を残す経路を既定とし、終了後は元に戻す。NHD・イメージ原本の上書きは [D2](../../../docs/CONSTRAINTS.md) に従う。

まずゲスト側の `gfxmode`、設定読み戻し、reset を試す。実際の backend は reset 後の `hal_test` で確認する。

## 構成切替の 3 経路 — 開始・停止・復帰

| 経路 | 開始 | 復帰 |
|---|---|---|
| trial (媒体) | 稼働中 PID・生成時刻を `tools/np21w_trial.py` に束縛。道具が通常終了・確認後に新規 ini を作り起動。PM は先に止めない | `tools/np21w_ctl.py stop`、事前に選んだ原本で `start --ini <原本名>` |
| 派生 ini | `np21w_ctl.py stop`、`tools/np21w_ini.py derive` で原本から新規作成、`np21w_ctl.py start --ini <派生名>` | 停止し原本で起動、元の GFX を戻し reset 後 `hal_test` で確認 |
| live-apply | 派生 ini が使えない場合、`tools/np21w_ini_live.py <操作> --live-apply --exclusive-operator`。道具が停止・保存・起動 | 同じ receipt で `restore --live-apply --exclusive-operator`。各適用を戻してから次へ |

trial は HDD1FILE・FDD の取り外し・`e_resume=false` のみ扱う。baseline は以前の active ini の証明ではない。試験の計画と終了確認は `tools/np21w_trial.py` に従う。
派生 ini は `NP21W_DIR` を明示し、既存名を上書きしない。対応する操作・キーと例は `tools/np21w_ini.py --help`、live の復元は `tools/np21w_ini_live.py --help` を参照する。未知・欠落・重複キーは推測して追記しない。
停止・起動は `np21w_ctl.py` を使う (trial と live-apply は道具自身が停止)。強制終了に落ちた場合は記録する。元の GFX と実 backend を復帰後に確認する。
