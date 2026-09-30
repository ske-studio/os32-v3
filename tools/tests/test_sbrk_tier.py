"""CPL=3 プログラムの sbrk 物理「二段構え」の性質試験 (決裁 2026-09-11).

票:   docs/archive/gui_v13/TASK_K5B_kernel.md (作業 8)
記録: tools/tests/k5b_kernel_tdd.md (回 4)

exec/exec.c の判定関数 (exec_ring3_extra_pages / exec_ring3_pages /
exec_sbrk_pick_tier) を
**テキストのまま切り出して** ホストへ差し込み、exec/appslot.c と一緒に
ILP32 freestanding でコンパイルして走らせる。tools/tests/test_pgalloc_model.py
が exec_child_claim を切り出すのと同じ流儀で、並行して書いた別式ではなく
出荷するコードそのものを見る。

最後に同じ exec/exec.c がクロスコンパイラのカーネルフラグで通ることも確かめる
([C1] C89/GNU89)。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement"]
INC = [str(ROOT / p) for p in ("include", "kernel", "lib", "exec", "fs",
                               "sdk/include/os32")]
SRC = ROOT / "tools/tests/sbrk_tier_host.c"

# 切り出す対象。名前と「開き括弧まで」で挟み、本体は最初の行頭 '}' まで。
WANTED = ("static u32 exec_ring3_extra_pages(",
          "static u32 exec_ring3_pages(",
          "static int exec_sbrk_pick_tier(")


def slice_out(source, signature):
    """exec.c から static 関数 1 本をテキストのまま取り出す。"""
    if source.count(signature) != 1:
        raise SystemExit("exec/exec.c: %r が 1 個ではない (実装が動いた?)" % signature)
    body = source.split(signature, 1)[1]
    if "\n}" not in body:
        raise SystemExit("exec/exec.c: %r の終端が見つからない" % signature)
    return signature + body.split("\n}", 1)[0] + "\n}\n"


def extract():
    source = (ROOT / "exec/exec.c").read_text()
    define = next((line for line in source.splitlines()
                   if line.startswith("#define RING3_USTACK_SIZE ")), None)
    if define is None:
        raise SystemExit("exec/exec.c: #define RING3_USTACK_SIZE が見つからない")
    parts = ["/* 生成物。exec/exec.c から test_sbrk_tier.py が切り出した。 */",
             define]
    parts += [slice_out(source, sig) for sig in WANTED]
    return "\n".join(parts) + "\n"


def main():
    inc = extract()
    includes = ["-I" + p for p in INC]
    with tempfile.TemporaryDirectory(prefix="os32-sbrk-tier-") as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "exec_sbrk_tier.inc").write_text(inc)
        exe = tmp / "sbrk-tier"
        subprocess.run(["gcc", *FLAGS, "-O0", "-nostdlib", "-static", "-no-pie",
                        "-I" + str(tmp), *includes, str(SRC), "-o", str(exe)],
                       cwd=ROOT, check=True)
        print("HOST ILP32 GNU89 COMPILE PASS", flush=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True, timeout=60)
        subprocess.run(["i386-elf-gcc", *FLAGS, "-O2", *includes, "-c",
                        str(ROOT / "exec/appslot.c"), "-o", str(tmp / "appslot.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU89 -Werror COMPILE PASS", flush=True)


if __name__ == "__main__":
    sys.exit(main())
