"""H1: hdrv_list_dir() の列挙ループが「途中で切れた一覧」を成功で返さない。

票:   docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §8 の H1 行
      (完了条件「I/O 失敗を成功にしない」/ 対象「HostDrv のエラー処理」)
記録: tools/tests/h1_tdd.md

tools/tests/hostdrv_list_host.c が fs/hostdrv_list_rules.inc を 1 行も写さず
そのまま #include し、`hostdrv_query_dir` に当たる 1 件取得だけを台本式の
贋物に差し替えて回す (模型ではない)。注入するのは

  (a) 途中で負値を返す   — 200 件のうち 50 件目で失敗
  (b) 上限を超える件数   — 5000 件を上限 1000 で読む

の 2 つ。どちらも **VFS_OK を返さない** ことを見る。

  python3 -B tools/tests/test_hostdrv_list.py [--target]

--target を付けると、実機と同じ i386-elf クロスコンパイラでも
fs/hostdrvfs.c が -Werror で通ることを確かめる ([C1] C89/GNU89)。
make・エミュレータ・実配備には一切触れない。
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

HOST_FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
              "-Wdeclaration-after-statement", "-D__cdecl="]
HOST_INC = ["-I" + str(ROOT / p) for p in
            (".", "include", "sdk/include", "sdk/include/os32")]

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt

TARGET_KERNEL = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
                 "-fno-pie", "-fno-stack-protector", "-nostdlib",
                 "-mno-red-zone", "-fcommon", "-O2",
                 "-Wall", "-Wextra", "-Werror",
                 "-Wdeclaration-after-statement",
                 # hostdrvfs.c は元から出る警告なのでここだけ外す
                 "-Wno-address-of-packed-member",
                 # arch/x86 + platform/pc98: include/io.h は契約だけで、実装は
                 # 固定名 arch_io.h / platform_io.h を引く (順序 3)。
                 "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
                 "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
                 "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet",
                 "-Ifs", "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]


def check_cap_constant():
    """上限値が 1 か所 (fs/hostdrvfs.c) にしか無いことを確かめる ([C4])。

    試験は上限を引数で受け取るので、本体の定数とずれても気づけない。
    ここで定義の存在と値を報告し、写しが増えていないかを見る。
    """
    text = (ROOT / "fs/hostdrvfs.c").read_text()
    m = re.search(r"^#define\s+HOSTDRV_MAX_DIR_ENTRIES\s+(\d+)", text, re.M)
    if not m:
        raise SystemExit("fs/hostdrvfs.c に HOSTDRV_MAX_DIR_ENTRIES が無い")
    hits = len(re.findall(r"HOSTDRV_MAX_DIR_ENTRIES", text))
    if hits != 2:          # 定義 1 + 使用 1
        raise SystemExit(
            "HOSTDRV_MAX_DIR_ENTRIES の出現が %d 箇所 (定義 1 + 使用 1 のはず)"
            % hits)
    print("CAP CONSTANT PASS (HOSTDRV_MAX_DIR_ENTRIES=%s、定義 1 + 使用 1)"
          % m.group(1), flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-hdrv-list-") as tmp:
        tmp = pathlib.Path(tmp)

        check_cap_constant()

        exe = tmp / "hostdrv-list"
        subprocess.run(["gcc", *HOST_FLAGS, *HOST_INC,
                        str(ROOT / "tools/tests/hostdrv_list_host.c"),
                        "-o", str(exe)], cwd=ROOT, check=True)
        print("HOST GNU89 -Werror COMPILE PASS (real hostdrv_list_rules.inc)",
              flush=True)
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT hostdrv_list_host=%d" % rc, flush=True)

        if "--target" in sys.argv:
            subprocess.run(["i386-elf-gcc", *TARGET_KERNEL, "-c",
                            "fs/hostdrvfs.c", "-o", str(tmp / "hostdrvfs.o")],
                           cwd=ROOT, check=True)
            print("TARGET i386-elf -Werror COMPILE PASS (fs/hostdrvfs.c)",
                  flush=True)

        sys.exit(rc)
