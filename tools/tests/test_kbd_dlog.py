"""キーボードの受信記録 (KAPI v67 kbd_diag_log) と `kbdstat -w` の行のホスト試験。

票:   docs/tasks/gui/TASK_KBD_NAV.md §3 (カナ / CAPS の make / break を実機で見る準備)、
      §2 (方式 B: カナ / CAPS は make で ON、break で OFF。起動時は 053Ah から)
記録: tools/tests/kbd_dlog_tdd.md

実物の drivers/input/kbd_dlog.c・drivers/input/kbd.c (IRQ1 ハンドラと kbd_diag_log)・
userland/shell/kbd_watch.c を 1 行も写さずに #include して回す。0043h / 0041h は
tools/tests/kbd_hostshim/io.h で模型へ回す。

**EMPTY / ERROR を積まないことは NP21/W では踏めない** (keyboard_i43 は IRQ1 の
前に必ず RxRDY を立てる) ので、ここで固定する。

  python3 -B tools/tests/test_kbd_dlog.py            # ホストで全ケース
  python3 -B tools/tests/test_kbd_dlog.py --target   # + i386-elf で 3 本を通す
  python3 -B tools/tests/test_kbd_dlog.py --mutate   # 否定側 (変異が RED になるか)
"""
CHECK = dict(name='kbd-dlog-host', tier=1,
    covers=['drivers/input/kbd_hal.c', 'input/hal/in2_dev.c', 'input/hal/in2_state.c', 'input/hal/kbd_merge.c', 'tools/tests/kbd_capture_fixture_host.h', 'kernel/idle_work.h', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'include/kbd.h', 'drivers/input/kbd.h', 'platform/pc98/legacy/v86_kbd.h', 'drivers/input/kbd.c', 'input/hal/legacy_ring.c', 'input/hal/legacy_mode.c', 'kernel/input_legacy.c', 'kernel/input_wait.c', 'input/hal/in2_int.h', 'drivers/input/kbd_fgpoll.inc', 'drivers/video/geforce/geforce_module.h', 'drivers/input/kbd_dlog.c', 'drivers/input/kbd_status.c', 'tools/tests/kbd_dlog_host.c', 'userland/shell/kbd_watch.c', 'tools/tests/mutpar.py', 'drivers/input/mouse.c', 'tools/tests/serial_emergency_host.h', 'kernel/serial_diag.c', 'include/console_output.h', 'lib/serial_diag.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.4,
    kind='characterization',
)

from kernel_includes import include_flags
import pathlib
import re
import mutpar
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/kbd_dlog_host.c"
SHIM = ROOT / "tools/tests/kbd_hostshim"
# 変異の対象。キーは MUTATIONS の 1 列目。
SOURCES = {
    "d": "drivers/input/kbd_dlog.c",
    "k": "drivers/input/kbd.c",
    "g": "input/hal/kbd_merge.c",
    "w": "userland/shell/kbd_watch.c",
    "m": "drivers/input/mouse.c",
}

CASES = ["stop_timeout", "stop_mouse", "stop_burst", "ring_basic", "ring_wrap", "irq_skip", "irq_mods", "irq_flags_api",
         "irq_locks", "irq_v86_mods", "watch_fmt", "watch_lost"]

# -Wno-attributes: __cdecl は x86-64 のホストでは無視される (警告だけ)。
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-attributes",
         "-Wno-unused-function"]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (対象ファイル, パターン, 置換, 説明)
