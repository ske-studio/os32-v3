# PC-98 resident 82557 HELLO, base c34ee44f1

From the worktree, run python3 -B /home/hight/os32-tmp/run/sfs-deadline-s0/model/build.py
and then qemu-i386 /home/hight/os32-tmp/run/sfs-deadline-s0/model/model.
Exit 1 flags the first expired HELLO publishing the old peer. See trace.log.

Start after device initialization with a running NIC and empty DMA ring. Send
HELLO nonce 1 at tick 0 with deadline 100. Complete each accepted transmission
one tick later except the transmission at 80, which completes at 101.
Write the valid nonce 1 reply from 02:00:00:00:00:01 to the receive descriptor
at 91, during the retry that began at 90. Sample the LAN state at 92 and on
return. Immediately send HELLO nonce 2 with deadline 200, and write its reply
from 02:00:00:00:00:02 at 102.

The model drives I/O registers, descriptors and clock interrupts. It runs the
product clock callbacks, tick dispatch, NIC driver, net_tx, idle work and I/O
dispatch, receive demux, LAN transport, client and wire decoder. The network
link protocol is idle in this scenario. Device probing, module loading, UART
and full platform boot are outside this recorded run.

## 通常の seq 更新と期限際の受付（常駐・モジュール共通）

作業木を cwd にして model ディレクトリを d に設定する。

    python3 -B "$d/build_normal.py"
    qemu-i386 "$d/normal"
    qemu-i386 "$d/normal" module

normal.c は初期化済み 82557 と空のリングから開始し、HELLO を受け付けた次の tick に正常応答する。接続後 seq を通常更新の閾値の直後に置き sfs_ensure_session を呼ぶ。続いて残り 1 tick で無応答の STAT を送り、DMA は受付から 2 tick 後に完了させる。どちらの構成でも同じ入力を供給する。常駐の接続表、または lan_module_init が構成した転送表を選ぶ。後者ではロード済みモジュールの記録を与え、同名の本体とファサードを同一 ELF に収めるためファサード側の公開名を objcopy で区別した。装置探索、モジュールロード、マウントは対象外。

## UART

    python3 -B "$d/build_uart.py"
    qemu-i386 "$d/uart"

uart.c の main が入力列。初期化済みの互換 UART を 9600 bps に置き、ポートの TxRDY、RxRDY、受信バイトと時計を与える。無応答と TxRDY 停滞を順に与えてから復旧し、有効な HELLO 応答を返す。通常 CALL の無応答と終了を経て、再取得後の CALL で TX を止める。TxRDY を戻して seq を最後の有効値に置き、CALL と終了を実行する。get は実ゲートへの呼出し、accepted は線上で完全に復号できたフレームを数える。idle、halt と TX ポーリングの delay で時計を進める。実 CPU の経過時間の測定は対象外。ログ出力は空のため LOG フレームはない。
