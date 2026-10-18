import importlib.util
import os, pathlib, re, shlex, shutil, subprocess, tempfile
MAKE_TARGETS = ["all"]
# Compile commands outlive dry_run; keep their generated inputs until exit.
_DRY_RUN_DIRS = []
ARG_DROP = {"-o", "-MF", "-MT", "-MQ", "-I", "-isystem", "-iquote", "-idirafter"}
FLAG_DROP = {"-c", "-MMD", "-MD", "-MP", "-M", "-MM"}
ARG_KEEP = {"-include", "-imacros", "-x"}
SEPARATORS = {"&&", "||", ";", "|", "then", "do", "else"}


def _segments(line):
    try:
        toks = shlex.split(line, comments=False)
    except ValueError:
        toks = line.split()
    seg = []
    for t in toks:
        if t in SEPARATORS:
            if seg:
                yield seg
            seg = []
            continue
        if t.endswith(";") and len(t) > 1:
            seg.append(t[:-1])
            yield seg
            seg = []
            continue
        seg.append(t)
    if seg:
        yield seg


def _is_cross_cc(tok):
    b = os.path.basename(tok)
    return b.startswith("i386-elf-") and b.endswith("gcc")


def parse_compile_lines(text):
    """`make -n` の出力から i386-elf-gcc の -c の行を拾い、
    [{"src": 元の .c, "sig": 旗の組 (tuple), "cc": コンパイラ, "argv": 引数}] で返す。
    旗の組からは -I・依存生成・-c・-o と元のソースを外す (言語に効かないもの)。
    argv は依存生成・-c・-o だけを外した実際の引数 (前処理に使う)。"""
    units = []
    for line in text.splitlines():
        if "gcc" not in line:
            continue
        for seg in _segments(line):
            if not seg or not _is_cross_cc(seg[0]) or "-c" not in seg[1:]:
                continue
            sig = []
            srcs = []
            argv = []
            it = iter(seg[1:])
            for a in it:
                if a in ("-o", "-MF", "-MT", "-MQ"):
                    next(it, None)
                    continue
                if a not in FLAG_DROP:
                    argv.append(a)
                if a in ARG_DROP:
                    argv.append(next(it, ""))
                    continue
                if a in ARG_KEEP:
                    nx = next(it, "")
                    argv.append(nx)
                    sig.append(a)
                    sig.append(nx)
                    continue
                if a in FLAG_DROP or (a.startswith("-I") and len(a) > 2):
                    continue
                if not a.startswith("-") and a.endswith(".c"):
                    srcs.append(os.path.normpath(a))
                    continue
                sig.append(a)
            if len(srcs) != 1:
                continue
            units.append({"src": srcs[0], "sig": tuple(sig), "cc": seg[0], "argv": argv})
    return units

SAVED_SETTINGS = ("lgy98.flags",)
REAL_BUILD_OUT = "build/out"

JOBSERVER_RE = re.compile(r"^(-j\d*|--jobs(=\d+)?|--jobserver-(auth|fds)=.*)$")


def strip_jobserver(makeflags):
    """親の MAKEFLAGS から並列 make の引き継ぎ (-j…、--jobserver-auth=…、--jobserver-fds=…)
    だけを取り除き、残り (単文字旗の束の e など、` -- ` 以降の変数指定) はバイト列のまま返す。
    変数指定のエスケープの解釈は子の make 自身に任せる。"""
    mf = makeflags or ""
    if mf.startswith("-- "):
        return mf
    head, sep, tail = mf.partition(" -- ")
    kept = []
    skip_number = False
    for w in head.split():
        if skip_number and w.isdigit():
            skip_number = False
            continue
        skip_number = w in ('-j','--jobs')
        if not JOBSERVER_RE.match(w):
            kept.append(w)
    out = " ".join(kept)
    if sep:
        out = (out + " -- " if out else "-- ") + tail
    return out


