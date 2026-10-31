# tools/accept/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: RECOVER_T4B_INJECT (D1 で移した)
ソース: `tools/accept/`

### recover_t4b_host

rshell_recover の open_port/exchange を使い、受信を底の raw へ記録してから final 行または EOT だけを監視器へ渡さない。二重 reader にしない。
- フィルタは分割受信に対応し、final行抑止とEOT抑止を分ける。上位log・下位raw・abort回数を保存する。ゲストの送信成功をホストの終端受領と同一視しない。

- helper は既存 state の session/Image CRC を照合して virtual end だけを送る。begin/step の state/log を継続し、ver を再送しない。
- 単一の FilterPort.read が下位rawを保存してから final行またはEOTを抑止し、既存exchangeへ渡す。abort フレーム数は標準出力に出し、成功は既存exchangeのOKだけとする。
