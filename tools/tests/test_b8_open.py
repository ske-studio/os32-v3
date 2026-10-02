"""票 B8: 読み取り失敗を「不存在」として扱う経路を **vfs_open まで** 通して見る。

票:   docs/archive/shell/TASK_FS_TYPE.md §2 (B8)
記録: tools/tests/b8_tdd.md

tools/tests/b8_open_host.c が

  * 実物の fs/ext2_super.c / ext2_inode.c / ext2_dir.c / ext2_file.c /
    ext2_fmt.c / ext2_vfs.c / vfs.c / vfs_fd.c をそのまま #include し、
  * 贋物は Device API と IDE だけ、
  * RAM 上の 8MB ディスクを実物の ext2_format() で作って、
  * **間接ブロックを使う大きなディレクトリ**の読み取りを一度だけ失敗させる

という形で、次を確かめる。

  - FD が発行されない
  - write_file が呼ばれない (合成ドライバ側で呼び出し回数を数える)
  - 既存の中身が残る (open を通さず ext2_read_file で読み直して確認)

`vfs_path_kind` の戻り値までしか見ない試験では同じ形の欠陥 (B7) を往復 4 で
取り逃しているので、**判定を消費する側**まで動かす。

  python3 -B tools/tests/test_b8_open.py [--target]

--target を付けると、触った fs/*.c が実ビルドと同じ i386-elf クロスコンパイラでも
通ることを確かめる。make・エミュレータ・実配備には一切触れない。

票 B8 往復 5: **本物の `e2fsck -fn` を正解に加える** (抜き取り)。
試験バイナリが `@@E2FSCK <像> <media_ok> <ラベル>` を出して止まるので、ここで
e2fsck を当てて出力を「許容 (漏れ側)」「不整合」に分類し (tools/tests/b8_e2fsck.py)、
**自前の媒体検査 (media_ok) と食い違ったら失敗**にする。e2fsck が無い環境では
`E2FSCK SKIP` と明示して媒体検査だけで通す ([V4])。
"""
import host32
import collections
import os
import pathlib
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b8_e2fsck  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]

# u32 は unsigned long (include/types.h) なので **必ず ILP32 で組む**。
# ext2 のオンディスク配置を実物の構造体で扱うため、ここは譲れない。
HOST_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
              "-fno-stack-protector", "-nostdlib", "-static", "-O1",
              "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter", "-Wno-sign-compare",
              "-D__cdecl="]
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "fs", "lib", "kernel", "drivers",
                      "sdk/include/os32")]
SRC = ROOT / "tools/tests/b8_open_host.c"

# 票 B8 で触ったカーネル側。実ビルドと同じ素性で -Werror を通す ([C1])。
TARGET_SRCS = ["fs/ext2_inode.c", "fs/ext2_dir.c", "fs/ext2_file.c",
               "fs/ext2_vfs.c", "fs/ext2_super.c", "fs/vfs.c", "fs/vfs_fd.c"]
TARGET_FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-Wno-sign-compare", "-Wno-unused-parameter",
                # arch/x86 + platform/pc98: include/io.h は契約だけで、実装は
                # 固定名 arch_io.h / platform_io.h を引く (順序 3)。
                "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
                "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
                "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet",
                "-Ifs", "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]


def find_e2fsck():
    for cand in ("/usr/sbin/e2fsck", "/sbin/e2fsck"):
        if os.access(cand, os.X_OK):
            return cand
    return shutil.which("e2fsck")


# ---- 見張り (票 B8 往復 6、レビュー非 blocker 6) --------------------------
# PM の `make check` で、この試験が 2400 秒の打ち切りまで止まった (原因不明)。
# 以前は proc.wait() にも `for raw in proc.stdout` にも時間の上限が無かったので、
# 子 (試験バイナリ) がどこかで止まると make の打ち切りまで誰も気づかなかった。
# 実測では全体 約 11 秒、子の出力行の間隔は最大でも 1 秒未満 (e2fsck の実行を除く)。
# 閾値は十分に大きく取り、止まったら**どの像の後で止まったか**を出して失敗させる。
# 環境変数で縮められる (変異で見張りそのものを試すため)。
STALL_SEC = float(os.environ.get("B8_STALL_SEC", "180"))      # 子が 1 行も出さない最長
IMAGE_SEC = float(os.environ.get("B8_IMAGE_SEC", "600"))      # 次の像が来ない最長
TOTAL_SEC = float(os.environ.get("B8_TOTAL_SEC", "1500"))     # 全体 (make の 2400 秒より前)
E2FSCK_SEC = float(os.environ.get("B8_E2FSCK_SEC", "120"))    # e2fsck 1 回
EXIT_SEC = float(os.environ.get("B8_EXIT_SEC", "60"))         # 出力が閉じてから終了まで
MARKER = "@@E2FSCK "


