"""常駐 shell の内部入口 (T7b-b2 S3a) — 実物の exec/shell_service.c をホストで。

設計: ~/os32-tmp/run/t7bb12/DESIGN.final.md §3・§5 (b2① 段 A/B、b2③ WM 中の
read、b2⑤ 印の 0、b2⑥ reload)。

S3b: exec/exec.c の結線は、exec.c から関数・分岐を切り出して tmp の
wire_*.inc に書き、shell_service_host.c が実物として #include する
(ring3_wm_enter/leave、ring3_context_*、exec_run の shell 確定 = retire → publish、
exec_finish の shell 分岐 = 回収の後に retire)。dispatch 入口の印の 0 と
entry frame の 5 語 (frame[5]) は字面で見る (static_checks)。

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
HDR = "exec/shell_service.h"
EXEC = "exec/exec.c"
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
          "    if (!font_x87_present)",
     "    if (!font_x87_present)", "open の段 B を外す"),
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
    # ---- T7b-b3 S2: version 2 の font_guard_run (段 A/B) と x87 無しの NOSYS ----
    (SRC, "    if (!shell_caller_ok((u32)(uptr)__builtin_return_address(0), &reason))\n"
          "        return shell_service_reject(reason);\n"
          "    if (!shell_read_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_GUARD);\n",
     "    (void)reason;\n    if (!shell_read_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_GUARD);\n",
     "b3 S2 guard wrapper の段 A (戻り番地の範囲) を外す"),
    (SRC, "    if (!shell_read_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_GUARD);\n", "",
     "b3 S2 guard wrapper の段 B を外す"),
    (SRC, "    if (!shell_read_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_GUARD);\n",
     "    if (!shell_plain_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_GUARD);\n",
     "b3 S2 guard wrapper の段 B を open の文脈にする (WM 中の handler/pump を断る)"),
    (SRC, "    r = font_guard_run(call, ctx);\n",
     "    r = font_guard_run(call, ctx);\n    if (r < 0) r = 0;\n",
     "b3 S2 guard の NOSYS/BUSY を握りつぶす"),
    (SRC, "    g_shell_service.font_guard_run = shell_svc_font_guard_run;\n", "",
     "b3 S2 publish が font_guard_run を埋めない"),
    (SRC, "    if (!font_x87_present) return OS32_ERR_NOSYS;   /* x87 無しは raster できない → KCG */\n", "",
     "b3 S2 x87 無しでも shell の open を通す"),
    (HDR, "#define SHELL_SERVICE_VERSION 2u\n", "#define SHELL_SERVICE_VERSION 1u\n",
     "b3 S2 版を 1 のまま (欄を足したのに版を上げない)"),
    # ---- S3b: exec/exec.c の結線 ----
    (EXEC, "    if (ring3_wm_depth == 1) shell_service_wm_mark();\n", "",
     "b2③ ring3_wm_enter が印を立てない"),
    (EXEC, "    if (ring3_wm_depth == 0) ring3_wm_shell_gen = 0;\n}", "\n}",
     "ring3_wm_leave が深さ 0 で印を消さない"),
    (EXEC, "    ring3_wm_depth = 0;\n    ring3_wm_shell_gen = 0;\n}", "    ring3_wm_depth = 0;\n}",
     "b2⑤ ring3_context_clear の shell_gen=0 を外す"),
    (EXEC, "    ring3_wm_shell_gen = saved->wm_shell_gen;\n", "",
     "ring3_context_restore が親の印を戻さない"),
    (EXEC, "    ring3_wm_shell_gen = 0;   /* T7b-b2: 深さと同じく WM の印も 0 から始まる */\n", "",
     "b2⑤ dispatch 入口の印の 0 を外す"),
    (EXEC, "            shell_service_retire();\n            shell_service_publish(load_base, text_sz);\n",
     "            shell_service_publish(load_base, text_sz);\n",
     "shell 確定で retire せずに publish (旧 lease が残る)"),
    (EXEC, "            shell_service_retire();\n            shell_service_publish(load_base, text_sz);\n",
     "            shell_service_publish(load_base, text_sz);\n            shell_service_retire();\n",
     "shell 確定の順を publish → retire にする"),
    (EXEC, "        exec_reclaim_owned(id, kind);\n        /* T7b-b2 §3-1: shell の内部入口を外し",
     "        shell_service_retire();\n        exec_reclaim_owned(id, kind);\n"
     "        /* T7b-b2 §3-1: shell の内部入口を外し", "exec_finish の shell 分岐が回収より先に retire"),
    (EXEC, "         * 次の shell の publish はこの後 (exec_run の appslot_shell_commit 直後)。 */\n"
           "        shell_service_retire();\n",
     "         * 次の shell の publish はこの後 (exec_run の appslot_shell_commit 直後)。 */\n",
     "exec_finish の shell 分岐が retire しない"),
    (EXEC, "        frame[4] = is_shell ? (u32)(uptr)&g_shell_service : 0;\n",
     "        frame[4] = (u32)(uptr)&g_shell_service;\n", "app にも service 表の番地を渡す"),
    (EXEC, "                      (want_ring3 ? 5 : 4) * sizeof(u32)))",
     "                      (want_ring3 ? 4 : 3) * sizeof(u32)))", "entry frame を 4 語目なしで積む"),
]

# exec/exec.c の結線を切り出す (wire_*.inc)。(名前, 始まり, 終わり, 終わりを含むか)
WIRE = [
    ("wire_wm.inc", "void ring3_wm_enter(void)\n{", "\nvolatile u32 ring3_wm_depth_underflow", False),
    ("wire_wm.inc+", "void ring3_wm_leave(void)\n{", "\n}\n", True),
    ("wire_ctx.inc", "typedef struct {\n    CallerAccessFrame caller;",
     "\nvolatile u32 ring3_caller_reject_count", False),
    ("wire_commit.inc", "            appslot_shell_commit();\n",
     "        } else {\n            appslot_start_commit(", False),
    ("wire_finish.inc", "        res_owner_set(0);\n        exec_reclaim_owned(id, kind);\n",
     "    } else {\n#ifdef OS32_R1_FIXTURE", False),
]


def write_wire(root, out):
    """exec.c から結線の部分を切り出して out/wire_*.inc に書く。切り出せなければ例外。"""
    text = (root / EXEC).read_text(encoding="utf-8")
    files = {}
    for name, head, tail, keep in WIRE:
        if text.count(head) != 1:
            raise RuntimeError("exec.c: 切り出しの始まりが 1 か所でない: %r" % head)
        a = text.index(head)
        if name == "wire_finish.inc":
            a += len("        res_owner_set(0);\n")
        b = text.index(tail, a)
        if keep:
            b += len(tail)
        files.setdefault(name.rstrip("+"), []).append(text[a:b])
    for name, parts in files.items():
        (out / name).write_text("\n".join(parts) + "\n", encoding="utf-8")


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
    for head in ("shell_svc_font_source_open(", "shell_svc_font_source_read_at(",
                 "shell_svc_font_source_validate(", "shell_svc_font_source_close(",
                 "shell_svc_font_guard_run("):
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
    if "$(R1_OBJ)/exec/shell_service.o: private KERNEL_CFLAGS += -fno-optimize-sibling-calls" not in mk:
        bad.append("build/kernel.mk が r1 の shell_service.o を -fno-optimize-sibling-calls で組まない")
    if " exec/shell_service.c " not in mk or "| $(SHELL_SERVICE_OBJ)" in mk:
        bad.append("build/kernel.mk: shell_service.c が C_KERNEL に無い / 順序だけの前提が残る")
    # S3b: exec/exec.c の字面 (組めない部分)
    ex = (root / EXEC).read_text(encoding="utf-8")
    disp = _func_body(ex, "void __cdecl ring3_syscall_dispatch(u32 *frame)")
    if "    ring3_wm_depth = 0;\n    ring3_wm_shell_gen = 0;" not in disp:
        bad.append("dispatch 入口で ring3_wm_shell_gen を 0 にしない (深さの 0 の隣)")
    for want, why in (
            ("        u32 frame[5];\n", "entry frame が 5 語でない"),
            ("        new_esp = (stack_top - 4 * sizeof(u32)) & ~(u32)15;\n", "new_esp の式が設計と違う"),
            ("        frame[4] = is_shell ? (u32)(uptr)&g_shell_service : 0;\n",
             "frame[4] が shell だけに表の番地を渡さない"),
            ("                      want_ring3 ? frame : frame + 1,\n"
             "                      (want_ring3 ? 5 : 4) * sizeof(u32)))",
             "shell は frame+1 から 4 語 / CPL=3 は frame から 5 語 でない")):
        if want not in ex:
            bad.append(why)
    reclaim = _func_body(ex, "static void exec_reclaim_resources(int id)")
    if "    font_lease_close_owned(id);\n    vfs_close_owned(id);" not in reclaim:
        bad.append("exec_reclaim_resources: font_lease_close_owned が vfs_close_owned の直前に無い")
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
    wire = tmp / "wire"
    wire.mkdir(exist_ok=True)
    try:
        write_wire(root, wire)
    except (RuntimeError, ValueError) as e:
        if not quiet:
            print("  FAIL wire: %s" % e)
        return 1
    p = subprocess.run(["gcc", *FLAGS, "-O0", "-D__KERNEL_BUILD__", "-I" + str(wire), *includes(root),
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
            # exec/ は INC_DIRS で写る (exec.c を含む)
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
