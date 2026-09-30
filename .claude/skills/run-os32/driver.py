#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""driver.py — OS32 を組み立てて、動いているゲストを叩くための 1 本の入口。

OS32 は WSL 上で組み、Windows 側の NP21/W (ai-debug fork) の中で動く。
「起動する」はホスト側のプロセス起動ではなく、**既に動いているゲストに
手を届かせる**こと。その手を 1 か所にまとめたのがこのファイル。

    python3 .claude/skills/run-os32/driver.py doctor
    python3 .claude/skills/run-os32/driver.py status
    python3 .claude/skills/run-os32/driver.py cmd 'ver'
    python3 .claude/skills/run-os32/driver.py cmd --wait 'kstr_bench > /tmp/k.txt'
    python3 .claude/skills/run-os32/driver.py pull /tmp/k.txt out.txt
    python3 .claude/skills/run-os32/driver.py shot screen.png
    python3 .claude/skills/run-os32/driver.py build
    python3 .claude/skills/run-os32/driver.py deploy
    python3 .claude/skills/run-os32/driver.py smoke

**このスクリプトは NHD とブート領域には一切触れない。** そこへの配備は
NP21/W を止めてから行う操作で ([D1])、承認の対象 ([D2])。ここが扱うのは
HostDrv 経由 (`make deploy` → ゲストの `hsync`) だけ。再起動は要らない。

