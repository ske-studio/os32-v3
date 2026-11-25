"""Inventory the P0-8 consumers. Run after gen_deploy_set.py on the built tree."""
import argparse
import csv
import json
import posixpath
import re
import subprocess
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UNKNOWN = "未確定"
KEEP = "変わらない"
MOVE = "変わる"
REMOTE = "未確認 — 相手に聞く項目"
NOW = "docs/tasks/restructure/NOW.md"
RULES = "docs/tasks/restructure/RULES.md"
WORKFLOW = ".github/workflows/build.yml"


def judgment(path, kind):
    """Scope is the source/test moves. Build configuration and paths have separate owners."""
    if kind in ("配布名", "媒体内", "相手機"):
        return KEEP, "W/T 各波", RULES + " §1–3（ソース・検査の配置のみ）"
    if kind == "SDK入力" and (path == "../build/sdk" or path.startswith("../build/sdk/")):
        return KEEP, "W/T 各波", "docs/08_build.md §8（staged SDK は build/sdk/）"
    if path.startswith(("arch/", "kernel/", "platform/", "exec/")):
        return MOVE, "W 該当波", NOW + " 対応する sys/ 配下への移動予定"
    if path.startswith(("assets/", "userland/", "build/out/")):
        return KEEP, "W/T 各波", NOW + " 現置き場維持・ビルド出力"
    return UNKNOWN, "P1 対応表で該当波を決定", NOW + " 個別パスの行き先未確定"


def git(tree, *args):
    return subprocess.check_output(["git", "-C", tree, *args]).decode(errors="replace")


def git_lines(tree, ref, pattern):
    cmd = ["git", "-C", tree, "grep", "-InE", pattern, ref, "--", ".",
           ":(exclude)**/.env", ":(exclude).env", ":(exclude)docs/archive/**"]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, check=False)
    if result.returncode not in (0, 1):
        raise RuntimeError("git grep: " + str(tree))
    for line in result.stdout.decode(errors="replace").splitlines():
        _, file, number, content = line.split(":", 3)
        yield file, number, content


def external(rows, tree, who, ref):
    if who == "np21w-src":
        pattern = r'(os32(-v3)?/|build/out/kernel\.map|tools/[A-Za-z0-9_.-]+)'
        for file, n, line in git_lines(tree, ref, pattern):
            for m in re.finditer(r'(?:/home/[^/ ]+/)?(?:os32(?:-v3)?/)?(?:tools/[\w./-]+|build/out/kernel\.map|os32/\.mcp\.json)', line):
                path = m[0].rstrip("/.")
                local = re.sub(r'^.*?os32(?:-v3)?/', '', path)
                # Unqualified tools/ may refer to the emulator's own repository.
                if path == local and ((tree / local).exists() or not (ROOT / local).exists()):
                    continue
                rows(who, path, "外部文書" if file.endswith(".md") else "外部ソース",
                     f"{tree}@{ref}:{file}:{n}", lookup=local)
        return
    pattern = r'(OS32_SDK|\.\./|os32-v3/|OS32_ROOT)'
    sdk_root = None
    for file, n, line in git_lines(tree, ref, pattern):
        if file == "Makefile":
            match = re.fullmatch(r'OS32_SDK\s*\?=\s*(\S+)', line)
            if match:
                sdk_root = match[1]
                break
    for file, n, line in git_lines(tree, ref, pattern):
        if file.endswith(".md") or line.lstrip().startswith(("# ", "//", "/*")):
            continue
        src = f"{tree}@{ref}:{file}:{n}"
        for m in re.finditer(r'\$\(OS32_SDK\)(/[\w./-]+)', line):
            path = (sdk_root or "$(OS32_SDK)") + m[1]
            rows(who, path, "SDK入力" if sdk_root else "未解決", src)
        for m in re.finditer(r'(?:\.\./)+[\w./-]+|(?:os32-v3/|\$\(OS32_ROOT\)/)[\w./-]+', line):
            path = m[0]
            base = posixpath.dirname(file) if line.lstrip().startswith("#include") else ""
            if posixpath.normpath(posixpath.join(base, path)).startswith("../") or not path.startswith("../"):
                rows(who, path, "SDK入力" if file == "Makefile" and path == sdk_root else "木外参照", src)