# kbd_diag_log の「max を KBD_DLOG_CAP で頭打ち」は変異に入れない — 保持件数が
# CAP 以下なので kbd_dlog_copy は頭打ち無しでも CAP 件を超えて書かず、外しても
# 振る舞いが変わらない (等価変異)。ローカルの tmp[] を守る防御として残す。
MUTATIONS = [
    ("d", r"    if \(s < oldest\) \{\n        s = oldest;\n    \}\n",
     "    (void)oldest;\n",
     "最古で切らない (上書き済みの枠を古い seq のつもりで写す = 飛びが見えない)"),
    ("d", r"while \(s <= last && n < max\)",
     "while (s <= last)",
     "max を見ない (呼び手の配列を越えて書く)"),
    ("d", r"    l->last_seq = seq;\n",
     "",
     "seq を進めない (同じ枠を上書きし続ける)"),
    ("d", r"e = &l->ent\[\(seq - 1\) % KBD_DLOG_CAP\];",
     "e = &l->ent[seq % KBD_DLOG_CAP];",
     "積む位置と読む位置が 1 つずれる"),
    ("d", r"oldest = \(last > \(u32\)KBD_DLOG_CAP\) \? last - \(u32\)KBD_DLOG_CAP \+ 1 : 1;",
     "oldest = (last > (u32)KBD_DLOG_CAP) ? last - (u32)KBD_DLOG_CAP : 1;",
     "最古を 1 つ古く見積もる (上書き済みの 1 件を混ぜる)"),
    ("k", r"(    if \(kind == KBD_ST_EMPTY\) \{\n        kbd_diag_empty\+\+;\n)",
     r"\1        kbd_dlog_push(&kbd_dlog, 0, kbd_shift_state, 0);\n",
     "空 IRQ も積む (読んでいないバイトが行になる)"),
    ("k", r"(        kbd_diag_err\+\+;\n)",
     r"\1        kbd_dlog_push(&kbd_dlog, 0, kbd_shift_state, 0);\n",
     "PE / FE で捨てたバイトも積む"),
    ("k", r"    kbd_deliver\(scancode\);\n\n(    /\*.*?\*/\n)    kbd_dlog_push\(&kbd_dlog, scancode, kbd_shift_state, lflags\);\n",
     r"    kbd_dlog_push(&kbd_dlog, scancode, kbd_shift_state, lflags);\n    kbd_deliver(scancode);\n",
     "配る前に積む (カナの行に更新前の修飾が載る)"),
    ("k", r"    if \(kind == KBD_ST_OVERRUN\) lflags \|= KBD_DLOG_F_OVERRUN;\n",
     "",
     "OVERRUN の印を付けない"),
    ("k", r"    kbd_dlog_reset\(&kbd_dlog\);\n",
     "",
     "kbd_init でリングを空にしない (前の記録が残る)"),
    ("k", r"    if \(out == NULL \|\| max <= 0\) \{\n        return OS32_ERR_INVAL;\n    \}\n",
     "",
     "引数を検査しない (NULL へ書く / 0 件を成功と答える)"),
    ("g", r"        e->buttons = kbd_shift_state = input_key_mods\(\);\n        if \(legacy_active\(\)\) return;",
     "        if (legacy_active()) return;\n        e->buttons = kbd_shift_state = input_key_mods();",
     "V86 中は修飾 snapshot を更新しない"),
    ("g", r"        if \(legacy_active\(\)\) return;", "        if (0) return;",
     "V86 のキーを OS32 にも配る"),
    ("g", r"if \(down\) mechanical\[e->src - 1\] \|= bit;", "if (down) mechanical[e->src - 1] ^= bit;",
     "機械式ロックを make で反転する"),
    ("g", r"else mechanical\[e->src - 1\] &= ~bit;", "else (void)bit;",
     "ロックの break を無視する"),
    ("g", r"if \(origin == INPUT_ORIGIN_NATIVE\)", "if (origin == INPUT_ORIGIN_NATIVE && !legacy_active())",
     "V86 中は機械式ロックを追わない"),
    ("k", r"kbd_hal_init\(kbd_lock_bits_from_bios\(kbd_bios_shift_peek\(\)\)\);", "kbd_hal_init(0);",
     "BIOS の初期ロックを捨てる"),
    ("k", r"kbd_hal_init\(kbd_lock_bits_from_bios\(kbd_bios_shift_peek\(\)\)\);", "kbd_hal_init(kbd_lock_bits_from_bios(kbd_bios_shift_peek())); kbd_shift_state = kbd_bios_shift_peek();",
     "BIOS の SHIFT/GRPH/CTRL も引き継ぐ"),
    ("k", r"if \(bios_shift & BIOS_KB_SHIFT_CAPS\) s \|= SHIFT_CAPS;",
     "if (bios_shift & BIOS_KB_SHIFT_CAPS) s |= SHIFT_KANA;",
     "053Ah の CAPS をカナと取り違える"),
    ("w", r'kbdw_str\(&o, \(code & KBD_DLOG_BREAK\) \? " break" : " make "\);',
     'kbdw_str(&o, (code & KBD_DLOG_BREAK) ? " make " : " break");',
     "make と break を取り違える"),
    ("w", r'    \{ KBD_DLOG_MOD_KANA,  "KANA"  \},',
     '    { KBD_DLOG_MOD_CAPS,  "KANA"  },',
     "KANA のビットを CAPS と取り違える"),
    ("w", r"    return first - prev - 1u;",
     "    return first - prev;",
     "取りこぼしの件数を 1 多く数える"),
    ("w", r"    if \(first <= prev \+ 1u\) return 0;",
     "    if (first <= prev + 2u) return 0;",
     "1 件だけの飛びを取りこぼしと言わない"),
    ("w", r"    if \(o->cap <= 0 \|\| o->len >= o->cap - 1\) return;",
     "    if (o->cap <= 0 || o->len >= o->cap) return;",
     "切り詰めで NUL の分を空けない (cap を 1 バイト越える)"),
]