終了コード: 0 = 成功 / 1 = 使い方の誤り / 2 = ゲストまたはホスト側の失敗。
"""

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))
BASE = os.environ.get("NP21W_AIDEBUG_URL", "http://127.0.0.1:8025")

# [V3] 遠隔実行の待ち時間は縮めない。15 秒未満にしないこと。
T_SHORT = 20          # status / screenshot
T_CMD = 60            # ゲストのシェル 1 行
POLL_EVERY = 5        # --wait の見張りの間隔 (秒)
POLL_MAX = 3600       # --wait の上限 (秒)
# ゲスト → ホストの持ち出し口。ゲストの /host がホストのここに見える。
HOSTDRV = "/mnt/c/os32"


def die(msg, code=2):
    sys.stderr.write("driver: %s\n" % msg)
    sys.exit(code)


def _http(path, body=None, timeout=T_SHORT, raw=False):
    url = BASE + path
    data = body.encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            blob = r.read()
    except urllib.error.URLError as exc:
        die("%s に届かない (%s)。NP21/W が動いているか、ini の "
            "aidebug=true / aidbport=8025 を確かめる" % (url, exc))
    except OSError as exc:
        die("%s で失敗: %s" % (url, exc))
    return blob if raw else blob.decode("utf-8", "replace")


def status():
    return json.loads(_http("/api/status"))


def cmd_once(line, timeout=T_CMD):
    """ゲストのシェルに 1 行送って出力を返す。長い処理は timeout する。"""
    return _http("/api/cmd", body=line, timeout=timeout)


def _read_done(path):
    """/mnt/c 側は書かれた直後に一覧が古いままのことがある。

    実測: ゲストが /host に作った直後、`ls` は「無い」と言うのに `cat` は
    読めた。存在確認ではなく**開いてみる**こと。
    """
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def run_waited(line, quiet=False):
    """長い処理を走らせ、**完了印**が現れるまで待って終了コードを返す。

    ゲストのシェルは 1 行実行している間 EOT を返さないので、長い処理は
    /api/cmd がそのまま時間切れになる。**時間切れは失敗ではない** —
    ゲストは走り続けている。

    終了の判定に CPU の居場所を使ってはいけない。`sleep` のように
    カーネルの中で待つプログラムは、**待機中のシェルと全く同じ番地**に居る
    (実測: どちらも sys_halt 0x0010d130)。CS も 0x0008 で区別できない。

    代わりにゲスト自身に印を置かせる。**/api/cmd は複数行を受け付ける**ので
    (実測)、2 行目に終了コードの書き出しを付ける。シェルは `;` での連結を
    持たない (字面のまま echo される) ので、この 2 行送りを使う。
    `source` も動く (2026-09-17 に再確認。出力が応答に乗らないだけで、
    副作用は出る) が、印を置くだけならこちらのほうが短い。
    ゲストの /host はホストの HOSTDRV にそのまま見えるので、ホスト側は
    ファイルの出現を待てばよい。
    """
    tok = "drv%d" % (int(time.time() * 1000) % 1000000000)
    done_guest = "/host/%s.done" % tok
    done_host = os.path.join(HOSTDRV, "%s.done" % tok)
    try:
        os.remove(done_host)
    except OSError:
        pass
    # 2 行目が印。`$?` はふつう正しい (実測: nosuchcmd → 127、echo → 0)。
    # ただし長く開いたままの rshell セッションでは 3 に固着することがある
    # (2026-09-17 実測。同時に source も効かなくなる)。開き直すと直る。
    # 値がおかしいと思ったら rshell を開き直してから測り直すこと。
    body = "%s\necho done $? > %s\n" % (line, done_guest)
    try:
        out = _http("/api/cmd", body=body, timeout=T_CMD)
        if not quiet:
            sys.stdout.write(out)
    except SystemExit:
        pass                       # 時間切れ = まだ走っている。印を待つ
    waited = 0
    while waited < POLL_MAX:
        txt = _read_done(done_host)
        if txt is not None and txt.strip():
            try:
                os.remove(done_host)
            except OSError:
                pass
            parts = txt.split()
            try:
                return int(parts[-1]), waited
            except (ValueError, IndexError):
                return None, waited
        if not quiet:
            sys.stderr.write("  待機 %ds\r" % waited)
            sys.stderr.flush()
        time.sleep(POLL_EVERY)
        waited += POLL_EVERY
    die("%d 秒待っても完了印 %s が現れなかった。ゲストが固まっている可能性が "
        "ある (CTRL+STOP で抜ける)" % (POLL_MAX, done_host))


def do_doctor():
    """前提が揃っているかを見る。値そのものは出さない ([D3])。"""
    ok = True

    def line(name, good, detail):
        sys.stdout.write("%-24s %s  %s\n" % (name, "OK " if good else "NG ", detail))

    cc = subprocess.run(["which", "i386-elf-gcc"], capture_output=True)
    have_cc = cc.returncode == 0
    line("i386-elf-gcc", have_cc,
         cc.stdout.decode().strip() if have_cc else "PATH に無い (INSTALL.md)")
    ok = ok and have_cc

    nasm = subprocess.run(["which", "nasm"], capture_output=True)
    line("nasm", nasm.returncode == 0,
         nasm.stdout.decode().strip() if nasm.returncode == 0 else "未導入")
    ok = ok and nasm.returncode == 0

    envp = os.path.join(ROOT, ".env")
    keys = []
    if os.path.exists(envp):
        with open(envp, encoding="utf-8", errors="replace") as f:
            for ln in f:
                ln = ln.strip()
                if ln and not ln.startswith("#") and "=" in ln:
                    keys.append(ln.split("=", 1)[0])
    need = ["CROSS_DIR", "NP21W_DIR", "HOSTDRV_DIR"]
    miss = [k for k in need if k not in keys]
    line(".env の鍵", not miss,
         "%d 個ある" % len(keys) if not miss else "足りない: %s" % ",".join(miss))
    ok = ok and not miss

    line("HostDrv", os.path.isdir(HOSTDRV),
         HOSTDRV if os.path.isdir(HOSTDRV) else "%s が無い" % HOSTDRV)

    try:
        st = status()
        line("NP21/W", True, "phase=%s protected_mode=%s eip=%s"
             % (st.get("phase"), st.get("protected_mode"), st.get("eip")))
    except SystemExit:
        line("NP21/W", False, "%s に届かない" % BASE)
        ok = False

    return 0 if ok else 2


def do_build(targets):
    """ビルドは常に .env のある本体で回す (worktree には .env が無い)。"""
    for t in targets:
        sys.stdout.write("=== make %s ===\n" % t)
        sys.stdout.flush()
        p = subprocess.run(["make", "-C", ROOT, t])
        if p.returncode != 0:
            die("make %s が失敗した (rc=%d)" % (t, p.returncode))
    return 0


def do_deploy():
    """HostDrv 経由だけ。NHD とブート領域には触らない ([D1])。"""
    p = subprocess.run(["make", "-C", ROOT, "deploy"])
    if p.returncode != 0:
        die("make deploy が失敗した (rc=%d)" % p.returncode)
    sys.stdout.write("=== ゲストで hsync ===\n")
    # hsync は数分かかることがあり、EOT を取り逃すことがある。
    # 時間切れを失敗にせず、落ち着くまで待ってから確かめる ([V4])。
    rc, waited = run_waited("hsync")
    sys.stdout.write("hsync 完了 (約 %d 秒、$? = %s)\n" % (waited, rc))
    if rc:
        sys.stdout.write("注意: hsync が非ゼロで終わった\n")
    sys.stdout.write("\n=== 反映の確認 ([V1] 文言では判断しない) ===\n")
    sys.stdout.write(cmd_once("ls -l /bin/sh.bin"))
    return 0


def do_pull(guest_path, out_path):
    """ゲストのファイルをホストへ持ち出す。

    大きな出力を /api/cmd の `cat` で引くと EOT を取り逃す。ゲスト側で
    /host へ写してからホストの実ファイルとして読むほうが確実。
    """
    name = os.path.basename(guest_path)
    out = cmd_once("cp %s /host/%s" % (guest_path, name), timeout=120)
    if "rror" in out or "not found" in out:
        die("ゲスト側の cp が失敗した:\n%s" % out)
    src = os.path.join(HOSTDRV, name)
    if not os.path.exists(src):
        die("%s に現れなかった。ゲストの /host が繋がっているか確かめる "
            "(実機には HostDrv が無い)" % src)
    with open(src, "rb") as f:
        blob = f.read()
    with open(out_path, "wb") as f:
        f.write(blob)
    sys.stdout.write("%s -> %s (%d バイト)\n" % (guest_path, out_path, len(blob)))
    return 0


def do_shot(out_path):
    """画面を取る。/api/screenshot は 640x400 の 4bit BMP を返す。

    .png を指定されたら PIL で変換する (BMP のままだと読み手が限られる)。
    PIL が無ければ BMP のまま置いて、そう言う。
    """
    blob = _http("/api/screenshot", timeout=30, raw=True)
    if not blob.startswith(b"BM"):
        die("BMP が返らなかった (先頭 %r)" % blob[:8])
    if out_path.lower().endswith(".png"):
        try:
            import io
            from PIL import Image
            Image.open(io.BytesIO(blob)).convert("RGB").save(out_path)
            sys.stdout.write("%s (PNG, BMP %d バイトから変換)\n"
                             % (out_path, len(blob)))
            return 0
        except ImportError:
            out_path = out_path[:-4] + ".bmp"
            sys.stdout.write("PIL が無いので BMP のまま置く\n")
    with open(out_path, "wb") as f:
        f.write(blob)
    sys.stdout.write("%s (%d バイト)\n" % (out_path, len(blob)))
    return 0


def do_smoke(shot_path):
    """動いているゲストに 1 往復して、生きていることを実際に示す。"""
    st = status()
    sys.stdout.write("phase=%s protected_mode=%s paging=%s eip=%s (%s)\n"
                     % (st.get("phase"), st.get("protected_mode"),
                        st.get("paging"), st.get("eip"), st.get("eip_sym")))
    if st.get("protected_mode") != 1:
        die("保護モードに入っていない。まだ起動途中か、落ちている")
    if st.get("fault_generation"):
        sys.stdout.write("注意: fault_generation=%s (過去に例外がある)\n"
                         % st.get("fault_generation"))
    for line in ("ver", "ls /bin", "echo os32-smoke-ok"):
        sys.stdout.write("\n$ %s\n" % line)
        sys.stdout.write(cmd_once(line))
    sys.stdout.write("\n")
    do_shot(shot_path)
    return 0


USAGE = __doc__


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(USAGE)
        return 1
    op = argv[1]
    rest = argv[2:]

    if op == "doctor":
        return do_doctor()
    if op == "status":
        sys.stdout.write(json.dumps(status(), indent=2, ensure_ascii=False) + "\n")
        return 0
    if op == "build":
        return do_build(rest or ["kernel", "programs"])
    if op == "deploy":
        return do_deploy()
    if op == "cmd":
        wait = False
        if rest and rest[0] == "--wait":
            wait = True
            rest = rest[1:]
        if not rest:
            sys.stderr.write("cmd: 実行する行が要る\n")
            return 1
        line = " ".join(rest)
        if wait:
            rc, waited = run_waited(line)
            sys.stdout.write("\n完了 (約 %d 秒)。$? = %s\n" % (waited, rc))
            return 0 if rc == 0 else 2
        else:
            sys.stdout.write(cmd_once(line))
        return 0
    if op == "pull":
        if len(rest) != 2:
            sys.stderr.write("pull: <ゲストのパス> <ホストのパス>\n")
            return 1
        return do_pull(rest[0], rest[1])
    if op == "shot":
        return do_shot(rest[0] if rest else "os32.png")
    if op == "smoke":
        return do_smoke(rest[0] if rest else "os32-smoke.png")

    sys.stderr.write("知らない命令: %s\n\n%s" % (op, USAGE))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
