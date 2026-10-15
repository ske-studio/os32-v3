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
