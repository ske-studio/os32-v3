# frames.log の読み取り (pc98emu)

出所: tools/ppc/link_sh_run.py が書く frames.log。
- 「>」の付いた行はホスト→ゲスト、付いていない行はゲスト→ホスト。
- ヘッダは host_agent.hdr_unpack (<BBHIIHIH) で解いた。
- frames.log には時刻が無い。行番号 (並び順) だけ。
- 解いた形は frames_d6ffafa.txt と frames_5c6659e.txt。解く台本は frames.py。

## sol の見立てと違う点
- ゲストが OUT を送っている途中で打ち切られたのではない。待っていた RSH id の「RSH OUT」の REQUEST は、ep1 でも ep2 でも一度も出ていない。
- ホストからゲストへの TOMBSTONE (制御 RESPONSE の status=2) も出ていない。
- 詰まったのは、アプリが走っている間のキー 'e' (USB KEY sc=12 down=1) の LOG。ゲストはホストの ACK・RESPONSE を受け取れていないように見える。ホストは毎回 ACK を返している。
- ゲストは送り直しが限度に達すると RELEASE し、epoch を上げて HELLO をやり直す。
- ep2 では、LOG は続き (offset 27) から送り直される。ところが RSH OUT は送られず、READY だけが「<id> done」を繰り返す。
- → OUT は、送りの列で待っている間に、epoch の切り替えで捨てられた形に見える。

## d6ffafa78 の回 (owner-unchecked / 7455、待っていたのは RSH 00000005 = v1-owner)
- ep1 の rid14 (OUT 4 = echo $?) と、rid15〜18 (LOG 7〜10) は正常。
- rid19 (LOG 11) で詰まった:
  - 173 REQUEST → 174 ACK → 175 WDATA seq1 → 176 ACK ack1 → 177 RESPONSE 200 と進んだ。
  - そのあとゲストは WDATA seq1 を 178/180/182/184 と、さらに 4 回送り直した。ホストはそれぞれに ACK ack1 を返している (179/181/183/185)。
  - 186 RELEASE → 187 RELACK。
- 188〜193 で HELLO をやり直して ep2 になった (SYN が 2 回)。
- ep2:
  - rid20: LOG 11 の送り直し。
  - rid21: LOG 12 (gen=3 init)。
  - rid22〜30: READY「cui 1 00000005 done」が繰り返される。
- 「RSH OUT … 00000005」は一度も出ていない。

## 5c6659eb1 run1 の回 (normal / 7455、待っていたのは RSH 00000008 = v1-dirty)
- ep1 の rid21 (OUT 5)、rid22 (OUT 6)、rid23〜26 (LOG 13〜16) は正常。
- rid27 (LOG 17) で詰まった:
  - REQUEST を 233/235/237/239/241 と 5 回送った。ホストはそれぞれに ACK を返している。
  - それでもゲストは WDATA に進まず、243 RELEASE → 244 RELACK。
- 245〜250 で HELLO をやり直して ep2 になった (SYN が 2 回)。
- ep2:
  - rid28: LOG 17 の送り直し。
  - rid29: READY「00000007 done」。
  - rid30: LOG 18 (gen=5 init)。
  - rid31〜39: READY「00000007 done」の繰り返し。
- 「RSH OUT … 00000008」は一度も出ていない。

## 補足 (未確認)
- 落ちる前は、どちらの回も 1 つのコマンドの間に LOG が 4〜6 本続いていた。
- ゲストの受信の窓 (RX の枠) が埋まっていた可能性がある。ただし d6ffafa の 143/144 では WINDOW 4 が見えている。
- RESPONSE の本文 c8 00 … は、status 200 と読んだ。
