# fable reviewer の r2 レビュー (2026-11-27): P1 1・P2 5・P3 3、r3 不要
読んだ実物: bd01609c0 の sfs_client.c 195–320・hostfs_link_io.c 全体・serialfs.c 55–90/275–300・idle_work.c・rpc.inc・ppc_usb_lan.py 60–135・kernel_dec.c。x86 (sfs_lan.c・lan_module.c)・platform_host_close の中は読んでいない。
(1) cancel の契約: 抜けなし。発端も成り立つ: idle_wait_step は「仕事を回してから眠る」順で、覚めた前景は poll を通らず client の期限判定へ戻り return。直しの後は CLIENT の終了が通常の道。
P1 §3・§7: 判定 end_tick == deadline は QEMU の負荷で落ちる (DEC 配達の遅れで 1 回のハンドラが tick を 2 つ進める kernel_dec.c の while (elapsed--)、前景が deadline の tick を見られない。dec_rate と同じ仕組み)。直し: 判定を「now ≥ deadline を最初に観測した前景の周回で閉じた」に (client の直前の now を fixture に残し prev < deadline ≤ end、timeout→close の間に別の周回が無い)。実時間の上限は host 差 80〜200 が持つ。§7 は「製品の保証は『期限を過ぎて最初の前景の観測で閉じる』まで。jiffy 側の失効は足さない (IRQ から close を出す新しい契約が要り過剰防御)」を推奨として諮る。
P2-1 §3: 順序 timeout→close→return は 10 ms tick では同じ値 → fixture に通し番号 (事象ごと +1)、順は番号・時刻は tick。
P2-2 §3・§6 段 2: ppc_usb_lan は POLL と CLIENT の両方を合格にするので hostfs_link の cancel を外しても通る。PPC の直しを守る変異が無い → 「PPC fixture の小試験」を置く登録検査を書き、hostfs_link の cancel を無効にする変異 (表の cancel を 0) を 1 つ。
P2-3 §3・§5: serialfs.c:67 の start・cancel 直前・finish の理由は製品ソースの中の点、「全部 fixture」の入れ方が無い。rpc_ticks の変異は serialfs.c を置き換えるので写しの順と噛み合う必要 → 差し込みの道 (どのファイルの写しに何を足すか、製品に #ifdef を足さない) と、変異の後に写しを作る事を 1 行ずつ。
P2-4 §1③・§6 段 0: UART は期限付き交換を通らない (非常駐は sfs_deadline 0) のに段 0・§2 の適用先に。82557 常駐・モジュールの模型 3 つは sfs_deadline_cancel と役が重なり U11 に対し過剰 → 段 0 は §1 の転送口の表の読みと sfs_deadline_cancel の「cancel=0 の同期 transport」の場合に畳み、UART は対象外。
P2-5 §2 末尾: 「超過なら送信中にも期限を渡して打ち切り、残留ならその transport の cancel」は範囲の拡大の先取り → 見つかったら止めて別の票。
P3-1 §3: 変異像で幅 50 の目印より先にほかの必須条件が負荷で落ちると不定 → 幅の判定を読み出し直後・ほかの判定の前に。
P3-2 §2: cancel 後も tx と result=TIMEOUT を保つので同じバイト列の再送は開かない。HELLO の nonce が繰り返さない事に寄る → 前提として 1 行。
P3-3 §2: platform_host_close(handle,0) が待たない事を非待機の根拠として §1 に実物の行で。
(2) 幅と終了の位置を別々に見る形は妥当、rpc_ticks は両 CPU で検出。(5) 段の順は妥当。
