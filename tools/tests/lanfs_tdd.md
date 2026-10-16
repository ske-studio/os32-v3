# LANFS stage 1 host tests

依頼: `/home/hight/os32-tmp/run/lanfs/PACK_lf1.md`、設計: 同所 `LANFS_DESIGN.r1.md` §3/§6 (PM の決定優先)。

`make check-lanfs-host` builds the real `fs/sfs_client.c`, `sfs_proto.c`,
`sfs_lan.c` and CRC into a host shared library. A ctypes NIC passes complete
Ethernet frames to the real `serialfs_host.Server` / read-only `HostFS`, through
`lanfs_host.LanServer`. Only NIC delivery and the 10 ms clock are simulated.
`--target` also compiles the C sources/harness with i386-elf GCC and `-Werror`.

Cases: 508 B READ and exact contents; request/response loss; immediate and late
duplicates (execute count); same-seq different contents; gaps; HELLO replay after
ordinary requests; CRC corruption; wrong EtherType/destination/source/marker;
IF=0; stopped server; continuous unrelated RX; bounded AGAIN put; preservation
of a marked frame during put; short-datagram isolation; RX capacity 1536 and
8-frame get budget; zero padding on short TX; read-only HostFS; foreground host
serve loop. The original Server's execution counter proves retries do not repeat
an operation. UART gate calls are observed by a C stub and must remain zero.

`--mutate` runs the unchanged C/Python control first, then separate source copies:
seq matching removed; old seq re-executed; put deadline removed; UART gate raised.
The put mutant must time out in a child process. Other mutants must fail a runtime
assertion. Build failures and missing mutation anchors are errors, never kills.

PM 台帳転記用 (この段では未実施):
- LF2-SESSION: `/host` busy, NIC/session ownership, mount/child failure, cleanup,
  serial/LAN CLI mismatch and UART session regression after real integration.
- LF1-FAULT-EXT: out-of-order delivery, old-session HELLO (current nonce only is
  deliberately retained), lost BYE, host restart, wrong sid/type, seq exhaustion.
- LF1-BOUNDARY-EXT: payload 0/512/513, explicit oversized-length rejection,
  tick wrap, BUSY/hard-error paths, hardware RX_WAIT full/recovery.
- LF3-HARDWARE: AF_PACKET capability/interface on notebook, 82557 external RX,
  60 s idle/rebegin, LAN hsync boot/bin, deployed image CRC and interruption.
- LF-NP21W: emulator end-to-end is a separate stage per PM F1; no LGY-98 work here.

Validation evidence lives under `/home/hight/os32-tmp/run/lanfs/lf1-*.log`;
only logs after the last source change count. Emulator/hardware are not run.
## L-F2 session / CLI / peer

`test_serialfs.py` の `lan_session` は実 SfsClient/SfsLan/session と実 UART gate、
偽 NIC/VFS/config/tick で mount/end、RX_WAIT、NIC 不在、HELLO/mount 失敗、
BYE 詰まり、受信状態の破棄、設定の再読、既定シリアル、固定 peer を検査する。
`test_lanfs.py` は実 Python Server に対する --peer/最初の HELLO 固定 (BYE 後も)、
クライアント HELLO peer、OUTGOING、起動時のリンク状態、送受信/handle の例外継続を検査。
`test_rshell_serial.py` は echo/開始行/exit/EOT、CRLF、排他オプション、実 run_line を検査。
`sh_status_host.c` は設定の保存・他キー保持・MAC 拒否・書込み失敗・子からの拒否を検査。
追加変異: SFS_LAN 無視、end の受信破棄除去、サーバの peer 照合除去。

## lanspeed

実 serialfs VFS もリンクする。HELLO 1024/旧ホスト512/serial提示512、LAN の
1024 B 読みを512+512、serial上限の512 B読みを508+4、最大応答1024/512、
1024 B書込みのpayload上限と内容を検査。µs時計は下位32bit境界をまたぐ400µs、
取得失敗時の標本除外。縮退を外す変異はRED。serial既定上限を1024にする変異は
check-serialfs-hostでRED。check-e82557-drv-hostはsendのC/非C/!OKとCX/CNAの
完了をtickなしで検査し、send/IRQの完了取得除去を別々にREDにする。
