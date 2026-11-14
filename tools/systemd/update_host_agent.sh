#!/bin/sh
# 常駐の host agent (/opt/os32 と os32-host-agent@<instance>) を指定の commit に入れ替える。
# 使い方 (どこからでも、一般ユーザーのまま):
#   tools/systemd/update_host_agent.sh <instance> [<commit>]   既定の commit は origin/main
#   戻すときは、入れ替えの時に表示された前の commit を <commit> に渡す。
# 前提 (README の初回の設定、1 回だけ): /opt/os32 が一般ユーザーの持ち物の git 作業木で、
#   sudoers に os32-host-agent@<instance> の start・stop・restart だけを NOPASSWD で許してある。
# 手順: /opt/os32 で fetch → checkout --detach → sudo -n systemctl restart → probe の commit を照合。
set -eu

PREFIX=/opt/os32
CONF_DIR=/etc/os32/host-agent

die() { echo "update_host_agent: $*" >&2; exit 1; }

[ $# -ge 1 ] || die "使い方: $0 <instance> [<commit>]"
inst=$1
want=${2:-origin/main}
unit="os32-host-agent@$inst"
[ "$(id -u)" -ne 0 ] || die "root ではなく一般ユーザーで実行する"
[ -w "$PREFIX/.git" ] || die "$PREFIX が一般ユーザーの持ち物の git 作業木でない (README の初回の設定)"

probe() {  # probe の commit を出す。operator-endpoint は設定から読む
    python3 - "$CONF_DIR/$inst.json" <<'EOF'
import json, socket, struct, sys, time
conf = json.load(open(sys.argv[1]))
host, port = conf.get("operator-endpoint", "127.0.0.1:8027").rsplit(":", 1)
deadline = time.monotonic() + 20
while True:
    try:
        s = socket.create_connection((host, int(port)), 2)
        req = json.dumps({"op": "probe"}).encode()
        s.sendall(struct.pack("!I", len(req)) + req)
        n = struct.unpack("!I", s.recv(4, socket.MSG_WAITALL))[0]
        r = json.loads(s.recv(n, socket.MSG_WAITALL))
        print(r.get("commit", ""))
        break
    except OSError:
        if time.monotonic() > deadline:
            print("")
            break
        time.sleep(0.5)
EOF
}

git -C "$PREFIX" fetch -q origin || die "git fetch に失敗"
sha=$(git -C "$PREFIX" rev-parse --verify "$want^{commit}") || die "commit $want が無い"
old=$(git -C "$PREFIX" rev-parse HEAD)
git -C "$PREFIX" checkout -q --detach "$sha"
echo "入れ替え: $old -> $sha"
sudo -n systemctl restart "$unit" || die "sudo -n systemctl restart $unit が通らない (README の sudoers の行)"

got=$(probe)
[ -n "$got" ] || { echo "probe が応答しない (journalctl -u $unit を見る)" >&2; exit 1; }
case "$sha" in
    "$got"*) echo "完了: $unit は $got (probe 一致)。戻すときは: $0 $inst $old" ;;
    *) echo "probe の commit が違う: 期待 $sha、probe '$got' (journalctl -u $unit を見る)" >&2; exit 1 ;;
esac
