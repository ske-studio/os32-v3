#!/bin/sh
# 常駐の host agent (/opt/os32 と os32-host-agent@<instance>) を指定の commit に入れ替える。
# 使い方 (作業木のルートで、一般ユーザーのまま。sudo は中で呼ぶ):
#   tools/systemd/update_host_agent.sh <instance> [<commit>]   既定の commit は origin/main
#   tools/systemd/update_host_agent.sh <instance> --rollback   直前の版 (/opt/os32.prev) に戻す
# 手順: 一般ユーザーで clone (root の作業木を触らないので dubious ownership にならない)
#   → サービス停止 → /opt/os32 を /opt/os32.prev に退避して差し替え → 設定の置き場を 0755
#   → サービス起動 → probe の commit を照合。
set -eu

PREFIX=/opt/os32
PREV=/opt/os32.prev
CONF_DIR=/etc/os32/host-agent

die() { echo "update_host_agent: $*" >&2; exit 1; }

[ $# -ge 1 ] || die "使い方: $0 <instance> [<commit>|--rollback]"
inst=$1
want=${2:-origin/main}
unit="os32-host-agent@$inst.service"
repo=$(git rev-parse --show-toplevel) || die "git の作業木の中で実行する"
[ "$(id -u)" -ne 0 ] || die "root ではなく一般ユーザーで実行する (sudo は中で呼ぶ)"

probe() {  # probe の commit を出す。operator-endpoint は設定から読む
    sudo cat "$CONF_DIR/$inst.json" | python3 -c '
import json, socket, struct, sys, time
conf = json.load(sys.stdin)
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
'
}

if [ "$want" = "--rollback" ]; then
    sudo test -d "$PREV" || die "$PREV が無い"
    sudo systemctl stop "$unit"
    sudo mv "$PREFIX" "$PREFIX.rollback-$$"
    sudo mv "$PREV" "$PREFIX"
    sudo mv "$PREFIX.rollback-$$" "$PREV"
    sudo systemctl start "$unit"
    echo "戻した: probe commit=$(probe)"
    exit 0
fi

git -C "$repo" fetch -q origin || die "git fetch に失敗"
sha=$(git -C "$repo" rev-parse --verify "$want^{commit}") || die "commit $want が無い"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git clone -q --no-local "$repo" "$tmp/os32"
git -C "$tmp/os32" checkout -q --detach "$sha"

echo "入れ替え: $(sudo git -C "$PREFIX" rev-parse --short HEAD 2>/dev/null || echo なし) -> $(git -C "$repo" rev-parse --short "$sha")"
sudo systemctl stop "$unit"
sudo cp -a "$tmp/os32" "$PREFIX.new"
sudo chown -R os32-host:os32-host "$PREFIX.new"
if sudo test -d "$PREFIX"; then
    if sudo test -d "$PREV"; then sudo rm -rf -- "$PREV"; fi
    sudo mv "$PREFIX" "$PREV"
fi
sudo mv "$PREFIX.new" "$PREFIX"
if sudo test -d "$CONF_DIR"; then sudo chmod 0755 "$CONF_DIR"; fi
sudo systemctl start "$unit"

got=$(probe)
[ -n "$got" ] || { echo "probe が応答しない (journalctl -u $unit を見る)" >&2; exit 1; }
case "$sha" in
    "$got"*) echo "完了: $unit は $got (probe 一致)。戻すときは: $0 $inst --rollback" ;;
    *) echo "probe の commit が違う: 期待 $sha、probe '$got' (journalctl -u $unit を見る)" >&2; exit 1 ;;
esac
