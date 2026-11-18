# tools/lanfs_host.py — LAN 版 SerialFS のホスト (HostFS と仮想の像)

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `tools/lanfs_host.py`・`tools/host_image.py`・`tools/serialfs_host.py` (`HostFS`・`Server`)・`tools/lan_bridge.py` (`NicPort`)

ゲストの側は `docs/design/fs/serialfs/serialfs_persistent.md` と `docs/design/fs/serialfs/sfs_block.md`。

## 何をするか

- `py-raw tools/lanfs_host.py --iface <NIC> --root <DIR> [--allow-write PATH]... [--image ALIAS=PATH]... [--peer MAC]`。AF_PACKET の 1 本のループで、EtherType 0x88B5 のうち先頭が `05 53 46` (SFS) のフレームだけを扱う。link のフレームは `host_agent.py` (と `lan_bridge.py`) の担当で、同じ NIC・同じ送信元 MAC で同居する。
- `--root` の下を `/host` として見せる (既存の `HostFS`・`Server`、SFS の操作は STAT・LIST・READ・WRITE・MKDIR・RMDIR・UNLINK・RENAME)。

## 書き込みの許可

- 既定は読み取り専用。`--allow-write PATH` (root からの相対、繰り返し可) を指定した所とその下だけ書ける。拒否は `ROFS` で返り、ゲストは VFS の誤りとしてそのまま返す。OS32 の側に許可の設定は持たない。

## 相手とセッション

- 相手 (peer) は最初の HELLO の送り手に固定し、BYE やセッションの替わりをまたいで保つ。`--peer` を指定すれば最初から固定。宛先が自分でないフレームは捨てる。
- 知らない sid の要求には `SFS_T_ERR` (ゲストは `STALE`)。ゲストはこれでホストの再起動を知るので、永続の世代やエージェントとの IPC は要らない。
- 同じ sid/seq の重複は `Server` の応答のキャッシュで二重に実行しない (再送された write も 1 回だけ)。seq は LE16 で一周しない (1 セッション最大 65,534 要求、ゲストは 0xF000 で張り直す)。

## 仮想の像 (`tools/host_image.py` の `ImageStore`)

- `--image ALIAS=PATH` で alias ごとに像を公開する。BOPEN・BREAD・BCLOSE (0x30〜0x32) をここへ渡し、セッションの開始・BYE で `open_session`・`close_session` を呼ぶ。
- sid ごとに読み取り専用の FD を持つ。handle は閉じた物を再利用しない (セッションをまたいでも)。
- 同一性は `os.fstat` の (size, mtime_ns)。BREAD の前後で比べて違えば `STALE`。範囲外は `INVAL`、短い読みは `IO`、知らない sid・handle は `STALE`、無い alias は `NOTFOUND`。
- size が 512 の倍数でない像、セクタ数が u32 を超える像は BOPEN で `INVAL`。

## 確かめ

- `tools/tests/test_lanfs.py` (`lanfs_host.c`)・`test_hostsvc_image.py`・`test_hostsvc_block.py`・`test_hostsvc_sfs_lan.py`・`test_hostsvc_serialfs.py` (--allow-write の有無の差、応答の喪失と再送で 1 回だけ書く)。
