"""shell の `peek` (userland/shell/cmd_peek.c) のホスト試験 (TASK_KPEEK §4、段 T3)。

実物の cmd_peek.c を tools/tests/kpeek_cli_host.c が #include し、KernelAPI の
kprintf / kdebug_call / version だけを偽物にして回す。見るのは引数の解釈、
64 KB 上限と幅の倍数、1 KB の分割、行の書式と 8 bit 和、最後の端数行、
CRC32・frames、誤りは 1 行だけ、NOSYS の案内。T5 の -l: probe と flags・tag・dst、
LAN では見出しと ok (tag) の 2 行だけ、使えなければ 1 行の後にシリアル版。

  python3 -B tools/tests/test_kpeek_cli.py            # 全ケース
  python3 -B tools/tests/test_kpeek_cli.py --mutate   # 否定側 (変異が RED になるか)
"""
CHECK = dict(name='kpeek-cli-host', tier=1,
    covers=['userland/shell/cmd_peek.c', 'userland/shell/shell.h', 'lib/crc32_core.inc', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_generated.h', 'tools/tests/kpeek_cli_host.c'],
    needs=[], budget=1.251798)

import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = "tools/tests/kpeek_cli_host.c"
SRC = "userland/shell/cmd_peek.c"
CASES = ["golden", "format", "split", "args", "errors", "lan"]
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-D__cdecl="]

# 否定側 (パターン, 置換, 説明)。どれも cmd_peek.c の 1 か所だけを壊す。
MUTATIONS = [
    (r"u8 sum = 0;", "u8 sum = 1;", "行の和の初期値を 0 でなくする"),
    (r"if \(n > KDEBUG_PEEK_CHUNK\) n = KDEBUG_PEEK_CHUNK;",
     "n = KDEBUG_PEEK_CHUNK;", "分割の端数を見ない (最後の要求も 1 KB)"),
    (r"if \(n > KPEEK_ROW_BYTES\) n = KPEEK_ROW_BYTES;",
     "n = KPEEK_ROW_BYTES;", "最後の行も 16 B 並べる (端数行が無くなる、K10)"),
    (r"len > KPEEK_CMD_MAX", "len >= KPEEK_CMD_MAX", "64 KB ちょうどを断る"),
    (r"kpeek_req\.status != KDEBUG_PEEK_OK", "kpeek_req.status == KDEBUG_PEEK_NOSYS",
     "kdebug の断りを見ない (読めていない data を出す)"),
    (r"\(space == KDEBUG_PEEK_SPACE_MMIO && !width_given\)",
     "(space == KDEBUG_PEEK_SPACE_MMIO && !width_given && 0)",
     "-m で -w を必須にしない"),
    (r"if \(rc == OS32_ERR_NOSYS\)", "if (rc == 0x7fff)",
     "KAPI の NOSYS を KDEBUG=1 の案内にしない"),
    (r"crc32_core_final\(crc\), frames", "crc32_core_final(crc) ^ CRC32_XOROUT, frames", "CRC32 の最後の XOR を忘れる"),
    (r"len - 1U > KPEEK_ADDR_LAST - addr", "len > KPEEK_ADDR_LAST - addr",
     "4 GB ちょうどで終わる範囲を断る"),
    (r"(rc = g_api->kdebug_call\(KDEBUG_OP_PEEK, &kpeek_req,\n\s*\(u32\)sizeof\(kpeek_req\)\);\n)\s*frames\+\+;",
     r"\1", "frames を数えない"),
    (r"if \(tag == kpeek_last_tag\) tag\+\+;", "", "同じ tick の 2 回目に同じ tag を使う"),
    (r"kpeek_req\.flags = KDEBUG_PEEK_F_LAN;", "kpeek_req.flags = 0;", "LAN の印を立てない"),
    (r"if \(lan == 1\) memset\(dst, 0, sizeof\(dst\)\);", "if (lan == 1) memset(dst, 0xFF, sizeof(dst));",
     "-l で宛先を空にしない (ペアを使わない)"),
    (r"(lan = kpeek_lan_probe\(dst\);)", r"\1 lan = lan || 1;", "probe の断りを見ない"),
    (r"    if \(lan\) \{\n        /\* 本文は LAN", "    if (0) {\n        /* 本文は LAN",
     "LAN でも 16 進行を出す"),
    (r"memcpy\(buf \+ off, kpeek_req\.data, n\);\n    \}\n",
     "memcpy(buf + off, kpeek_req.data, n);\n        g_api->kprintf(ATTR_WHITE, \"%s\", \"\");\n    }\n",
     "読んだ回ごとに kprintf (誤りの前に何か出す — 1 行 1 回を壊す)"),
]


def build(tmp, mutated=None, quiet=False):
    tmp = pathlib.Path(tmp)
    root = ROOT
    if mutated is not None:
        tree = tmp / "tree"
        for rel in (HARNESS, SRC, "userland/shell/shell.h", "lib/crc32_core.inc"):
            dst = tree / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(mutated if rel == SRC else
                           (ROOT / rel).read_text(encoding="utf-8"), encoding="utf-8")
        root = tree
    exe = tmp / "kpeek-cli-host"
    inc = ["-I" + str(root), "-I" + str(ROOT / "include"),
           "-I" + str(ROOT / "sdk/include/os32")]
    subprocess.run(["gcc", *FLAGS, *inc, str(root / HARNESS), "-o", str(exe)],
                   cwd=ROOT, check=True,
                   stderr=subprocess.DEVNULL if quiet else None)
    return exe


def run_cases(exe, cases, quiet=False):
    failed = 0
    for case in cases:
        rc = subprocess.run([str(exe), case], cwd=ROOT,
                            stderr=subprocess.DEVNULL if quiet else None).returncode
        if not quiet:
            print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    return failed


def mutate(tmp):
    bad = 0
    original = (ROOT / SRC).read_text(encoding="utf-8")
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        text, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = build(tmp, text, quiet=True)
        except subprocess.CalledProcessError:
            print(f"MUTATION {i} NOT COMPILABLE: {why}", flush=True)
            bad += 1
            continue
        hits = run_cases(exe, CASES, quiet=True)
        print(f"MUTATION {i} {'RED' if hits else '**GREEN (見逃し)**'} ({hits} 件): {why}",
              flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-kpeek-cli-") as tmp:
        exe = build(tmp)
        print("HOST GNU11 -Werror compile PASS (real userland/shell/cmd_peek.c)", flush=True)
        names = [a for a in args if not a.startswith("--")] or CASES
        rc = run_cases(exe, names)
        print(f"SUMMARY {len(names) - rc}/{len(names)} PASS", flush=True)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kpeek_cli.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