def _pump(stream, q):
    """子の出力を 1 行ずつ queue へ (主のスレッドが時間の上限つきで待てるように)。"""
    try:
        for raw in iter(stream.readline, b""):
            q.put(raw)
    finally:
        q.put(None)


def run_with_e2fsck(exe, imgdir, e2fsck):
    """試験バイナリを動かし、@@E2FSCK の行ごとに e2fsck を当てる。"""
    stats = {"samples": 0, "clean": 0, "allowed": 0, "bad": 0, "mismatch": 0,
             "stall": 0}
    cats_allowed = collections.OrderedDict()
    cats_bad = collections.OrderedDict()
    mismatches = []
    proc = subprocess.Popen(host32.command([str(exe), str(imgdir)], cwd=ROOT), cwd=ROOT,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    q = queue.Queue()
    threading.Thread(target=_pump, args=(proc.stdout, q), daemon=True).start()
    start = last_image = time.monotonic()
    last_label = "(まだ像を 1 枚も受け取っていない)"
    tail = collections.deque(maxlen=15)
    stalled = None
    while True:
        now = time.monotonic()
        budget = min(STALL_SEC, TOTAL_SEC - (now - start), IMAGE_SEC - (now - last_image))
        if budget <= 0:
            stalled = ("全体の上限 %.0f 秒" % TOTAL_SEC if TOTAL_SEC - (now - start) <= 0
                       else "次の像が %.0f 秒来ない" % IMAGE_SEC)
            break
        try:
            raw = q.get(timeout=budget)
        except queue.Empty:
            now = time.monotonic()
            if TOTAL_SEC - (now - start) <= 0:
                stalled = "全体の上限 %.0f 秒" % TOTAL_SEC
            elif IMAGE_SEC - (now - last_image) <= 0:
                stalled = "次の像が %.0f 秒来ない" % IMAGE_SEC
            else:
                stalled = "子が %.0f 秒間 1 行も出力しない" % STALL_SEC
            break
        if raw is None:
            break
        line = raw.decode("utf-8", "replace")
        # 目印は行頭とは限らない: 手前に改行の無い出力が付くと行頭から外れ、以前は
        # 目印と気づかずに表示だけして**応答を返さず**、子は stdin で、こちらは
        # stdout で待ち合って止まる形になり得た。行のどこにあっても拾う。
        idx = line.find(MARKER)
        if idx < 0:
            sys.stdout.write(line)
            tail.append(line)
            continue
        if idx > 0:
            sys.stdout.write(line[:idx] + "\n")
            tail.append(line[:idx] + "\n")
        parts = line[idx:].rstrip("\n").split(" ", 3)
        if len(parts) < 3:
            mismatches.append((line.strip(), "malformed @@E2FSCK line", ""))
            stats["mismatch"] += 1
            proc.stdin.write(b"ok\n")
            proc.stdin.flush()
            continue
        path, media_ok = parts[1], parts[2]
        label = parts[3] if len(parts) > 3 else "(no label)"
        try:
            # stdin は必ず /dev/null: 端末を継いだ e2fsck が背景のジョブとして
            # 読み取りで止まる (SIGTTIN) 余地を残さない
            res = subprocess.run([e2fsck, "-fn", path], stdin=subprocess.DEVNULL,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 timeout=E2FSCK_SEC)
        except subprocess.TimeoutExpired:
            stalled = "e2fsck が %.0f 秒で終わらない (像: %s)" % (E2FSCK_SEC, label)
            last_label = label
            break
        last_label = label
        last_image = time.monotonic()
        out = res.stdout.decode("utf-8", "replace")
        stats["samples"] += 1
        allowed, bad = b8_e2fsck.classify(out, path)
        # -n では壊れたディレクトリブロックを直せないので e2fsck は「Salvage? no」の後で
        # 打ち切る (rc 12)。不整合の診断が出ていればそれを判定に使い、診断の無い
        # rc >= 8 だけを道具の失敗とする。
        if res.returncode >= 8 and not (bad and "aborted" in out):
            mismatches.append((label, "e2fsck operational error rc=%d" % res.returncode, out))
            stats["mismatch"] += 1
        else:
            for cat, l in allowed:
                cats_allowed.setdefault(cat, [0, l, label])[0] += 1
            for cat, l in bad:
                cats_bad.setdefault(cat, [0, l, label])[0] += 1
            if bad:
                stats["bad"] += 1
            elif allowed:
                stats["allowed"] += 1
            else:
                stats["clean"] += 1
            e2f_ok = not bad
            if e2f_ok != (media_ok == "1"):
                stats["mismatch"] += 1
                mismatches.append((label, "media_ok=%s e2fsck=%s" % (
                    media_ok, "ok" if e2f_ok else "inconsistent"), out))
        try:
            proc.stdin.write(b"ok\n")
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    if stalled:
        proc.kill()
        try:
            proc.wait(timeout=EXIT_SEC)
        except subprocess.TimeoutExpired:
            pass
        stats["stall"] = 1
        rc = 1
        print("E2FSCK STALL: %s — 最後に e2fsck へ渡した像: %s (経過 %.0f 秒、像 %d 枚)"
              % (stalled, last_label, time.monotonic() - start, stats["samples"]),
              flush=True)
        print("E2FSCK STALL: 止まる直前の子の出力:")
        for l in tail:
            sys.stdout.write("  | " + l)
        sys.stdout.flush()
    else:
        try:
            proc.stdin.close()
        except BrokenPipeError:
            pass
        try:
            rc = proc.wait(timeout=EXIT_SEC)
        except subprocess.TimeoutExpired:
            proc.kill()
            stats["stall"] = 1
            rc = 1
            print("E2FSCK STALL: 出力が閉じたのに子が %.0f 秒で終わらない — 最後の像: %s"
                  % (EXIT_SEC, last_label), flush=True)

    host32.report_signal(rc)

    print("E2FSCK samples=%d clean=%d allowed-only=%d inconsistent=%d mismatch=%d"
          % (stats["samples"], stats["clean"], stats["allowed"], stats["bad"],
             stats["mismatch"]), flush=True)
    print("E2FSCK 許容した分類:")
    for cat, (n, l, label) in cats_allowed.items():
        print("  %5d  %s\n         例: %s\n         (%s)" % (n, cat, l, label))
    print("E2FSCK 不整合の分類:")
    for cat, (n, l, label) in cats_bad.items():
        print("  %5d  %s\n         例: %s\n         (%s)" % (n, cat, l, label))
    for label, why, out in mismatches[:10]:
        print("E2FSCK MISMATCH: %s: %s\n%s" % (label, why, out))
    return rc, stats


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-b8-") as tmp:
        tmp = pathlib.Path(tmp)

        exe = tmp / "b8-open"
        subprocess.run(["gcc", *HOST_FLAGS, *INCLUDES, str(SRC), "-o", str(exe)],
                       cwd=ROOT, check=True)
        print("HOST GNU11 -Werror COMPILE PASS (real fs/ext2_*.c + vfs.c + vfs_fd.c)",
              flush=True)

        e2fsck = None if "--no-e2fsck" in sys.argv else find_e2fsck()
        mismatch = 0
        if e2fsck:
            rc, stats = run_with_e2fsck(exe, tmp, e2fsck)
            mismatch = stats["mismatch"] + stats["stall"]
            if stats["samples"] == 0:
                print("E2FSCK FAIL: no samples were produced", flush=True)
                mismatch = 1
        else:
            print("E2FSCK SKIP: e2fsck not found (or --no-e2fsck) — "
                  "media_check only, e2fsck cross-check NOT performed", flush=True)
            rc = host32.run([str(exe)], cwd=ROOT, timeout=900).returncode
        print("EXIT b8_open_host=%d" % rc, flush=True)

        if "--target" in sys.argv:
            for src in TARGET_SRCS:
                subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, "-c", src,
                                "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                               cwd=ROOT, check=True)
            print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)

        sys.exit(1 if (rc != 0 or mismatch) else 0)
