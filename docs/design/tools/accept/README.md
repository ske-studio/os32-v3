# 手動受入の設計記録
> 状態: **現行 (2026-10-31)** — 出所: RECOVER_T4B_INJECT (D1 で移した)
手動受入の現行入口は `tools/ops/` と `tools/guest_acceptance.yaml`。

## 外部と実機の運用入口

| 利用者 | 取得先と用途 |
|---|---|
| pc98emu | `git archive <SHA> tools/sound/snd2/` で音声の台本・WAV 生成・PCM 照合を取得する。GUI は `tools/ops/pc98_in2_gui.py`、共通の試行は隣の `in2_gui_trial.py` を使う。 |
| 実機ノート | 音声の手順は `tools/sound/snd2/README.md`。ゲストの実行一覧は `tools/guest_tests.txt`、手動受入の手順と合否は `tools/guest_acceptance.yaml` にある。 |
| os32powerpc | 手動の shell 観測は `tools/ppc/link_sh_run.py` が隣の `ppc_shell_flow.inc` を読む。枝の載せ直しに使う表は `tools/ci/replay/move_map.json`。 |

手動の GUI・ARM 計測・H2 診断・実機採取と文書保守の台本は `tools/ops/` に置く。

### recover_t4b_host

rshell_recover の open_port/exchange を使い、受信を底の raw へ記録してから final 行または EOT だけを監視器へ渡さない。二重 reader にしない。
- フィルタは分割受信に対応し、final行抑止とEOT抑止を分ける。上位log・下位raw・abort回数を保存する。ゲストの送信成功をホストの終端受領と同一視しない。

- helper は既存 state の session/Image CRC を照合して virtual end だけを送る。begin/step の state/log を継続し、ver を再送しない。
- 単一の FilterPort.read が下位rawを保存してから final行またはEOTを抑止し、既存exchangeへ渡す。abort フレーム数は標準出力に出し、成功は既存exchangeのOKだけとする。

- [gf_accel](gf_accel.md)
- [ppc_qemu](ppc_qemu.md) — PowerPC の QEMU の受入の台本の落とし穴 (撮影の時・キー・変異の目印・期待の出所・CI の重み)
