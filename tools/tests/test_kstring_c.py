"""移植準備 順序 4-b: lib/kstring_asm.asm (x86) と lib/kstring_c.c (C 版) の答え合わせ。

票:   移植準備 順序 4-b (lib/kstring_asm.asm の C 版を用意する)

**x86 の既定ビルドはアセンブリのまま。** ここで見るのは「C 版に差し替えても
結果が 1 バイトも変わらない」ことだけで、切り替えは別の判断。

やり方は tools/tests/kstring_c_host.c の頭に書いた。要点だけ:

  * 実物の lib/kstring_asm.asm を nasm -f elf32 で組み、objcopy で 13 本の
    シンボルを a_* に改名する。
  * 実物の lib/kstring_c.c をホスト ILP32 GNU89 (-m32) でコンパイルし、同じ
    13 本を c_* に改名する。
  * 両方を 1 つの実行ファイルにリンクし、同じ入力で**戻り値とバッファの
    全内容 (前後の番兵を含む)** を突き合わせる。食い違ったら C 版が悪い
    (アセンブリ版が「正」)。

突き合わせる入力: 空 / 1 バイト / 4・8・16 の前後 / 0x80 以上のバイトを含む列
(日本語の UTF-8) / n が長さより長い・短い・0 / 重なりコピー (前方・後方) /
kstrncpy の埋め。kernel/kselftest.c の test_str の既存ケースも両版で通す。

  python3 -B tools/tests/test_kstring_c.py [--mutate]

--mutate は**否定側**。C 版をわざと壊した版で試験が落ちることを見る。中心は
`kstrcmp` の比較を**符号付き**に変える変異 (0x80 以上のバイト = 日本語
ファイル名の並び順がアセンブリ版と食い違う。build/config.mk が -fsigned-char
を固定した理由そのもの)。

make・エミュレータ・実配備・libc には一切触れない。
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]

ASM_SRC = ROOT / "lib/kstring_asm.asm"
C_SRC = ROOT / "lib/kstring_c.c"
HOST_SRC = ROOT / "tools/tests/kstring_c_host.c"

# lib/kstring_asm.asm が global 宣言している 13 本。ここが増減したら
# 試験も追随させる (下の check_symbol_set が食い違いを検出する)。
FUNCS = ["kmemcpy", "memcpy", "kmemset", "memset",
         "kstrlen", "strlen", "kstrcmp", "strcmp",
         "kstrncmp", "strncmp", "kstrcpy", "kstrncpy", "memcmp"]

# u32 は unsigned long (include/types.h) なので **必ず ILP32 で組む**。
# build/config.mk の CFLAGS_COMMON と同じ素性 (-fsigned-char / -fno-short-enums
# を含む) に -Wextra -Werror -Wdeclaration-after-statement を足したもの。
COMMON_FLAGS = ["-std=gnu89", "-ffreestanding", "-fno-pie",
                "-fno-stack-protector", "-fcommon",
                "-fsigned-char", "-fno-short-enums",
                "-Wall", "-Wextra", "-Werror",
                "-Wdeclaration-after-statement"]
X86_FLAGS = ["-m32", "-march=i386", "-mno-red-zone"]
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "lib", "sdk/include/os32")]


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, check=True, **kw)


def write_renames(path, prefix):
    path.write_text("".join("%s %s%s\n" % (f, prefix, f) for f in FUNCS),
                    encoding="ascii")


def symbols(obj, nm="nm"):
    out = subprocess.run([nm, str(obj)], cwd=ROOT, check=True,
                         capture_output=True, text=True).stdout
    defined, undef = set(), set()
    for line in out.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "U":
            undef.add(parts[1])
        elif len(parts) == 3 and parts[1] in "TtDdBbRrWw":
            defined.add(parts[2])
    return defined, undef


def check_symbol_set(obj, label, nm="nm"):
    """13 本が揃っていること、**外部呼び出しが 1 本も無い**ことを見る。

    後者が要る理由: GCC はループを memset / memcpy の呼び出しに畳むことが
    ある。kstring_c.c でそれが起きると自分自身の無限再帰になる。
    """
    defined, undef = symbols(obj, nm)
    missing = [f for f in FUNCS if f not in defined]
    bad = 0
    if missing:
        print("SYM %-12s 足りない: %s" % (label, " ".join(missing)), flush=True)
        bad += 1
    else:
        print("SYM %-12s 13 本すべて定義" % label, flush=True)
    if undef:
        print("SYM %-12s **未定義参照あり (自己再帰の危険): %s**"
              % (label, " ".join(sorted(undef))), flush=True)
        bad += 1
    return bad


def build_exe(tmp, c_src, name):
    """asm 版 + C 版 + 駆動部を 1 つの実行ファイルに固める。"""
    asm_o = tmp / (name + "-asm.o")
    asm_r = tmp / (name + "-asm-r.o")
    c_o = tmp / (name + "-c.o")
    c_r = tmp / (name + "-c-r.o")
    exe = tmp / name

    run(["nasm", "-f", "elf32", str(ASM_SRC), "-o", str(asm_o)])
    write_renames(tmp / "ren_a.txt", "a_")
    run(["objcopy", "--redefine-syms=" + str(tmp / "ren_a.txt"),
         str(asm_o), str(asm_r)])

    run(["gcc", *COMMON_FLAGS, *X86_FLAGS, "-O2", *INCLUDES,
         "-c", str(c_src), "-o", str(c_o)])
    write_renames(tmp / "ren_c.txt", "c_")
    run(["objcopy", "--redefine-syms=" + str(tmp / "ren_c.txt"),
         str(c_o), str(c_r)])

    run(["gcc", *COMMON_FLAGS, *X86_FLAGS, "-O0", *INCLUDES,
         "-nostdlib", "-static", "-no-pie", "-Wl,-z,noexecstack",
         str(HOST_SRC), str(asm_r), str(c_r), "-o", str(exe)])
    return exe, asm_o, c_o


def target_compile(tmp):
    """カーネル本番と同じ i386-elf-gcc と、移植先候補の ARM で通ることを見る。"""
    bad = 0

    cc = shutil.which("i386-elf-gcc")
    if cc is None:
        print("TARGET i386-elf SKIP (i386-elf-gcc が無い)", flush=True)
        bad += 1
    else:
        obj = tmp / "kstring_c-i386.o"
        run([cc, *COMMON_FLAGS, *X86_FLAGS, "-nostdlib", "-O2",
             "-D__KERNEL_BUILD__", *INCLUDES, "-c", str(C_SRC),
             "-o", str(obj)])
        print("TARGET i386-elf GNU89 -Werror COMPILE PASS", flush=True)
        nm = shutil.which("i386-elf-nm") or "nm"
        bad += check_symbol_set(obj, "i386-elf", nm)

    cc = shutil.which("arm-none-eabi-gcc")
    if cc is None:
        print("TARGET arm-none-eabi SKIP (arm-none-eabi-gcc が無い)", flush=True)
    else:
        obj = tmp / "kstring_c-arm.o"
        run([cc, *COMMON_FLAGS, "-nostdlib", "-O2",
             "-D__KERNEL_BUILD__", *INCLUDES, "-c", str(C_SRC),
             "-o", str(obj)])
        print("TARGET arm-none-eabi GNU89 -Werror COMPILE PASS", flush=True)
        nm = shutil.which("arm-none-eabi-nm") or "nm"
        bad += check_symbol_set(obj, "arm-none-eabi", nm)

    return bad


# ---------------------------------------------------------------------------
#  変異 (否定側)。C 版をわざと壊し、試験が **落ちる** ことを見る。
# ---------------------------------------------------------------------------
MUTATIONS = [
    # 中心の変異: kstrcmp の差を符号付きで取る版。0x80 以上のバイト
    # (日本語ファイル名) の並び順がアセンブリ版 (movzx = 符号無し) と逆転する。
    ("kstrcmp_signed",
     "        pa++;\n"
     "        pb++;\n"
     "    }\n"
     "    return (int)*pa - (int)*pb;",
     "        pa++;\n"
     "        pb++;\n"
     "    }\n"
     "    return (int)(signed char)*pa - (int)(signed char)*pb;"),
    # memcmp も同じ罠。こちらを符号付きにする版。
    ("memcmp_signed",
     "    while (n-- != 0) {\n"
     "        if (*pa != *pb) return (int)*pa - (int)*pb;",
     "    while (n-- != 0) {\n"
     "        if (*pa != *pb) "
     "return (int)(signed char)*pa - (int)(signed char)*pb;"),
    # kstrncpy が libc の strncpy のように残りを 0 で埋める版。
    ("kstrncpy_pads",
     "    for (i = 0; (i + 1) < n && src[i] != '\\0'; i++) {\n"
     "        dst[i] = src[i];\n"
     "    }\n"
     "    dst[i] = '\\0';",
     "    for (i = 0; (i + 1) < n && src[i] != '\\0'; i++) {\n"
     "        dst[i] = src[i];\n"
     "    }\n"
     "    while ((i + 1) < n) dst[i++] = '\\0';\n"
     "    dst[i] = '\\0';"),
    # kmemcpy のバルク部を 1 バイトずつにする版。rep movsd は 4 バイト読んで
    # から 4 バイト書くので、**重なりコピーの壊れ方**が変わる。
    ("kmemcpy_bytewise",
     "        b0 = s[0]; b1 = s[1]; b2 = s[2]; b3 = s[3];\n"
     "        d[0] = b0; d[1] = b1; d[2] = b2; d[3] = b3;",
     "        b0 = s[0]; d[0] = b0; b1 = s[1]; d[1] = b1;\n"
     "        b2 = s[2]; d[2] = b2; b3 = s[3]; d[3] = b3;"),
    # 別名を「別の実体」にする版 (エイリアスでなく素の転送関数)。
    ("alias_separate_body",
     "void *memcpy(void *dst, const void *src, u32 n) "
     "__attribute__((alias(\"kmemcpy\")));",
     "void *memcpy(void *dst, const void *src, u32 n) "
     "{ return kmemcpy(dst, src, n); }"),
]


def one_mutation(item):
    """変異 1 本: C 版の写し (一時ディレクトリの木) を壊して組んで回す。
    実物の lib/kstring_c.c は読むだけ。(印字, 見逃し) を返す。"""
    name, old, new = item
    original = C_SRC.read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-22s SKIP (目印が見つからない)" % name, 1
    rel = str(C_SRC.relative_to(ROOT))
    with tempfile.TemporaryDirectory(prefix="os32-kstring-c-mut-") as td:
        td = pathlib.Path(td)
        dep_cmd = ["gcc", *COMMON_FLAGS, *X86_FLAGS, "-O2", *INCLUDES,
                   "-c", str(C_SRC), "-o", str(td / "dep.o")]
        tree = mutpar.mutant_tree(ROOT, td / "tree",
                                  {rel: original.replace(old, new, 1)},
                                  gcc_cmds=[dep_cmd])
        try:
            exe, _, _ = build_exe(td, tree / rel, "mut-" + name)
        except subprocess.CalledProcessError:
            return "MUTATE %-22s RED (コンパイルが通らない)" % name, 0
        rc = subprocess.run([str(exe)], cwd=str(tree), timeout=300,
                            capture_output=True).returncode
    if rc == 0:
        return ("MUTATE %-22s **GREEN のまま = 試験が契約を見ていない**"
                % name, 1)
    return "MUTATE %-22s RED (期待どおり落ちた)" % name, 0


def run_mutations(tmp):
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, MUTATIONS,
                                   ("control", "", ""))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-kstring-c-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        exe, asm_o, c_o = build_exe(tmp, C_SRC, "kstring-c")
        failed += check_symbol_set(asm_o, "asm (nasm)")
        failed += check_symbol_set(c_o, "c (host m32)")

        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=300).returncode
        print("EXIT kstring_c_host=%d" % rc, flush=True)
        failed += rc != 0

        failed += target_compile(tmp)

        if "--mutate" in sys.argv:
            failed += run_mutations(tmp)

        sys.exit(1 if failed else 0)
