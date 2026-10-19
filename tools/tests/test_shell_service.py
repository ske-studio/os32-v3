"""常駐 shell の内部入口 (T7b-b2 S3a) — 実物の exec/shell_service.c をホストで。

設計: ~/os32-tmp/run/t7bb12/DESIGN.final.md §3・§5 (b2① 段 A/B、b2③ WM 中の
read、b2⑥ reload)。exec/exec.c への結線 (publish / retire / wm_mark の呼出し、
frame[5]、ring3_context_clear の印) は S3b で、その試験もそこで足す。

見るもの (tools/tests/shell_service_host.c):
  (1) publish / retire — 表の印・版・関数 4 本・text 範囲・generation
  (2) b2① 段 A (戻り番地が現行 shell text の中・登録中) / 段 B (文脈)
  (3) b2③ 登録済み WM 区間の read は通り、印の無い・旧世代の区間は断る
  (4) b2⑥ retire → publish で旧 lease は STALE、旧 text は INVAL
加えて静的に: exec/shell_service.c の wrapper が noinline、build/kernel.mk が
このファイルを -fno-optimize-sibling-calls で組む (設計 §3-2)。

  python3 -B tools/tests/test_shell_service.py            # ホスト
  python3 -B tools/tests/test_shell_service.py --target   # + i386-elf の -Werror
  python3 -B tools/tests/test_shell_service.py --mutate   # 否定側 (写しの上で変異)
"""
import host32
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror"]
INC_DIRS = ("include", "kernel", "lib", "exec", "fs", "sdk/include/os32",
            "arch/x86", "platform/pc98")
SRC = "exec/shell_service.c"
KERNEL_MK = "build/kernel.mk"

# 否定側: 実物の 1 行を壊すと RED になることを見る (写しの上で。ソースは触らない)。
# (ファイル, 置き換え前 (ちょうど 1 か所), 置き換え後, 何を崩したか)
MUTATIONS = [
    (SRC, "    if (ret_addr < g_shell_text_lo || ret_addr >= g_shell_text_hi) {\n",
     "    if (0) {\n", "b2① 段 A の範囲比較を外す"),
    (SRC, "    if (!g_shell_active) {\n        *reason = SHELL_SVC_REJ_INACTIVE;",
     "    if (0) {\n        *reason = SHELL_SVC_REJ_INACTIVE;", "段 A が登録中を見ない"),
    (SRC, "    return ring3_wm_depth > 0 &&\n           ring3_wm_shell_gen == g_shell_service.generation;\n",
     "    return ring3_wm_depth > 0;\n", "b2① 段 B read の選言の片方 (印の世代) を外す"),
    (SRC, "    if (ring3_in_syscall != 0 || ring3_wm_depth != 0) return 0;\n",
     "    if (ring3_in_syscall != 0) return 0;\n", "段 B open が WM 中を断らない"),
    (SRC, "    if (!shell_plain_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_OPEN);\n"
          "    r = font_lease_shell_open(",
     "    r = font_lease_shell_open(", "open の段 B を外す"),
    (SRC, "    return slot && !slot->cpl3 && res_owner_get() == GUI_SHELL_OWNER &&\n",
     "    return slot && !slot->cpl3 &&\n", "段 B が res_owner を見ない"),
    (SRC, "    if (g_shell_active) ring3_wm_shell_gen = g_shell_service.generation;\n",
     "    (void)0;\n", "b2③ enter で印を立てない"),
    (SRC, "    font_lease_close_shell();\n    g_shell_active = 0;",
     "    g_shell_active = 0;", "b2⑥ retire が shell lease を閉じない"),
    (SRC, "    g_shell_service.generation++;\n", "", "b2⑥ publish が generation を進めない"),
    (SRC, "    r = font_lease_shell_read_at(id, g_shell_service.generation, offset, dst, len);",
     "    r = font_lease_shell_read_at(id, 1, offset, dst, len);",
     "段 C へ現 generation を渡さない"),
]