def dry_run(root):
    """`make -n -B all`。親の make の旗と変数指定 (C_STD=…、-e など) は MAKEFLAGS のまま
    子に渡し、ジョブサーバの引き継ぎだけを切る (strip_jobserver)。BUILD_OUT は一時
    ディレクトリに向ける — config.mk の `$(shell mkdir -p $(BUILD_OUT) …)` は -n でも
    走るので、そのままだと実物の木 (写しの木なら symlink の先) に build/out を作り得る
    (コマンドラインの BUILD_OUT が MAKEFLAGS から来た指定より勝つ)。旗を変える保存設定
    (SAVED_SETTINGS、例: make kernel-lgy98 が残す lgy98.flags) は実物の build/out から
    一時の BUILD_OUT に写す。"""
    env = dict(os.environ)
    mf = strip_jobserver(env.get("MAKEFLAGS", ""))
    for k in ("MAKEFLAGS", "MFLAGS", "MAKE_TERMOUT", "MAKE_TERMERR"):
        env.pop(k, None)
    if mf:
        env["MAKEFLAGS"] = mf
    workspace = tempfile.TemporaryDirectory(prefix="c_dialect_out_")
    _DRY_RUN_DIRS.append(workspace)
    tmp = workspace.name
    out_dir = os.path.join(tmp, "out")
    os.makedirs(out_dir)
    from gen_memmap import write_layout_header
    write_layout_header(root, os.path.join(out_dir, "memmap_layout.h"))
    # make -n does not run the paired modules' C/header generator.
    # Keep these derived inputs beside the layout header for later AST reads.
    generator = pathlib.Path(root, "tools/gen_sqlite_imports.py")
    if generator.is_file():
        spec = importlib.util.spec_from_file_location("sqlite_dry_run", generator)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT = pathlib.Path(root)
        module.generate(pathlib.Path(out_dir, "sqlite-proto"),
                        pathlib.Path(root, "tools/prototypes/sqlite_imports.def"))
        for component in ("fep", "cd", "kcg", "fm", "pcm", "v86", "lan"):
            definition = pathlib.Path(root, "tools/prototypes/" + component + "_imports.def")
            if definition.is_file():
                module.generate(pathlib.Path(out_dir, component), definition, module=component)
    for name in SAVED_SETTINGS:
        src = pathlib.Path(root, REAL_BUILD_OUT, name)
        if src.is_file():
            shutil.copyfile(str(src), os.path.join(out_dir, name))
    cmd = (["make", "--no-print-directory", "-n", "-B"] + MAKE_TARGETS
           + ["BUILD_OUT=" + os.path.join(tmp, "out")])
    if env.get("OS32_CLANG_CROSS_FREE") == "1":
        # Supply only external link inputs, keeping all C prerequisites and
        # recipes in the real make graph. Never apply these to a build.
        inputs = pathlib.Path(tmp, "cross-free.mk")
        inputs.write_text("$(CROSS_DIR)/i386-elf/lib/libc.a: ;\n")
        cmd += ["-f", "Makefile", "-f", str(inputs)]
        if shutil.which("i386-elf-gcc", path=env.get("PATH")) is None:
            # Link flags query libgcc even under -n. This shim refuses every
            # other invocation, so an accidental compilation fails closed.
            compiler = pathlib.Path(tmp, "i386-elf-gcc")
            compiler.write_text(
                '#!/bin/sh\n'
                'if [ "$#" = 1 ] && [ "$1" = -print-libgcc-file-name ]; then\n'
                '    echo "${0%/*}/libgcc.a"\n'
                'else\n'
                '    echo "cross-free dry-run: unexpected GCC invocation" >&2\n'
                '    exit 1\n'
                'fi\n')
            compiler.chmod(0o700)
            env["PATH"] = tmp + os.pathsep + env.get("PATH", os.defpath)
    r = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True, env=env, stdin=subprocess.DEVNULL)
    return r.returncode, r.stdout, r.stderr
