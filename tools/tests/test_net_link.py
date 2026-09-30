"""N1 段 4: net/link.c (ワイヤ v2) と kapi/kapi_host.c (KAPI v51) のホスト TDD。

票:   docs/archive/network/TASK_N1.md 段 4 / 契約は TASK_N0.md §1a・§1b・§2
記録: tools/tests/n1_tdd.md

tools/tests/kapi_db_v50_host.c と同じ様式 — 実物のソースを 1 行も写さずに
#include し、ホストに持ち込めないもの (NIC・cli/sti・100Hz タイマ・
ディスパッチャ) だけを贋物にする。対向は **実 Agent** (tools/host_agent.py を
UNIX ソケットでサブプロセス起動) か、フレームを手で組む台本。
Make・エミュレータ・ネットワークには触らない (Agent は --offline で起動する)。

  python3 -B tools/tests/test_net_link.py [--target] [--sanitize] [ケース名 ...]

--target は同じソースがカーネルと同じフラグの i386-elf-gcc -Werror でも通ることを
別に見る ([C1] C89/GNU89)。
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST_SRC = ROOT / "tools/tests/net_link_host.c"
GENERATED = ROOT / "kapi/kapi_generated.c"

# ヘッダは実物の置き場を通す。ホストに持ち込めないものは net_link_host.c が
# **include ガードを先に define して**中身を自前のものに差し替える。
INC = ["-I" + str(ROOT / p) for p in ("include", "net", "kapi", "lib", "kernel",
                                      "drivers", "exec", "fs", "sdk/include/os32")]
FLAGS = ["-std=gnu89", "-O0", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-Wno-pointer-to-int-cast",
         "-Wno-unused-parameter"]

TARGET_FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
                "-fno-stack-protector", "-nostdlib", "-mno-red-zone", "-fcommon",
                "-O2", "-Wall", "-Wextra", "-Werror",
                "-Wdeclaration-after-statement", "-D__KERNEL_BUILD__"]
# arch/x86 + platform/pc98: include/io.h は契約だけで、実装は固定名
# arch_io.h / platform_io.h を引く (順序 3)。build/config.mk の INC_KERNEL と
# 同じものをここでも渡す (ホスト側は LINK_HOST_TEST で io.h を外すので不要
# だが、--target はカーネルと同じ素性で組むので要る)。
TARGET_INC = ["-I" + str(ROOT / p) for p in
              (".", "include", "arch/x86", "platform/pc98",
               "sdk/include", "sdk/include/os32", "kernel",
               "drivers", "net", "fs", "exec", "gfx", "lib", "kapi", "lib/sqlite3")]


def argptr_defines():
    """kapi_argptr[] の host_* の行を **生成物から読んで** -D で渡す。

    ディスパッチャの早期検査が見るのはこの表なので、試験が自前の値を持つと
    「表が変わったのに試験は通る」が起きる。生成物を読むことで縛る。
    """
    text = GENERATED.read_text(encoding="utf-8")
    out = []
    for name in ("host_open", "host_status", "host_read", "host_write", "host_close"):
        m = re.search(r"^\s*(0x[0-9A-Fa-f]{4}),\s*/\* %s[:*/ ]" % name, text, re.M)
        assert m, "kapi_generated.c に %s の argptr が無い" % name
        out.append("-DARGPTR_%s=%s" % (name.upper(), m.group(1)))
    return out


def check_reclaim_has_host_owner_exit():
    """exec_reclaim_owned が host_owner_exit を呼んでいるか (票 N1 段 3)。

    並びは exec/exec.c の 1 か所にしかなく、ホストではリンクできない。
    con_sink_owner_exit / launch_owner_exit と同じ位置にあることを本文で見る。
    """
    src = (ROOT / "exec/exec.c").read_text(encoding="utf-8")
    m = re.search(r"static void exec_reclaim_owned\(int id\)\s*\{(.*?)\n\}", src, re.S)
    assert m, "exec_reclaim_owned が見つからない"
    body = m.group(1)
    assert "host_owner_exit(id);" in body, "exec_reclaim_owned に host_owner_exit が無い"
    assert body.index("launch_owner_exit(id);") < body.index("host_owner_exit(id);") \
        < body.index("con_sink_owner_exit(id);"), \
        "host_owner_exit は launch_owner_exit と con_sink_owner_exit の間に置く"
    print("ORDER SOURCE: host_owner_exit between launch/con_sink PASS", flush=True)


def check_timer_calls_link_tick():
    """link_tick が 100Hz タイマ (ne2k_timer_tick の直後) からだけ呼ばれるか。"""
    isr = (ROOT / "kernel/isr_handlers.c").read_text(encoding="utf-8")
    m = re.search(r"void timer_handler\(void\)\s*\{(.*?)\n\}", isr, re.S)
    assert m, "timer_handler が見つからない"
    body = m.group(1)
    assert "ne2k_timer_tick();" in body and "link_tick();" in body
    assert body.index("ne2k_timer_tick();") < body.index("link_tick();")
    kapi = (ROOT / "kapi/kapi_host.c").read_text(encoding="utf-8")
    assert "link_tick" not in kapi, "KAPI ラッパーが link_tick を呼んでいる"
    link = re.sub(r"/\*.*?\*/", "", (ROOT / "net/link.c").read_text(encoding="utf-8"),
                  flags=re.S)
    assert len(re.findall(r"\blink_tick\s*\(", link)) == 1, \
        "net/link.c の中から link_tick を呼んでいる (定義 1 か所だけのはず)"
    lgy = (ROOT / "drivers/lgy98.c").read_text(encoding="utf-8")
    assert "if (!(flags & LGY98_FLAG_REFLECT)) link_init(mac);" in lgy, \
        "反射モードで link_init を止めていない"
    print("DRIVE SOURCE: link_tick only from timer_handler PASS", flush=True)


if __name__ == "__main__":
    check_reclaim_has_host_owner_exit()
    check_timer_calls_link_tick()
    with tempfile.TemporaryDirectory(prefix="os32-n1-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = str(tmp / "net_link")
        san = (["-fsanitize=address", "-fno-omit-frame-pointer"]
               if "--sanitize" in sys.argv else [])
        subprocess.run(["gcc", *FLAGS, *san, *argptr_defines(), *INC,
                        str(HOST_SRC), "-o", exe], check=True, cwd=ROOT)
        print("HOST GNU89 -Werror compile PASS (real net/link.c + kapi/kapi_host.c)",
              flush=True)
        if "--target" in sys.argv:
            for src in (ROOT / "net/link.c", ROOT / "kapi/kapi_host.c"):
                subprocess.run(["i386-elf-gcc", *TARGET_FLAGS, *TARGET_INC,
                                "-c", str(src), "-o", str(tmp / (src.stem + ".o"))],
                               check=True, cwd=ROOT)
            print("TARGET i386-elf GNU89 -Werror compile PASS", flush=True)
        cases = [x for x in sys.argv[1:] if not x.startswith("--")]
        rc = subprocess.run([exe, *cases], cwd=ROOT, timeout=600).returncode
        print("EXIT net_link_host=%d" % rc, flush=True)
        sys.exit(rc)
