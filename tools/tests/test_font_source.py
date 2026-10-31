"""font の read lease (T7b-b1) — 実物の fs/vfs_fd.c と kernel/console/font_source.c をホストで。

設計: ~/os32-tmp/run/t7bb12/DESIGN.final.md §2・§5 (b1①〜⑦、b2②④ の lease 表側)。
S1 = VFS の内部 API (vfs_open_font / validate / read_at / close_font、公開経路の
締め出し、vfs_close_owned、FD 予算)。S2 = lease 表・app の門 (public KAPI
font_source_* の実体)・shell 側の段 C/D (lease_* の case)。S3b = exec/exec.c の
回収 hook (exec_reclaim_resources を exec.c から切り出して tmp の exec_reclaim.inc
に書き、lease_reclaim が実物として呼ぶ)。lease_limit は
app の合計上限だけを変えた変種 (VARIANT) でも回し、owner ごとの上限を見分ける。

  python3 -B tools/tests/test_font_source.py            # ホスト
  python3 -B tools/tests/test_font_source.py --target   # + i386-elf の -Werror
  python3 -B tools/tests/test_font_source.py --mutate   # 否定側 (写しの上で変異)
"""
CHECK = dict(name='font-source-host', tier=1,
    covers=['fs/vfs_fd.c', 'fs/vfs.h', 'kernel/console/font_source.c', 'kernel/console/font_source.h', 'kernel/gui.h', 'exec/exec.h', 'include/redir_access.h', 'fs/fd_redirect.h', 'exec/exec.c', 'tools/tests/font_source_host.c', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py'],
    needs=[], budget=3.680518)

from kernel_includes import include_dirs, include_flags
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["open_fields", "open_reject", "read_at", "validate", "close_reuse",
         "stale_close", "public_reject", "close_owned", "pinned_busy",
         "generic_regression", "budget",
         "lease_open", "lease_gate", "lease_token", "lease_read", "lease_close",
         "lease_limit", "lease_owned", "lease_shell", "lease_reclaim"]
# 変種: app の合計上限を owner ごとの上限より大きくして組む (値は両方 3 (F3) で、
# そのままでは owner ごとの判定が合計の判定に隠れる)
VARIANT_DEFS = ["-DFONT_SOURCE_HOST_APP_TOTAL=4"]
VARIANT_CASES = ["lease_limit", "lease_owned"]
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare", "-D__cdecl="]
INC_DIRS = tuple(d for d in dict.fromkeys(include_dirs()) if d != '.')
HOST = "tools/tests/font_source_host.c"
SRC = "fs/vfs_fd.c"
LEASE = "kernel/console/font_source.c"
EXEC = "exec/exec_finish.inc"
# --target で kernel と同じ include の並び (build/config.mk の INC_KERNEL)

# 否定側: 実物の 1 行を壊すと RED になることを見る (写しの上で。ソースは触らない)。
# (ファイル, 置き換え前 (ちょうど 1 か所), 置き換え後, 何を崩したか)
MUTATIONS = [
    (SRC, "    if (offset > f->size || len > f->size - offset) return VFS_ERR_INVAL;\n",
     "    if (offset > f->size) return VFS_ERR_INVAL;\n",
     "b1② len > size-offset の判定を外す"),
    (SRC, "    if (offset > f->size || len > f->size - offset) return VFS_ERR_INVAL;\n",
     "    if (len > f->size - offset) return VFS_ERR_INVAL;\n",
     "b1② offset > size の判定を外す (桁あふれ)"),
    (SRC, "    return fd_read_backend(f, offset, dst, len);\n",
     "    (void)fd_read_backend(f, offset, dst, len);\n    return (int)len;\n",
     "b1② backend の戻りを len に書き換える (短読を隠す)"),
    (SRC, "    if (f->generation != l->generation) return VFS_ERR_INVAL;\n", "",
     "b1④ generation の比較を外す"),
    (SRC, "    if (f->owner != l->owner) return VFS_ERR_INVAL;\n", "",
     "validate が owner を見ない"),
    (SRC, "    if (rc != VFS_OK && rc != VFS_ERR_STALE) return VFS_ERR_INVAL;\n",
     "    if (rc != VFS_OK) return VFS_ERR_INVAL;\n",
     "close_font が失効した FD を解放しない"),
    (SRC, "        if (f->lifetime == VFS_FD_FONT) { fd_release(f); continue; }\n", "",
     "b1⑤ close_owned の lifetime==FONT 条件を外す"),
    (SRC, "    open_files[fd].pinned = (flags & VFS_FONT_OPEN_PIN) ? 1 : 0;\n",
     "    open_files[fd].pinned = 0;\n", "b1⑦ pinned=(flags&PIN) → 0"),
    (SRC, "        if (open_files[fd].lifetime == VFS_FD_FONT) return;\n        fd_release(",
     "        fd_release(", "公開 vfs_close が FONT を閉じる"),
    (SRC, "    if (f->lifetime == VFS_FD_FONT) return VFS_ERR_INVAL; /* lease 経由だけ */\n"
          "    if (f->stale) return VFS_ERR_STALE;   /* fs_ctx より先に見る */\n",
     "    if (f->stale) return VFS_ERR_STALE;   /* fs_ctx より先に見る */\n",
     "公開 vfs_read_fd が FONT を断らない"),
    (SRC, "    if (f->lifetime == VFS_FD_FONT) return VFS_ERR_INVAL; /* 位置を持たない */\n", "",
     "公開 vfs_seek が FONT を断らない"),
    (SRC, "    if (!f->in_use || f->lifetime != VFS_FD_FONT) return VFS_ERR_INVAL;\n",
     "    if (!f->in_use) return VFS_ERR_INVAL;\n", "validate が lifetime を見ない"),
    # ---- S2 (S1 の取り込みで送られた項目) ----
    (SRC, "    open_files[fd].protect = (flags & VFS_FONT_OPEN_PROTECT) ? 1 : 0;\n",
     "    open_files[fd].protect = 0;\n", "protect=(flags&PROTECT) → 0"),
    # ---- S2: lease 表と app の門 (kernel/console/font_source.c) ----
    (LEASE, "    if (ring3_call_from_user() == 0)\n",
     "    if (0)\n", "門 1 (CPL=3 の dispatch 中) を外す"),
    (LEASE, "    if (!caller_access_get(c) || c->origin != CALLER_USER ||\n",
     "    if (!caller_access_get(c) ||\n", "門 2 が origin を見ない"),
    (LEASE, "        c->app_id != res_owner_get())\n",
     "        0)\n", "b1① c.app_id == res_owner_get() → 1"),
    (LEASE, "    if (s->vfs.owner != c->app_id)\n",
     "    if (s->vfs.owner != (int)c->owner)\n", "3d を AS の札 (caller.owner) で照合する"),
    (LEASE, "    return font_lease_install(FONT_LEASE_APP, kpath, c.app_id,",
     "    return font_lease_install(FONT_LEASE_APP, kpath, (int)c.owner,",
     "lease の owner を AS の札にする (回収 id と合わない)"),
    (LEASE, "    if (s->id_gen != id_gen)\n",
     "    if (s->id_gen != id_gen && 0)\n", "3b id_gen を見ない (再 close が STALE にならない)"),
    (LEASE, "    if (s->kind != FONT_LEASE_APP)\n",
     "    if (0)\n", "b2② 3c kind==APP の照合を外す"),
    (LEASE, "    if (s->vfs.owner != c->app_id)\n",
     "    if (0)\n", "3d 他 owner の id を通す"),
    (LEASE, "    if (s->owner_gen != c->generation)\n",
     "    if (0)\n", "b1① 3e owner_gen != c.generation → 0"),
    (LEASE, "    if (font_lease_count(FONT_LEASE_APP, c.app_id, 0) >= VFS_FONT_FD_MAX_APP ||\n",
     "    if (font_lease_count(FONT_LEASE_APP, c.app_id, 0) > VFS_FONT_FD_MAX_APP ||\n",
     "b1③ >= VFS_FONT_FD_MAX_APP → >"),
    (LEASE, "        font_lease_count(FONT_LEASE_APP, 0, 1) >= VFS_FONT_FD_MAX_APP_TOTAL)\n",
     "        font_lease_count(FONT_LEASE_APP, 0, 1) > VFS_FONT_FD_MAX_APP_TOTAL)\n",
     "b1③ >= VFS_FONT_FD_MAX_APP_TOTAL → >"),
    (LEASE, "                              VFS_FONT_OPEN_PIN, c.generation, out_id, out_size);\n",
     "                              0, c.generation, out_id, out_size);\n",
     "b1⑦ app の open が PIN を立てない"),
    (LEASE, "        ring3_fault_kill();\n        return font_lease_reject(FONT_LEASE_REJ_PATH",
     "        return font_lease_reject(FONT_LEASE_REJ_PATH", "写せない path で kill しない"),
    (LEASE, "    s->id_gen++;\n", "", "close が id_gen を進めない"),
    (LEASE, "            g_font_leases[i].id_gen < FONT_LEASE_GEN_MAX) return (int)i;\n",
     "            1) return (int)i;\n", "満了した枠を再利用する"),
    (LEASE, "        if (s->kind == FONT_LEASE_APP && s->vfs.owner == owner)\n",
     "        if (s->kind == FONT_LEASE_APP)\n", "b1⑤ close_owned が owner を見ない"),
    (LEASE, "        if (s->kind == FONT_LEASE_APP && s->vfs.owner == owner)\n",
     "        if (s->vfs.owner == owner)\n", "b1⑤ close_owned が shell の lease も閉じる"),
    (LEASE, "        if (s->kind == FONT_LEASE_APP && s->vfs.owner == owner)\n            (void)font_lease_release(s);\n",
     "        if (s->kind == FONT_LEASE_APP && s->vfs.owner == owner)\n            (void)0;\n",
     "b1⑤ close_owned が閉じない"),
    (LEASE, "    i32 rc = vfs_close_font(&s->vfs);\n",
     "    i32 rc = 0;\n", "release が VFS の FD を閉じない"),
    (LEASE, "    return font_lease_install(FONT_LEASE_SHELL, path, GUI_SHELL_OWNER,\n",
     "    return font_lease_install(FONT_LEASE_SHELL, path, res_owner_get(),\n",
     "b2④ GUI_SHELL_OWNER → res_owner_get()"),
    (LEASE, "                              VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN,\n",
     "                              VFS_FONT_OPEN_PIN,\n", "shell の open が PROTECT を立てない"),
    (LEASE, "    if (font_lease_count(FONT_LEASE_SHELL, 0, 1) >= VFS_FONT_FD_MAX_SHELL)\n",
     "    if (font_lease_count(FONT_LEASE_SHELL, 0, 1) > VFS_FONT_FD_MAX_SHELL)\n",
     "shell の上限 >= → >"),
    (LEASE, "    if (s->kind != FONT_LEASE_SHELL) return OS32_ERR_INVAL;\n", "",
     "段 C kind==SHELL の照合を外す"),
    (LEASE, "    if (s->owner_gen != shell_gen) return OS32_ERR_STALE;\n", "",
     "段 C 登録世代の照合を外す"),
    (LEASE, "        if (s->kind == FONT_LEASE_SHELL) (void)font_lease_release(s);\n",
     "        if (0) (void)font_lease_release(s);\n", "close_shell が閉じない"),
    # ---- S3b: exec/exec.c の回収 hook ----
    ('exec/exec_finish.inc', "    font_lease_close_owned(id);\n    vfs_close_owned(id);\n",
     "    vfs_close_owned(id);\n", "b1⑤ exec_reclaim_resources の font_lease_close_owned 呼出を外す"),
    ('exec/exec_finish.inc', "    font_lease_close_owned(id);\n    vfs_close_owned(id);\n    /* (4) パイプバッファ自動解放 */\n    pipe_free_owned(id);\n",
     "    vfs_close_owned(id);\n    /* (4) パイプバッファ自動解放 */\n    pipe_free_owned(id);\n    font_lease_close_owned(id);\n",
     "font_lease_close_owned を回収の後ろへずらす"),
]


def write_reclaim(root, out):
    """exec.c の exec_reclaim_resources を切り出して out/exec_reclaim.inc に書く"""
    text = (root / EXEC).read_text(encoding="utf-8")
    head = "static void exec_reclaim_resources(int id)\n{"
    if text.count(head) != 1:
        raise RuntimeError("exec.c に exec_reclaim_resources が 1 つでない")
    a = text.index(head)
    b = text.index("\n}\n", a) + 3
    (out / "exec_reclaim.inc").write_text(text[a:b], encoding="utf-8")


def includes(root):
    return include_flags(root)


def gui_shell_owner(root):
    """kernel/gui.h の GUI_SHELL_OWNER (64 ビットのホストでは gui.h を読めない)"""
    m = re.search(r"^#define\s+GUI_SHELL_OWNER\s+(\d+)\b",
                  (root / "kernel/gui.h").read_text(encoding="utf-8"), re.M)
    return "-DHOST_GUI_SHELL_OWNER=" + m.group(1)


def build(root, exe, quiet=False, defs=()):
    write_reclaim(root, exe.parent)
    p = subprocess.run(["gcc", *FLAGS, *defs, gui_shell_owner(root), "-I" + str(exe.parent),
                        *includes(root),
                        str(root / HOST), "-o", str(exe)],
                       cwd=root, capture_output=True, text=True)
    if p.returncode != 0 and not quiet:
        sys.stdout.write(p.stdout + p.stderr)
    return p.returncode


def run_cases(root, exe, quiet=False, cases=CASES):
    failed = 0
    for case in cases:
        p = subprocess.run([str(exe), case], cwd=root, capture_output=True, text=True)
        if not quiet:
            sys.stdout.write(p.stdout + p.stderr)
            print("EXIT %s=%d" % (case, p.returncode), flush=True)
        failed += p.returncode != 0
    if not quiet:
        print("SUMMARY %d/%d PASS" % (len(cases) - failed, len(cases)), flush=True)
    return failed


def target_compile(root, tmp):
    subprocess.run(["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
                    "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
                    "-mno-red-zone", "-fcommon", "-O2", "-Wall", "-Werror",
                    "-D__KERNEL_BUILD__", *includes(root), "-c", str(root / SRC),
                    "-o", str(tmp / "vfs_fd.o")], cwd=root, check=True)
    subprocess.run(["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
                    "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
                    "-mno-red-zone", "-fcommon", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-sign-compare",
                    "-D__KERNEL_BUILD__", *include_flags(root),
                    "-c", str(root / LEASE), "-o", str(tmp / "font_source.o")],
                   cwd=root, check=True)


def run_all(root, tmp, quiet=False):
    """通常の組みで全 case、変種の組みで VARIANT_CASES。戻り = 落ちた数 (組めないは -1)"""
    exe = tmp / "fd-host"
    vexe = tmp / "fd-host-variant"
    if build(root, exe, quiet=quiet) != 0 or build(root, vexe, quiet=quiet, defs=VARIANT_DEFS) != 0:
        return -1
    failed = run_cases(root, exe, quiet=quiet)
    if not quiet:
        print("VARIANT %s" % " ".join(VARIANT_DEFS), flush=True)
    return failed + run_cases(root, vexe, quiet=quiet, cases=VARIANT_CASES)


def mutate():
    red = bad = 0
    for i, (rel, old, new, why) in enumerate(MUTATIONS, 1):
        with tempfile.TemporaryDirectory(prefix="os32-font-source-mut-") as tmp:
            copy = pathlib.Path(tmp)
            for d in INC_DIRS:
                shutil.copytree(ROOT / d, copy / d, dirs_exist_ok=True)
            (copy / HOST).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / HOST, copy / HOST)
            shutil.copy2(ROOT / "tools/tests/input_fixture_host.h",
                         copy / "tools/tests/input_fixture_host.h")
            src = copy / rel
            text = src.read_text(encoding="utf-8")
            if text.count(old) != 1:
                print("MUTATION %d SKIP (pattern not found once): %s" % (i, why))
                return 1
            src.write_text(text.replace(old, new), encoding="utf-8")
            failed = run_all(copy, copy, quiet=True)
            if failed < 0:
                # 組めない変異は RED に数えない
                print("MUTATION %d BUILD FAIL (not RED): %s" % (i, why), flush=True)
                bad += 1
                continue
            state = "RED" if failed else "GREEN (bad)"
            print("MUTATION %d %s: %s" % (i, state, why), flush=True)
            red += failed != 0
    print("MUTATIONS %d/%d RED" % (red, len(MUTATIONS)))
    return 0 if red == len(MUTATIONS) and not bad else 1


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-font-source-") as tmp:
        tmp = pathlib.Path(tmp)
        rc = build(ROOT, tmp / "fd-host-check")
        if rc:
            sys.exit(rc)
        print("COMPILE GNU11 -Werror PASS", flush=True)
        if "--target" in sys.argv[1:]:
            target_compile(ROOT, tmp)
            print("TARGET i386-elf GNU11 -Werror PASS", flush=True)
        if run_all(ROOT, tmp) != 0:
            sys.exit(1)
    sys.exit(mutate() if "--mutate" in sys.argv[1:] else 0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_font_source.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