def _func_body(text, head):
    i = text.find(head)
    if i < 0:
        return ""
    j = text.find("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[j:k + 1]
    return ""


def static_checks(root, quiet=False):
    """wrapper の形 (noinline・volatile 受け) と kernel.mk の旗。落ちた数を返す。"""
    bad = []
    src = (root / SRC).read_text(encoding="utf-8")
    for name in ("open", "read_at", "validate", "close"):
        head = "shell_svc_font_source_%s(" % name
        i = src.find(head)
        decl = src[src.rfind("\n\n", 0, i):i] if i >= 0 else ""
        body = _func_body(src, head)
        if "noinline" not in decl:
            bad.append("%s が noinline でない" % head)
        if "volatile i32 r;" not in body or "__builtin_return_address(0)" not in body:
            bad.append("%s が戻り値を volatile に受けない / 戻り番地を見ない" % head)
    mk = (root / KERNEL_MK).read_text(encoding="utf-8")
    if "$(SHELL_SERVICE_OBJ): private KERNEL_CFLAGS += -fno-optimize-sibling-calls" not in mk:
        bad.append("build/kernel.mk が shell_service.o を -fno-optimize-sibling-calls で組まない")
    if not quiet:
        for b in bad:
            print("  FAIL static: " + b)
        if not bad:
            print("STATIC noinline / -fno-optimize-sibling-calls OK", flush=True)
    return len(bad)


def includes(root):
    return ["-I" + str(root / p) for p in INC_DIRS]


def run_host(root, tmp, quiet=False):
    exe = tmp / "shell-service"
    p = subprocess.run(["gcc", *FLAGS, "-O0", "-D__KERNEL_BUILD__", *includes(root),
                        "-fno-optimize-sibling-calls", "-nostdlib", "-static", "-no-pie",
                        str(root / "tools/tests/shell_service_host.c"), "-o", str(exe)],
                       cwd=root, capture_output=True, text=True)
    if p.returncode != 0:
        if not quiet:
            sys.stdout.write(p.stdout + p.stderr)
        return p.returncode
    return host32.run([str(exe)], cwd=root, timeout=30,
                      stdout=subprocess.DEVNULL if quiet else None).returncode


def target_compile(root, tmp):
    subprocess.run(["i386-elf-gcc", *FLAGS, "-D__KERNEL_BUILD__", *includes(root),
                    "-include", str(root / "sdk/include/os32/os32_unit_stamp.h"),
                    "-I" + str(root), "-I" + str(root / "sdk/include"),
                    "-O2", "-fno-optimize-sibling-calls", "-c", str(root / SRC),
                    "-o", str(tmp / "shell_service.o")], cwd=root, check=True)


def mutate():
    red = 0
    for i, (rel, old, new, why) in enumerate(MUTATIONS, 1):
        with tempfile.TemporaryDirectory(prefix="os32-shell-service-mut-") as tmp:
            copy = pathlib.Path(tmp)
            for d in INC_DIRS + ("tools/tests",):
                shutil.copytree(ROOT / d, copy / d, dirs_exist_ok=True,
                                ignore=shutil.ignore_patterns("target"))
            (copy / KERNEL_MK).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / KERNEL_MK, copy / KERNEL_MK)
            src = copy / rel
            text = src.read_text(encoding="utf-8")
            if text.count(old) != 1:
                print("MUTATION %d SKIP (pattern not found once): %s" % (i, why))
                return 1
            src.write_text(text.replace(old, new), encoding="utf-8")
            rc = run_host(copy, copy, quiet=True)
            if rc == 0:
                rc = static_checks(copy, quiet=True)
            state = "RED" if rc != 0 else "GREEN (bad)"
            print("MUTATION %d %s: %s" % (i, state, why), flush=True)
            if rc != 0:
                red += 1
    print("MUTATIONS %d/%d RED" % (red, len(MUTATIONS)))
    return 0 if red == len(MUTATIONS) else 1


if __name__ == "__main__":
    if static_checks(ROOT):
        sys.exit(1)
    with tempfile.TemporaryDirectory(prefix="os32-shell-service-") as tmp:
        tmp = pathlib.Path(tmp)
        rc = run_host(ROOT, tmp)
        print("HOST ILP32 GNU11 EXIT shell_service_host=%d" % rc, flush=True)
        if rc == 0 and "--target" in sys.argv:
            target_compile(ROOT, tmp)
            print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
        if rc:
            sys.exit(rc)
    sys.exit(mutate() if "--mutate" in sys.argv else 0)