def host_build(tmp, mutated=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。

    SOURCES を tmp へ写し (mutated = {キー: 本文} は差し替え)、-I tmp を
    先頭に置いてハーネスの #include をそちらへ向ける。io.h は shim が先。"""
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "kbd-dlog-host"
    # Keep driver-local and explicit ../drivers/input/kbd.h includes in the fixture.
    for rel in ("platform/pc98/legacy/v86_kbd.h", "drivers/input/kbd.h", "include/kbd.h"):
        dst = src_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text((ROOT / rel).read_text(encoding="utf-8"), encoding="utf-8")
    for key, rel in SOURCES.items():
        dst = src_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        text = (mutated or {}).get(key)
        if text is None:
            text = (ROOT / rel).read_text(encoding="utf-8")
        dst.write_text(text, encoding="utf-8")
    cmd = ["gcc", *FLAGS, "-I" + str(SHIM), "-I" + str(src_dir),
           "-I" + str(ROOT / "userland/shell"), "-I" + str(ROOT / "input/hal"), *include_flags(ROOT),
           str(HARNESS), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return exe


def run_cases(exe, cases):
    failed = 0
    for case in cases:
        rc = mutpar.run_timeout([str(exe), case], cwd=ROOT).returncode
        print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def build_target(tmp):
    """カーネルと同じ i386-elf で通す (ホストで外した STATIC_ASSERT もここで効く)。
    kbd_watch.c は常駐シェルの側だが、同じ GNU11 -Werror で通るかだけを見る。"""
    for rel in ["drivers/input/kbd_dlog.c", "drivers/input/kbd.c", "userland/shell/kbd_watch.c"]:
        cmd = ["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror",
               "-D__KERNEL_BUILD__",
                *include_flags(ROOT),
               "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU11 -Werror PASS", flush=True)


def mutate(tmp):
    """実装を 1 か所ずつ壊して、どれも RED になることを見る。"""
    orig = {k: (ROOT / rel).read_text(encoding="utf-8")
            for k, rel in SOURCES.items()}
    bad = 0
    for i, (which, pattern, repl, why) in enumerate(MUTATIONS, 1):
        mutated, n = re.subn(pattern, repl, orig[which], count=1, flags=re.S)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, {which: mutated})
        except subprocess.CalledProcessError:
            print(f"MUTATION {i} RED (compile): {why}", flush=True)
            continue
        hits = 0
        for c in CASES:
            try:
                rc = mutpar.run_timeout([str(exe), c], cwd=ROOT, timeout=20,
                                    stderr=subprocess.DEVNULL).returncode
            except subprocess.TimeoutExpired:
                rc = -1
            hits += rc != 0
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-kbd-dlog-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU11 -Werror compile PASS (real drivers/input/kbd_dlog.c + drivers/input/kbd.c"
              " + userland/shell/kbd_watch.c)", flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_kbd_dlog.py', '--target', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
