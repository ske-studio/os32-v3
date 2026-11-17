# OS32 host agent の導入

管理者が NIC ごとに 1 instance を導入する。例の instance 名は `eth0`。
受入より前に導入し、サービスはゲストや受入台本と独立して常駐させる。
`/opt/os32` は probe が commit を読める Git 作業木とする。配置する commit を
事前に確定し、受入対象の作業木の HEAD と一致させる。

## Ubuntu ノート / systemd 有効の WSL

作業木のルートで管理者が実行する。`eth0` と設定内容は環境に合わせる。
Git、Python 3、systemd が必要。既存の `/opt/os32` がある場合はその更新方法を
管理者が決める。

```sh
sudo useradd --system --user-group --no-create-home os32-host
sudo install -d -o root -g os32-host -m 0755 /etc/os32/host-agent
sudo install -d -o os32-host -g os32-host -m 0750 /var/lib/os32-host-agent/eth0
git clone --no-local "$PWD" /tmp/os32-install
git -C /tmp/os32-install checkout --detach "$(git rev-parse HEAD)"
sudo cp -a /tmp/os32-install /opt/os32
sudo chown -R os32-host:os32-host /opt/os32
sudo install -o root -g os32-host -m 0644 tools/systemd/host-agent.example.json /etc/os32/host-agent/eth0.json
sudoedit /etc/os32/host-agent/eth0.json
sudo install -o root -g root -m 0644 tools/systemd/os32-host-agent@.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now os32-host-agent@eth0.service
```

設定の `iface`、絶対パスの `root`、固定ゲスト MAC の `peer`、`backend`、
`frame-endpoint`、`operator-endpoint`、`state-dir` を実環境に合わせる。
`state-dir` は例では `/var/lib/os32-host-agent/eth0`、`allow-write` は既定で
false。書き込みを許す場合は true にし、HostFS の root に対して os32-host に
必要な読み書き権限を与え、作成ファイルの所有者と権限を受入で確認する。
`root` がホームの下にある場合は、親ディレクトリを通れるよう `setfacl -m u:os32-host:--x /home/<ユーザー>` 等で ACL を設定する。
instance ごとに NIC、両 endpoint、state-dir を分ける。state-dir に世代や ID を
保存しない。現行 runner の NIC ロックは共有の `/run/lock` に置かれるため、
os32-host がそこに書ける必要がある。

`backend=bridge` は Ubuntu の物理 NIC を `lan_bridge.py` で frame endpoint に
中継する。`backend=np21w` は WSL の agent が frame endpoint で待ち、Windows の
NP21/W の NP2NETSOCK が接続する。例の `127.0.0.1:8026` はその待受先で、WSL の
NAT を物理 Ethernet の bridge として扱わない。

WSL の agent は NP21/W で開発するときだけ使う。WSL が動いている間は systemd が
サービスを起こすので、WSL を起こしたままにする仕掛け (ログオン時のタスクなど) は
置かない。unit の状態は `systemctl status os32-host-agent@eth0.service`、ログは
`journalctl -u os32-host-agent@eth0.service` で確認できる。

## 版の入れ替え (導入の後)

作業木のルートで、一般ユーザーのまま実行する (sudo は中で呼ぶ)。一般ユーザーで clone してから
root で `/opt/os32` に写すので、root の作業木を触らない。直前の版は `/opt/os32.prev` に残る。

```sh
tools/systemd/update_host_agent.sh <instance>            # origin/main に入れ替える
tools/systemd/update_host_agent.sh <instance> <commit>   # 指定の commit に
tools/systemd/update_host_agent.sh <instance> --rollback # 直前の版に戻す
```

サービスを止めて差し替え、`/etc/os32/host-agent` を 0755 にして (CLI の `--instance` が設定を読むため)
起動し、probe の commit が入れた commit と一致すれば完了。違えば rc=1 で止まる。

## 版の入れ替え (導入の後)

初回の設定 (1 回だけ、`<instance>` は NIC 名): `/opt/os32` を一般ユーザーの持ち物にし (サービスの os32-host は読むだけ)、
サービスの start・stop・restart だけを sudoers でパスワード無しにする。

```sh
sudo chown -R "$USER:$USER" /opt/os32 && sudo chmod -R a+rX /opt/os32
printf '%s ALL=(root) NOPASSWD: /usr/bin/systemctl start os32-host-agent@%s, /usr/bin/systemctl stop os32-host-agent@%s, /usr/bin/systemctl restart os32-host-agent@%s\n' \
  "$USER" <instance> <instance> <instance> | sudo tee /etc/sudoers.d/os32-host-agent >/dev/null
sudo chmod 0440 /etc/sudoers.d/os32-host-agent && sudo visudo -cf /etc/sudoers.d/os32-host-agent
sudo chmod 0755 /etc/os32/host-agent
```

以後の入れ替えはパスワード無しで回る (どこからでも、一般ユーザーのまま):

```sh
/opt/os32/tools/systemd/update_host_agent.sh <instance>            # origin/main に
/opt/os32/tools/systemd/update_host_agent.sh <instance> <commit>   # 指定の commit に (戻すときは表示された前の commit)
```

`/opt/os32` で fetch・checkout し、`sudo -n systemctl restart` して、probe の commit が入れた commit と一致すれば完了。
違えば rc=1 で止まる。サービスは別ユーザーなので、host agent は commit を `git -c safe.directory=*` で読む。

## systemd 無効の WSL

開発のときに、同じ設定で runner を手で起こす。専用ユーザーは作らなくてよい。
`state-dir` と NIC ロックの置き場 (`--lock-dir`) を、起こすユーザーが書ける場所にする。

```sh
python3 tools/host_agent_service.py --config <設定> --lock-dir <書ける場所>
```

## 受入時

受入台本は agent を起動・停止しない。probe で常駐、peer と READY を確認し、
probe の `commit` を受入対象の作業木の `git rev-parse HEAD` と照合する。一致しない、
空、または agent 不在なら前提不足として止める (A2)。再起動後の READY 回復は
管理者が別途サービスを再起動して確認し、台本は観測だけを行う。
