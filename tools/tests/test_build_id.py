"""tools/gen_build_id.py (カーネルに埋め込むコミット ID) のホスト試験。

記録: tools/tests/vk32_crc_tdd.md (「コミット ID」の節)
票  : docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 A-4 (ユーザー指示: ver に Commit)

一時ディレクトリに git リポジトリを作り、実物の生成器を走らせて見る:
  - 変更なし → `git rev-parse --short=7 HEAD` と同じ
  - 追跡中のファイルを変える → "-dirty"、未追跡のファイルだけなら dirty にしない
  - リポジトリでない / git が PATH に無い → "unknown"
  - 中身が同じなら書き直さない (mtime が動かない = build_id.o を組み直さない)
  - 生成した C を gcc で組んで文字列を読むと同じ ID
  - 長さが BUILD_COMMIT_MAX (include/build_id.h) 未満

  python3 -B tools/tests/test_build_id.py            # 全ケース
  python3 -B tools/tests/test_build_id.py --mutate   # 否定側 (写しの上で変異 → RED)
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
GEN = ROOT / "tools/gen_build_id.py"
HDR = ROOT / "include/build_id.h"


class Fail(Exception):
    pass


def git(repo, *args):
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.invalid",
               GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.invalid")
    r = subprocess.run(["git", "-C", str(repo)] + list(args), capture_output=True,
                       text=True, env=env)
    if r.returncode != 0:
        raise Fail("git {} -> {}".format(args, r.stderr))
    return r.stdout.strip()


def gen(script, repo, out=None, env=None):
    cmd = [sys.executable, "-B", str(script), "--repo", str(repo)]
    cmd += ["-o", str(out)] if out else ["--print"]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise Fail("生成器が失敗: {}".format(r.stderr))
    return r.stdout.strip()


def commit_max():
    m = re.search(r"^#define\s+BUILD_COMMIT_MAX\s+(\d+)", HDR.read_text(encoding="utf-8"),
                  re.MULTILINE)
    if not m:
        raise Fail("include/build_id.h に BUILD_COMMIT_MAX が無い")
    return int(m.group(1))


def compiled_string(c_file, work):
    """生成した C を組んで os32_build_commit を読む (書式ごと確かめる)。"""
    main = pathlib.Path(work) / "m.c"
    main.write_text('#include <stdio.h>\nextern const char os32_build_commit[];\n'
                    'int main(void){ fputs(os32_build_commit, stdout); return 0; }\n',
                    encoding="utf-8")
    exe = pathlib.Path(work) / "m"
    r = subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Werror", str(main), str(c_file),
                        "-o", str(exe)], capture_output=True, text=True)
    if r.returncode != 0:
        raise Fail("生成した C が組めない: {}".format(r.stderr))
    return subprocess.run([str(exe)], capture_output=True, text=True).stdout


def run_all(script):
    lines = []
    with tempfile.TemporaryDirectory(prefix="build_id_") as work:
        work = pathlib.Path(work)
        repo = work / "repo"
        repo.mkdir()
        git(repo, "init", "-q")
        (repo / "a.txt").write_text("a\n")
        git(repo, "add", "a.txt")
        git(repo, "commit", "-q", "-m", "one")
        want = git(repo, "rev-parse", "--short=7", "HEAD")

        got = gen(script, repo)
        if got != want:
            raise Fail("clean: {} != {}".format(got, want))
        lines.append("clean: {} (= rev-parse --short=7)".format(got))

        (repo / "untracked.bin").write_text("x")
        got = gen(script, repo)
        if got != want:
            raise Fail("未追跡だけで dirty になった: {}".format(got))
        lines.append("untracked only: {} (dirty にしない)".format(got))

        (repo / "a.txt").write_text("b\n")
        got = gen(script, repo)
        if got != want + "-dirty":
            raise Fail("追跡中の変更で -dirty にならない: {}".format(got))
        lines.append("modified: {}".format(got))

        git(repo, "add", "a.txt")
        got = gen(script, repo)
        if got != want + "-dirty":
            raise Fail("index に載せた変更で -dirty にならない: {}".format(got))
        git(repo, "commit", "-q", "-m", "two")
        want2 = git(repo, "rev-parse", "--short=7", "HEAD")
        got = gen(script, repo)
        if got != want2 or want2 == want:
            raise Fail("commit 後: {} (want {})".format(got, want2))
        lines.append("staged: -dirty、commit 後: {}".format(got))

        # サブモジュールの中の変更では dirty にしない / 指すコミットが違えば dirty
        sub = work / "subsrc"
        sub.mkdir()
        git(sub, "init", "-q")
        (sub / "s.txt").write_text("s\n")
        git(sub, "add", "s.txt")
        git(sub, "commit", "-q", "-m", "s1")
        git(repo, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(sub), "apps")
        git(repo, "commit", "-q", "-m", "add sub")
        want3 = git(repo, "rev-parse", "--short=7", "HEAD")
        (repo / "apps" / "s.txt").write_text("changed\n")          # 中の追跡ファイル
        (repo / "apps" / "build.out").write_text("x")               # 中の生成物
        got = gen(script, repo)
        if got != want3:
            raise Fail("サブモジュールの中の変更で dirty になった: {}".format(got))
        git(repo / "apps", "commit", "-q", "-am", "s2")               # 指す先を動かす
        got = gen(script, repo)
        if got != want3 + "-dirty":
            raise Fail("サブモジュールの指すコミットが違うのに dirty にならない: {}".format(got))
        lines.append("submodule: 中の変更・生成物は数えない / 指す先が違えば -dirty")
        want2 = want3
        git(repo, "add", "apps")
        git(repo, "commit", "-q", "-m", "bump sub")
        want2 = git(repo, "rev-parse", "--short=7", "HEAD")

        norepo = work / "norepo"
        norepo.mkdir()
        got = gen(script, norepo)
        if got != "unknown":
            raise Fail("リポジトリでない: {}".format(got))
        env = dict(os.environ, PATH=str(work / "empty-bin"))
        (work / "empty-bin").mkdir()
        got = gen(script, repo, env=env)
        if got != "unknown":
            raise Fail("git が無い: {}".format(got))
        lines.append("no repo / no git: unknown")

        out = work / "out" / "build_id.c"
        gen(script, repo, out)
        text = out.read_text()
        if compiled_string(out, work) != want2:
            raise Fail("生成した C の文字列が {} でない:\n{}".format(want2, text))
        m1 = out.stat().st_mtime_ns
        time.sleep(0.05)
        gen(script, repo, out)
        if out.stat().st_mtime_ns != m1:
            raise Fail("中身が同じなのに書き直した (build_id.o が毎回組み直される)")
        (repo / "a.txt").write_text("c\n")
        gen(script, repo, out)
        if compiled_string(out, work) != want2 + "-dirty" or out.stat().st_mtime_ns == m1:
            raise Fail("ID が変わったのに書き直さない")
        lines.append("output: 同じなら書かない / 変われば書く、組んだ文字列が一致")

        cm = commit_max()
        longest = "f" * 40
        r = subprocess.run([sys.executable, "-B", "-c",
                            "import importlib.util,sys;"
                            "s=importlib.util.spec_from_file_location('g',sys.argv[1]);"
                            "g=importlib.util.module_from_spec(s);s.loader.exec_module(g);"
                            "print(g.COMMIT_MAX, g.HASH_MAX)", str(script)],
                           capture_output=True, text=True)
        cmax, hmax = (int(x) for x in r.stdout.split())
        if cmax != cm or hmax + len("-dirty") + 1 > cm or len(longest[:hmax]) < 7:
            raise Fail("長さの上限: 生成器 {} / {}、ヘッダ {}".format(cmax, hmax, cm))
        lines.append("length: ハッシュは最長 {} 文字 + '-dirty' + NUL <= {}".format(hmax, cm))
    return lines


MUTATIONS = [
    ("", "", "対照: 何も変えない (GREEN であること)"),
    ("return h + ('-dirty' if dirty else '')", "return h", "-dirty を付けない"),
    ("'--untracked-files=no'", "'--untracked-files=normal'", "未追跡のファイルでも dirty にする"),
    ("            if f.read() == text:\n                return 0\n",
     "            if f.read() == text and False:\n                return 0\n",
     "中身が同じでも書き直す"),
    ("    except (OSError, subprocess.SubprocessError):\n        return 'unknown'",
     "    except (OSError, subprocess.SubprocessError):\n        return '0000000'",
     "git が無いとき unknown にしない"),
    ("h = git(['rev-parse', '--short=7', 'HEAD'], cwd).strip()",
     "h = git(['rev-parse', 'HEAD'], cwd).strip()", "短縮しない"),
    ("COMMIT_MAX = 24", "COMMIT_MAX = 32", "ヘッダの上限とずれる"),
    ("                     '--ignore-submodules=dirty'], cwd)", "                     ], cwd)",
     "サブモジュールの中の変更でも dirty にする"),
]


def run_mutations():
    text = GEN.read_text(encoding="utf-8")
    red = green = error = 0
    for pat, rep, desc in MUTATIONS:
        with tempfile.TemporaryDirectory(prefix="build_id_mut_") as d:
            if pat and pat not in text:
                print("MUTATION ERROR (当たらない): {}".format(desc))
                error += 1
                continue
            m = pathlib.Path(d) / "gen_build_id.py"
            m.write_text(text.replace(pat, rep, 1) if pat else text, encoding="utf-8")
            try:
                run_all(m)
            except Fail as e:
                if not pat:
                    print("CONTROL RED (試験が壊れている): {}".format(e))
                    error += 1
                else:
                    print("MUTATION RED (期待どおり): {} -- {}".format(desc, str(e)[:90]))
                    red += 1
                continue
            if not pat:
                print("CONTROL GREEN (期待どおり): {}".format(desc))
                green += 1
            else:
                print("MUTATION 生き残り: {}".format(desc))
                error += 1
    print("MUTATE 内訳: RED {} / 対照 GREEN {} / 生き残り・ERROR {} (全 {})".format(
        red, green, error, len(MUTATIONS)))
    return error


def main(args):
    if shutil.which("git") is None:
        print("FAIL: git が無い (この試験は git のリポジトリを作る)")
        return 1
    try:
        for ln in run_all(GEN):
            print(ln)
    except Fail as e:
        print("FAIL:", e)
        return 1
    print("build_id PASS", flush=True)
    if "--mutate" in args:
        if run_mutations():
            print("MUTATE FAIL")
            return 1
        print("MUTATE PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