def inventory(deploy_set, consumers):
    data = defaultdict(set)

    def add(who, path, kind, source, lookup=None):
        result, wave, reason = judgment(lookup or path, kind)
        status = REMOTE if who in ("実機ノート", "pc98emu") else "ローカル資料照合"
        data[who, path, kind, result, wave, reason, status].add(source)

    for who, tree in consumers:
        ref = (git(tree, "rev-parse", "HEAD").strip() if who == "np21w-src"
               else git(ROOT, "rev-parse", "HEAD:" + who).strip())
        external(add, tree, who, ref)

    def extract(who, file, regex, kind, prefix=""):
        for n, line in enumerate((ROOT / file).read_text().splitlines(), 1):
            for match in re.finditer(regex, line):
                add(who, prefix + match.group(1), kind, f"{file}:{n}")

    real = ".claude/skills/os32-realhw-update/SKILL.md"
    emu = ".claude/skills/os32-emu-remote/SKILL.md"
    for file in ("tools/systemd/os32-host-agent@.service", "tools/systemd/README.md"):
        extract("実機ノート", file, r'(/opt/os32/tools/[\w./@-]+)', "常駐・更新")
    extract("実機ノート", "tools/host_agent_service.py", r"TOOLS / '([^']+)'", "常駐・子プロセス",
            prefix="/opt/os32/tools/")
    for file in (real, "docs/decisions/realhw/RA266.md"):
        extract("実機ノート", file, r'((?:tools/)?(?:ci_fetch\.sh|lanfs_host\.py|rshell_serial\.py)|tools/hostdrv_deploy\.py)', "操作手順")
        extract("実機ノート", file, r'(hostdrv\.tar\.gz|\.deploy/manifest\.txt)', "配布名")
    extract("pc98emu", emu, r'`(C:\\[^`]+|~/os32pm-request-<N>\.md|~/os32-v3|np21x64w(?:-iN)?\.ini)`', "相手機")
    extract("pc98emu", emu, r'(hostdrv\.tar\.gz|os32-<枝>-<SHA7>)', "配布名")
    extract("pc98emu", emu, r'((?:tools/)?(?:driver|np21w_ctl|np21w_client)\.py|\.mcp\.json)', "操作手順")
    extract("pc98emu", emu, r"(C:\\[A-Za-z0-9_\\.%-]+)", "相手機")
    extract("pc98emu", emu, r"`(/mnt/c/os32|np21w_aidebug_<port>\.token)`", "相手機")
    fetch = "tools/ci_fetch.sh"
    extract("ci_fetch", fetch, r'startswith\("([^"]+)"\)', "配布名")
    extract("ci_fetch", fetch, r'(BUILD_INFO\.txt|SHA256SUMS)', "配布名")
    extract("ci_fetch", WORKFLOW, r'artifact=(os32-[^"]+)', "配布名")
    for n, line in enumerate((ROOT / WORKFLOW).read_text().splitlines(), 1):
        parts = line.split()
        if parts[:1] == ["cp"] and parts[-1].startswith("out/"):
            for path in parts[1:-1]:
                add("ci_fetch", parts[-1][4:] + path.split("/")[-1], "配布名", f"{WORKFLOW}:{n}")
    extract("ci_fetch", WORKFLOW, r'-czf out/(\S+)', "配布名")

    manifest = json.loads(deploy_set.read_text())
    for i, item in enumerate(manifest["files"]):
        src = f"{deploy_set}#/files/{i}"
        add("deploy-set", item["host"], "配備元", src)
        add("deploy-set", item["guest"], "媒体内", src)
    for i, item in enumerate(manifest["allow_list"]):
        add("deploy-set", item["guest"], "媒体内", f"{deploy_set}#/allow_list/{i}")
    extract("deploy-set", "tools/deploy_source_check.py", r'(build/out/deploy-set\.json)', "一覧入力")
    return data


def write_table(path, header, rows):
    with path.open("w", newline="") as f:
        writer = csv.writer(f, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--modules", required=True, type=Path)
    parser.add_argument("--np21w", required=True, type=Path)
    parser.add_argument("--deploy-set", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    consumers = [("np21w-src", args.np21w), ("apps", args.modules / "apps"), ("game", args.modules / "game")]
    data = inventory(args.deploy_set, consumers)
    counts = defaultdict(Counter)
    notices = defaultdict(set)
    for who, path, kind, result, wave, reason, status in data:
        counts[who][result] += 1
        notices[wave, who, result].add(path)
    write_table(args.output / "paths.tsv", ["相手", "パス", "種類", "判定", "波", "判定根拠", "実物", "参照元"],
                [(*key, " | ".join(sorted(sources))) for key, sources in sorted(data.items())])
    write_table(args.output / "notify.tsv", ["照合時期", "知らせる相手", "判定", "照合するパス"],
                [(*key, " | ".join(sorted(paths))) for key, paths in sorted(notices.items())])
    # The stage ticket gives the W range, but assigns no individual file to a wave.
    ticket = (ROOT / "docs/tasks/restructure/TASK_RESTRUCTURE.md").read_text()
    first, last = map(int, re.search(r"波 W(\d+)〜W(\d+)", ticket).groups())
    waves = []
    for wave in [f"W{i}" for i in range(first, last + 1)] + ["T1〜Tn（各波）"]:
        for who in sorted(counts):
            candidates = sorted({key[1] for key in data if key[0] == who and
                                 (key[3] == UNKNOWN or (wave.startswith("W") and key[3] == MOVE))})
            action = "旧パスが当該波の移動表と一致したら事前通知" if candidates else "移動による通知対象なし"
            waves.append((wave, who, action, " | ".join(candidates)))
    write_table(args.output / "waves.tsv", ["波", "相手", "通知条件", "照合候補"], waves)
    write_table(args.output / "remote.tsv", ["相手", "未確認のパス", "相手に聞く項目", "文書の所在"],
                [(key[0], key[1], "現在の版・枠でこのパスを使うか。別の配置なら実際のパスは何か。",
                  " | ".join(sorted(sources))) for key, sources in sorted(data.items()) if key[-1] == REMOTE])
    write_table(args.output / "counts.tsv", ["相手", MOVE, KEEP, UNKNOWN],
                [(who, c[MOVE], c[KEEP], c[UNKNOWN]) for who, c in sorted(counts.items())])


if __name__ == "__main__":
    main()
