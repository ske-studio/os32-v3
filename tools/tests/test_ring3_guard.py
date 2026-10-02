"""WM の文脈では KAPI の出力検査を効かせない — 実物の exec/ring3_str.c と kernel/gui.c で。

票:   docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md (追補 2026-09-26)
記録: tools/tests/ring3_guard_tdd.md

GUI で filer.bin を起動すると窓が出ずに消え、fault_kill_count が +1 した。
WM (gshell、CPL=0) はアプリの syscall の中で走るので ring3_in_syscall = 1 の
まま、WM 自身のスタックの MouseInfo が「アプリの出力先」として検査され、
シェル帯には USER が無いので拒否 → アプリが kill。直しは「カーネルが WM の
コードへ入っている深さ」を gui_call / ポンプ / owner_exit で数え、判定を
ring3_guard_active(in_syscall, wm_depth) に寄せること。

見るもの:
  (1) 判定表 — in_syscall × wm_depth (負は安全側)
  (2) gui_call / gui_owner_exit の前後で深さが対になる (入れ子も)
  (3) アプリが登録したバッファ (fs/fd_redirect.c) は WM の中でも表を歩く
      — 門は呼び手の文脈ではなくポインタの由来で決める (代行レビュー P2)
  (4) KAPI ime_set_render (kernel/gui.c の gui_ime_set_render) は常駐側
      (owner 1、CPL=0 の直呼び) だけ、gshell の終了で NULL に戻る (同 P2)
  (5) gui_register も同じ門 (owner 1 かつ CPL=0 の直呼び)、gshell の
      top-level の登録は通る (代行レビュー P3、host §6)
  (6) WM の中で落ちた観測点 (静的): exec/exec.c の ring3_kill_kind が深さを
      0 に戻す**前**に ring3_wm_fault_count を数え、kernel/isr_handlers.c の
      2 つの kill の行が深さ 1 以上で " (in WM)" を足す (代行レビュー P3)

test_ring3_str.py と同じ様式 — ホスト ILP32 GNU11 で走らせたあと、同じ
ソースが i386-elf-gcc -Werror でも通ることを見る ([C1])。libc は使わない。

  python3 -B tools/tests/test_ring3_guard.py            # ホスト
  python3 -B tools/tests/test_ring3_guard.py --target   # + i386-elf の -Werror
  python3 -B tools/tests/test_ring3_guard.py --mutate   # 否定側 (写しの上で変異)
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
INC_DIRS = ("include", "kernel", "lib", "exec", "fs", "sdk/include/os32")
HOST_SRC = ROOT / "tools/tests/ring3_guard_host.c"
KERNEL_SRCS = [ROOT / "exec/ring3_str.c", ROOT / "kernel/gui.c", ROOT / "fs/fd_redirect.c"]

# 否定側: 実物の 1 行を壊すと RED になることを見る (写しの上で。ソースは触らない)。
MUTATIONS = [
    ("exec/ring3_str.c",
     "    if (wm_depth > 0) return 0;\n",
     "    (void)wm_depth;\n",
     "WM の文脈を見ない (= 直す前の判定)"),
    ("kernel/gui.c",
     "    ring3_wm_enter();\n    r = g_gui_handler(op, arg, res_owner_get());\n",
     "    r = g_gui_handler(op, arg, res_owner_get());\n",
     "gui_call が印を立てない"),
    ("kernel/gui.c",
     "    r = g_gui_handler(op, arg, res_owner_get());\n    ring3_wm_leave();\n",
     "    r = g_gui_handler(op, arg, res_owner_get());\n",
     "gui_call が印を下ろさない"),
    ("kernel/gui.c",
     "        ring3_wm_enter();\n        g_gui_handler(GUI_OP_OWNER_EXIT, 0, owner);\n",
     "        g_gui_handler(GUI_OP_OWNER_EXIT, 0, owner);\n",
     "owner_exit が印を立てない"),
    # Registered-buffer mutations now live in test_fd_redirect_d0a.py.
    # 代行レビュー P2 (2026-09-26): ime_set_render は常駐側だけ。
    ("kernel/gui.c",
     "    if (res_owner_get() != GUI_SHELL_OWNER || ring3_call_from_user()) {\n        gui_ime_render_rejected++;\n",
     "    if (0) {\n        gui_ime_render_rejected++;\n",
     "ime_set_render の門を外す (= 直す前)"),
    ("kernel/gui.c",
     "    if (res_owner_get() != GUI_SHELL_OWNER || ring3_call_from_user()) {\n        gui_ime_render_rejected++;\n",
     "    if (res_owner_get() != GUI_SHELL_OWNER) {\n        gui_ime_render_rejected++;\n",
     "ime_set_render の門が由来 (ring3_call_from_user) を見ない"),
    ("kernel/gui.c",
     "    if (res_owner_get() != GUI_SHELL_OWNER || ring3_call_from_user()) {\n        gui_ime_render_rejected++;\n",
     "    if (ring3_call_from_user()) {\n        gui_ime_render_rejected++;\n",
     "ime_set_render の門が owner を見ない"),
    ("kernel/gui.c",
     "        ime_set_render((void *)0);\n    }\n}\n",
     "    }\n}\n",
     "gshell の終了で FEP の描画先を戻さない"),
    # 代行レビュー P3 (2026-09-26): gui_register も同じ門。
    ("kernel/gui.c",
     "    if (res_owner_get() != GUI_SHELL_OWNER || ring3_call_from_user()) {\n        return OS32_ERR_INVAL;\n",
     "    if (res_owner_get() != GUI_SHELL_OWNER) {\n        return OS32_ERR_INVAL;\n",
     "gui_register の門が由来 (ring3_call_from_user) を見ない (= 直す前)"),
    ("kernel/gui.c",
     "    if (res_owner_get() != GUI_SHELL_OWNER || ring3_call_from_user()) {\n        return OS32_ERR_INVAL;\n",
     "    if (res_owner_get() != GUI_SHELL_OWNER || !ring3_call_from_user()) {\n        return OS32_ERR_INVAL;\n",
     "gui_register の門が top-level の正当な登録を断る (過剰)"),
    # 代行レビュー P3 (2026-09-26): WM の中で落ちた観測点 (静的検査)。
    ("exec/exec.c",
     "    if (kind == EXEC_KIND_FAULT && ring3_wm_depth > 0) {\n        ring3_wm_fault_count++;     /* 深さを 0 に戻す前に数える */\n    }",
     "    ring3_context_clear();\n    if (kind == EXEC_KIND_FAULT && ring3_wm_depth > 0) {\n        ring3_wm_fault_count++;     /* 深さを 0 に戻す前に数える */\n    }",
     "ring3_wm_fault_count を深さを 0 に戻した後で数える (常に 0)"),
    ("exec/exec.c",
     "        ring3_wm_fault_count++;     /* 深さを 0 に戻す前に数える */\n",
     "",
     "ring3_wm_fault_count を数えない"),
    ("kernel/isr_handlers.c",
     "        sputs(\" EIP=\"); sput_hex32(fault_eip);\n        if (ring3_wm_depth > 0) sputs(\" (in WM)\");\n",
     "        sputs(\" EIP=\"); sput_hex32(fault_eip);\n",
     "例外の kill の行に (in WM) を付けない"),
    ("kernel/isr_handlers.c",
     "        }\n        if (ring3_wm_depth > 0) sputs(\" (in WM)\");\n        sputs(\" -> kill app\\n\");\n",
     "        }\n        sputs(\" -> kill app\\n\");\n",
     "#PF の kill の行に (in WM) を付けない"),
]

# (6) の静的検査の対象。ring3_kill_kind と 2 つの kill の行は実物のホストでは
# 組めない (exec.c は大きく、ISR は入口の形が違う) ので、字面で見る。
WM_FAULT_COUNT = "ring3_wm_fault_count++;"
WM_DEPTH_RESET = "    ring3_wm_depth = 0;"
IN_WM_LINE = 'if (ring3_wm_depth > 0) sputs(" (in WM)");'


def _func_body(text, head):
    """head で始まる関数の本体 (先頭の `{` から対応する `}` まで)。無ければ ''。"""
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
    """(6) WM の中で落ちた観測点。落ちた項目の数を返す。"""
    bad = []
    exec_c = (root / "exec/exec.c").read_text(encoding="utf-8")
    body = _func_body(exec_c, "static void ring3_kill_kind(int kind)")
    c = body.find(WM_FAULT_COUNT)
    # d2 P3: clear is shared by exec_exit / exec_pending_transfer. Count before
    # either call, and also reject any early reset reintroduced in this body.
    resets = [body.find(token) for token in (WM_DEPTH_RESET, "ring3_context_clear();",
                                           "exec_exit(", "exec_pending_transfer(")]
    r = min((pos for pos in resets if pos >= 0), default=-1)
    if c < 0:
        bad.append("ring3_kill_kind が ring3_wm_fault_count を数えない")
    elif r < 0 or c > r:
        bad.append("ring3_kill_kind が深さを 0 に戻した後で数える (常に 0)")
    elif "ring3_wm_depth > 0" not in body[:c] or "EXEC_KIND_FAULT" not in body[:c]:
        bad.append("ring3_kill_kind の数える条件が深さ 1 以上 / FAULT でない")
    if "volatile u32 ring3_wm_fault_count = 0;" not in exec_c:
        bad.append("ring3_wm_fault_count が大域 (カーネルシンボル) でない")
    isr = (root / "kernel/isr_handlers.c").read_text(encoding="utf-8")
    for head in ("void exception_handler(", "void page_fault_handler("):
        fb = _func_body(isr, head)
        k = fb.find("ring3_fault_kill();")
        if k < 0 or IN_WM_LINE not in fb[:k]:
            bad.append("%s の kill の行に (in WM) が無い" % head.split("(")[0])
    if not quiet:
        for b in bad:
            print("  FAIL static: " + b)
        if not bad:
            print("STATIC (in WM) / ring3_wm_fault_count OK", flush=True)
    return len(bad)


def includes(root):
    return ["-I" + str(root / p) for p in INC_DIRS]


def run_host(root, tmp, quiet=False):
    exe = tmp / "ring3-guard"
    p = subprocess.run(["gcc", *FLAGS, "-O0", "-D__KERNEL_BUILD__", *includes(root),
                        "-nostdlib", "-static", "-no-pie",
                        str(root / "tools/tests/ring3_guard_host.c"), "-o", str(exe)],
                       cwd=root, capture_output=True, text=True)
    if p.returncode != 0:
        if not quiet:
            sys.stdout.write(p.stdout + p.stderr)
        return p.returncode
    return host32.run([str(exe)], cwd=root, timeout=30,
                          stdout=subprocess.DEVNULL if quiet else None).returncode


def mutate():
    """実物の写し (一時ディレクトリ) に変異を当てて、ホスト試験が落ちることを見る。"""
    red = 0
    # Each mutant owns one source copy; Rust build outputs are not test inputs.
    for i, (rel, old, new, why) in enumerate(MUTATIONS, 1):
        with tempfile.TemporaryDirectory(prefix="os32-ring3-guard-mut-") as tmp:
            copy = pathlib.Path(tmp)
            for d in INC_DIRS + ("tools/tests",):
                shutil.copytree(ROOT / d, copy / d, dirs_exist_ok=True,
                                ignore=shutil.ignore_patterns("target"))
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


def kapi_target_ok(root):
    """KAPI ime_set_render が門 (gui_ime_set_render) を通ること (sdk/kapi.json)。"""
    import json
    api = json.loads((root / "sdk/kapi.json").read_text(encoding="utf-8"))["api"]
    for f in api:
        if isinstance(f, dict) and f.get("name") == "ime_set_render":
            return f.get("target") == "gui_ime_set_render"
    return False


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        sys.exit(mutate())
    if not kapi_target_ok(ROOT):
        print("FAIL: sdk/kapi.json の ime_set_render の target が gui_ime_set_render でない")
        sys.exit(1)
    print("KAPI ime_set_render -> gui_ime_set_render OK", flush=True)
    if static_checks(ROOT):
        sys.exit(1)
    with tempfile.TemporaryDirectory(prefix="os32-ring3-guard-") as tmp:
        tmp = pathlib.Path(tmp)
        rc = run_host(ROOT, tmp)
        print("HOST ILP32 GNU11 EXIT ring3_guard_host=%d" % rc, flush=True)
        if rc == 0 and "--target" in sys.argv:
            for src in KERNEL_SRCS:
                subprocess.run(["i386-elf-gcc", *FLAGS, "-D__KERNEL_BUILD__",
                                *includes(ROOT), "-O2", "-c", str(src),
                                "-o", str(tmp / (src.stem + ".o"))],
                               cwd=ROOT, check=True)
            print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)
        sys.exit(rc)
