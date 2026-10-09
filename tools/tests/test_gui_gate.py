"""tools/gui_gate.py の「GUI に入れたか」の判定のホスト試験。

記録: tools/tests/gui_gate_tdd.md
経緯: docs/archive/settings/TASK_S5.md §6 (R2 の予備調査、2026-09-29)、POLICY_DEBUG §4-31

台本が rshell を閉じずに `os32gui` を打つと、4 文字ずつの text が `os32` / `gui` の
2 コマンドになって GUI に入らず、それでも `leave_gshell` の `ver` が通るので
RESULT: OK になっていた。ここで固定するのは 3 つ:

  1. rshell を閉じたことを tvram の `[Remote shell closed]` で確かめる判定
     (`rshell_closed_fresh`) — 前の回の印が残っているだけでは数えない。
  2. GUI に居ることの判定 (`gui_entered`) — `scrn_ymax == --h` かつ `grph_disp == 1`、
     または Cirrus の WAB 中継 (`wab_relay == 1` かつ `wab_height == --h`)。Cirrus の
     gshell 中も 98 の表示レジスタは `scrn_ymax 400 grph_disp 0` のまま (2026-09-29 夕)。
  3. 台本の入口 (`begin_gui`) と台本全体 — 偽のゲスト (rshell の段数・GUI の有無・
     tvram・/api/status を持つ) の上で、GUI に入れなければ NG、入れれば OK。
     NG の後は CUI + rshell へ戻す — 高さ違いで GUI に入っていれば GUI から抜けてから。

NP21/W もゲストも要らない。実物の tools/gui_gate.py (または変異させた写し) を
import して、post / get / _cmd_raw / time を偽物に差し替える。
**実際のゲストで ESC が rshell を閉じるか・/api/status の値がこの通りかはここでは
分からない** — それは NP21/W で確かめる。

  python3 -B tools/tests/test_gui_gate.py            # 全ケース
  python3 -B tools/tests/test_gui_gate.py --mutate   # 否定側
"""
import contextlib
import importlib.util
import io
import json
import pathlib
import re
import sys
import tempfile
import types

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
SRC = ROOT / "tools/gui_gate.py"
SHARED = ROOT / "tools/np21w_mcp/gui.py"
MARK = "[Remote shell closed]"
PROMPT = "/> "


def load(path=None):
    spec = importlib.util.spec_from_file_location(
        "gui_gate_under_test", str(path or SRC))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


FAILED = []


def check(cond, what):
    if not cond:
        FAILED.append(what)


class Runaway(Exception):
    """偽の時計が上限を超えた (変異で待ちが終わらなくなったとき)。"""


class FakeGuest:
    """ゲストの見え方の最小の模型。

    - rshell は `depth` 段重なる。ESC は内側の 1 段を閉じ、印を出す。最後の段が
      閉じるとプロンプトが出る (外側の rshell は何も出さない)。
    - rshell が有効な間の text は 1 回の POST が 1 コマンドになる (§4-31)。
    - CUI (rshell 無し) の text は行に溜まり、RETURN で実行される。`os32gui` で GUI へ。
    - GUI では Start → CUI mode → Yes で CUI へ戻り、rshell が 1 段で自動起動する。
    """

    def __init__(self, h=480, depth=1, gui_ok=True, esc_works=True, lines=None,
                 cirrus=False):
        self.h = h
        self.cirrus = cirrus   # GUI は WAB 中継 (98 の GDC は CUI のまま)
        self.depth = depth
        self.gui = False
        self.gui_ok = gui_ok
        self.esc_works = esc_works
        self.lines = list(lines if lines is not None else
                          ["OS32 boot", "Remote shell active (ESC to exit)",
                           "Waiting for commands via serial..."])
        self.cmd_line = ""
        self.keys = []
        self.gui_entries = 0
        self.ax = self.ay = 0
        self.cui_pending = False
        self.clicks = 0
        self.gui_text = []
        self.now = 0.0

    # --- 画面 ---
    def out(self, *ls):
        self.lines.extend(ls)
        self.lines = self.lines[-25:]

    def tvram(self):
        body = (self.lines + [""] * 25)[:25]
        return {"ok": True, "width": 80, "height": 25, "lines": body}

    def status(self):
        if self.cirrus:
            # 実測 (TASK_S5 §6、2026-09-29 夕): gshell 中も scrn_ymax 400 / grph_disp 0、
            # 画面は wab_relay 1 / wab_height 480。CUI での wab_height は未実測なので
            # 画面高のまま残す (wab_relay を見ない判定を落とすため)。
            return {"scrn_ymax": 400, "grph_disp": 0,
                    "wab_relay": 1 if self.gui else 0,
                    "wab_width": 640, "wab_height": self.h,
                    "fault_generation": 0}
        return {"scrn_ymax": self.h if self.gui else 400,
                "grph_disp": 1 if self.gui else 0,
                "wab_relay": 0, "fault_generation": 0}

    # --- 入力 ---
    def key(self, data):
        self.keys.append(dict(data))
        if "seq" in data:
            self.seq(data["seq"])
        if "text" in data:
            self.text(data["text"])

    def seq(self, s):
        if s == "ESC":
            if self.gui:
                return
            if self.depth > 0 and self.esc_works:
                self.depth -= 1
                self.out("", MARK)
                if self.depth == 0:
                    self.out(PROMPT)
            return
        if s == "RETURN" and not self.gui and self.depth == 0:
            line, self.cmd_line = self.cmd_line, ""
            self.out(PROMPT + line)
            if line == "os32gui":
                if self.gui_ok:
                    self.gui = True
                    self.gui_entries += 1
            elif line == "rshell":
                self.depth += 1
                self.out("Remote shell active (ESC to exit)",
                         "Waiting for commands via serial...")
            else:
                self.out("%s: command not found" % line, PROMPT)

    def text(self, t):
        if self.gui:
            self.gui_text.append(t)   # 実物では gshell (の端末・窓) に打ち込まれる
            return
        if self.depth > 0:
            self.out("> " + t)
            if t == "rshell":
                self.depth += 1
                self.out("Waiting for commands via serial...")
            else:
                self.out("%s: command not found" % t)
            return
        self.cmd_line += t

    def mouse(self, data):
        if "ax" in data:
            self.ax, self.ay = data["ax"], data["ay"]
        if data.get("btn") == 1:
            self.clicks += 1
        if data.get("btn") == 1 and self.gui:
            px = self.ax * 639 // 65535
            py = self.ay * (self.h - 1) // 65535
            if abs(py - self.cui_row[1]) <= 3 and abs(px - self.cui_row[0]) <= 3:
                self.cui_pending = True
            elif self.cui_pending and abs(px - 410) <= 3 and abs(py - (self.h // 2 + 11)) <= 3:
                self.gui = False
                self.cui_pending = False
                self.depth = 1
                self.out(PROMPT, "Remote shell active (ESC to exit)",
                         "Waiting for commands via serial...")

    def cmd(self, line):
        if self.depth > 0 and not self.gui:
            if line == "ver":
                return "OS32 v2 Build: test\n"
            if line.startswith("cat /etc/system.cfg"):
                return "GUI=0\n"
            return ""
        return ""   # 実物は EOT を待ち切る。ここでは空で返す


def wire(mod, guest, tmp):
    """mod の外への口を guest に差し替える。"""
    guest.cui_row = mod.start_row(guest.h, mod.ROW_CUI)

    def post(path, data, timeout=20):
        if path == "/api/key":
            guest.key(data)
        elif path == "/api/mouse":
            guest.mouse(data)
        return b'{"ok":true}'

    def get(path, timeout=20):
        if path == "/api/tvram":
            return json.dumps(guest.tvram()).encode(), {}
        if path == "/api/status":
            return json.dumps(guest.status()).encode(), {}
        if path.startswith("/api/screenshot"):
            src = "wab" if guest.cirrus and guest.gui else "fake"
            return b"", {"X-Screen-Source": src}
        raise AssertionError("unexpected GET " + path)

    def sleep(s):
        guest.now += s
        if guest.now > 3600:
            raise Runaway()

    mod.post = post
    mod.get = get
    mod._cmd_raw = lambda line, timeout: guest.cmd(line)
    mod.time = types.SimpleNamespace(sleep=sleep, time=lambda: guest.now)
    return mod.Shots(str(tmp))


# ---------------------------------------------------------------------------
#  ケース
# ---------------------------------------------------------------------------
ACTIVE = ["OS32 boot", "Remote shell active (ESC to exit)",
          "Waiting for commands via serial...", "> ver", "OS32 v2 Build: x"]


def case_fresh(mod, tmp):
    f = mod.rshell_closed_fresh
    # 閉じた: 末尾に印 + プロンプト。
    check(f(ACTIVE, ACTIVE + ["", MARK, PROMPT]), "fresh: 閉じた直後を見落とす")
    # 何も変わらない。
    check(not f(ACTIVE, list(ACTIVE)), "fresh: 変化なしを閉じたと読む")
    # 変わったが印が無い (ESC が別の物に食われた)。
    check(not f(ACTIVE, ACTIVE + ["> x"]), "fresh: 印の無い変化を閉じたと読む")
    # 前の回の印が末尾に残っているだけ (画面の上の方だけ変わった)。
    stale = ["clock 12:00", "", MARK, PROMPT]
    check(not f(stale, ["clock 12:01", "", MARK, PROMPT]),
          "fresh: 残っている古い印を新しい印と読む")
    # 前の回の印が途中にあり、その後に rshell の出力が続く (rshell は生きている)。
    mid = ["", MARK, PROMPT + "rshell", "Remote shell active (ESC to exit)",
           "Waiting for commands via serial..."]
    check(not f(mid, mid + ["> ver", "OS32 v2 Build: x"]),
          "fresh: 末尾でない古い印を閉じた証拠と読む")
    # 同じ末尾 (印 + プロンプト) が 2 回続く: 印の数が増えたので閉じた。
    once = ["x", "", MARK, PROMPT]
    check(f(once, once + ["", MARK, PROMPT]),
          "fresh: 末尾が前と同じ形のときの新しい印を見落とす")
    # 画面が流れて古い印が消え、数は同じだが末尾は変わった。
    full = ["", MARK] + ["line %d" % i for i in range(23)]
    after = full[3:] + ["", MARK, PROMPT]
    check(f(full, after), "fresh: 画面が流れたときの新しい印を見落とす")
    # 重なった rshell: 内側が閉じて外側は黙っている → 外側を閉じる。
    nested = ACTIVE + ["", MARK]
    check(f(nested, nested + ["", MARK, PROMPT]), "fresh: 2 段目の閉じを見落とす")
    # CUI のプロンプトで ESC (行を捨ててプロンプトを出し直す) は閉じではない。
    closed = ACTIVE + ["", MARK, PROMPT]
    check(not f(closed, closed + [PROMPT]), "fresh: プロンプトの出し直しを閉じたと読む")


def case_gui_entered(mod, tmp):
    g = mod.gui_entered
    check(g({"scrn_ymax": 480, "grph_disp": 1}, 480), "entered: PEGC 480 の GUI を落とす")
    check(g({"scrn_ymax": 400, "grph_disp": 1}, 400), "entered: 9801 400 の GUI を落とす")
    check(not g({"scrn_ymax": 400, "grph_disp": 0}, 480), "entered: CUI (400/0) を GUI と読む")
    check(not g({"scrn_ymax": 400, "grph_disp": 1}, 480), "entered: 高さ違いを GUI と読む")
    check(not g({"scrn_ymax": 480, "grph_disp": 0}, 480), "entered: グラフィック非表示を GUI と読む")
    check(not g({"scrn_ymax": 400, "grph_disp": 0}, 400), "entered: 9801 の CUI を GUI と読む")
    check(not g({"scrn_ymax": None, "grph_disp": None}, 480), "entered: 値が無いのを GUI と読む")
    # Cirrus: 98 の GDC は CUI のまま、画面は WAB 中継。
    cir = {"scrn_ymax": 400, "grph_disp": 0, "wab_relay": 1, "wab_height": 480}
    check(g(cir, 480), "entered: Cirrus (wab_relay 1 / wab_height 480) の GUI を落とす")
    check(not g(dict(cir, wab_height=400), 480), "entered: Cirrus の高さ違いを GUI と読む")
    check(not g(dict(cir, wab_height=480), 400), "entered: Cirrus 480 を --h 400 の GUI と読む")
    check(not g(dict(cir, wab_relay=0), 480), "entered: WAB 中継なし (CUI) を GUI と読む")
    check(not g(dict(cir, wab_height=None), 480), "entered: wab_height が無いのを GUI と読む")


def case_close(mod, tmp):
    guest = FakeGuest(depth=1)
    wire(mod, guest, tmp)
    check(mod.close_rshell() is True, "close: 1 段を閉じられない")
    check(guest.depth == 0, "close: rshell が残る")
    # 2 段: 両方閉じる (§4-31)。
    guest = FakeGuest(depth=2)
    wire(mod, guest, tmp)
    check(mod.close_rshell() is True, "close: 2 段で失敗する")
    check(guest.depth == 0, "close: 2 段目の rshell が残る (以後の text が食われる)")
    # 印が出ない (rshell が居ない / ESC が届かない) → False。
    guest = FakeGuest(depth=0, lines=["x", "", MARK, PROMPT])
    wire(mod, guest, tmp)
    check(mod.close_rshell() is False, "close: 印が出ないのに閉じたと言う")
    guest = FakeGuest(depth=1, esc_works=False)
    wire(mod, guest, tmp)
    check(mod.close_rshell() is False, "close: ESC が効かないのに閉じたと言う")


def case_begin(mod, tmp):
    guest = FakeGuest(depth=1)
    wire(mod, guest, tmp)
    check(mod.begin_gui(480) is True, "begin: 入れるのに NG")
    check(guest.gui, "begin: GUI に入っていない")
    # 閉じられなければ os32gui を打たない。
    guest = FakeGuest(depth=1, esc_works=False)
    wire(mod, guest, tmp)
    check(mod.begin_gui(480) is False, "begin: rshell を閉じられないのに OK")
    check(not any(k.get("text") for k in guest.keys), "begin: 閉じられないのに text を打つ")
    # os32gui が GUI に入らない → NG、rshell を戻す。
    guest = FakeGuest(depth=1, gui_ok=False)
    wire(mod, guest, tmp)
    check(mod.begin_gui(480) is False, "begin: GUI に入らないのに OK")
    check(guest.depth == 1, "begin: 失敗の後に rshell を戻さない")
    check(guest.clicks == 0, "begin: GUI に入っていないのにマウスを押す (従来どおりでない)")
    # 高さ違い (400 ラインで GUI に入ったのに --h 480) → NG。GUI には入っているので、
    # 実際の高さで Start → CUI mode → Yes を通して CUI へ戻し、rshell を戻す
    # (2026-09-29 Cirrus 試験: CUI の前提で打った `rshell` が gshell に入り、GUI に残った)。
    guest = FakeGuest(h=400, depth=1)
    wire(mod, guest, tmp)
    check(mod.begin_gui(480) is False, "begin: 高さ違いを OK にする")
    check(not guest.gui, "begin: 高さ違いで GUI に入ったまま残す")
    check(guest.depth == 1, "begin: 高さ違いの後に rshell を戻さない")
    check(not guest.gui_text, "begin: GUI に text を打ち込む (%r)" % guest.gui_text)
    # 逆向き (480 ラインで入ったのに --h 400) も同じ。
    guest = FakeGuest(h=480, depth=1)
    wire(mod, guest, tmp)
    check(mod.begin_gui(400) is False, "begin: 高さ違い (480/--h 400) を OK にする")
    check(not guest.gui and guest.depth == 1,
          "begin: 高さ違い (480/--h 400) で CUI + rshell に戻らない")


def case_begin_cirrus(mod, tmp):
    # 入った: scrn_ymax 400 / grph_disp 0 のままでも wab_relay 1 / wab_height 480 で OK。
    guest = FakeGuest(h=480, depth=1, cirrus=True)
    wire(mod, guest, tmp)
    check(mod.begin_gui(480) is True, "cirrus begin: 入れるのに NG")
    check(guest.gui, "cirrus begin: GUI に入っていない")
    check(not guest.gui_text, "cirrus begin: GUI に text を打ち込む (%r)" % guest.gui_text)
    # 入らない: NG、rshell を戻す、マウスは押さない。
    guest = FakeGuest(h=480, depth=1, gui_ok=False, cirrus=True)
    wire(mod, guest, tmp)
    check(mod.begin_gui(480) is False, "cirrus begin: GUI に入らないのに OK")
    check(guest.depth == 1, "cirrus begin: 失敗の後に rshell を戻さない")
    check(guest.clicks == 0, "cirrus begin: GUI に入っていないのにマウスを押す")
    # 高さ違い (WAB 480 で入ったのに --h 400): 98 の GDC は 400/0 で CUI と同じに見える。
    # CUI の前提で `rshell` を打つと gshell に入る (2026-09-29 夕に試験担当が踏んだ)。
    # wab_height (480) の座標で leave_gshell を通して戻す — scrn_ymax (400) では外れる。
    guest = FakeGuest(h=480, depth=1, cirrus=True)
    wire(mod, guest, tmp)
    check(mod.begin_gui(400) is False, "cirrus begin: 高さ違い (480/--h 400) を OK にする")
    check(not guest.gui, "cirrus begin: 高さ違い (480/--h 400) で GUI に残す")
    check(guest.depth == 1, "cirrus begin: 高さ違い (480/--h 400) の後に rshell を戻さない")
    check(not guest.gui_text, "cirrus begin: GUI に text を打ち込む (%r)" % guest.gui_text)
    # 逆向き (WAB 400 で入ったのに --h 480)。
    guest = FakeGuest(h=400, depth=1, cirrus=True)
    wire(mod, guest, tmp)
    check(mod.begin_gui(480) is False, "cirrus begin: 高さ違い (400/--h 480) を OK にする")
    check(not guest.gui and guest.depth == 1,
          "cirrus begin: 高さ違い (400/--h 480) で CUI + rshell に戻らない")
    check(not guest.gui_text, "cirrus begin: GUI に text を打ち込む (%r)" % guest.gui_text)


def case_shots(mod, tmp):
    # 撮影ごとに X-Screen-Source を shots.json に残す (Cirrus では wab のはず)。
    # 前の実行 (変異の前の本番の回) の shots.json を読まないよう、毎回新しい場所に撮る。
    out = pathlib.Path(tempfile.mkdtemp(prefix="shots_", dir=str(tmp)))
    guest = FakeGuest(h=480, depth=1, cirrus=True)
    wire(mod, guest, tmp)
    shots = mod.Shots(str(out))
    shots.take("cui")
    mod.begin_gui(480)
    shots.take("gui")
    try:
        log = json.loads((out / "shots.json").read_text(encoding="utf-8"))
    except Exception as e:
        check(False, "shots: shots.json が読めない (%s)" % e)
        return
    got = [(e.get("name"), e.get("src")) for e in log]
    check(got == [("cui", "fake"), ("gui", "wab")],
          "shots: X-Screen-Source の記録が違う (%r)" % (got,))


def case_scenarios(mod, tmp):
    for name, run in (("v11", lambda sh: mod.scenario_v11(480, sh)),
                      ("v12g1", lambda sh: mod.scenario_v12_g1(480, sh)),
                      ("v12g4", lambda sh: mod.scenario_v12_g4(480, sh))):
      for cirrus in (False, True):
        tag = name + (" (cirrus)" if cirrus else "")
        # rshell 有効のまま始めても (リセット直後・leave の後の常態) GUI に入って OK。
        guest = FakeGuest(depth=1, cirrus=cirrus)
        shots = wire(mod, guest, tmp)
        check(run(shots) is True, "%s: 入れるのに NG" % tag)
        check(guest.gui_entries == 1, "%s: GUI に入っていない (%d)" % (tag, guest.gui_entries))
        check(not guest.gui and guest.depth == 1, "%s: CUI + rshell に戻っていない" % tag)
        # GUI に入れない (元の不具合: CUI のまま RESULT: OK) → NG、Run... のパスを打たない。
        guest = FakeGuest(depth=1, gui_ok=False, cirrus=cirrus)
        shots = wire(mod, guest, tmp)
        check(run(shots) is False, "%s: GUI に入らないのに OK" % tag)
        check(not any("/usr" in k.get("text", "") for k in guest.keys),
              "%s: CUI にパスを打ち込む" % tag)
    # --halt の 2 回目の入口も rshell を閉じてから入る。
    guest = FakeGuest(depth=1)
    shots = wire(mod, guest, tmp)
    mod.scenario_v12_g4(480, shots, do_halt=True)
    check(guest.gui_entries == 2, "v12g4 --halt: 2 回目に GUI へ入れない (%d)" % guest.gui_entries)


CASES = {
    "fresh": case_fresh,
    "gui_entered": case_gui_entered,
    "close": case_close,
    "begin": case_begin,
    "begin_cirrus": case_begin_cirrus,
    "shots": case_shots,
    "scenarios": case_scenarios,
}

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
MUTATIONS = [
    (r"    if st\.get\(\"scrn_ymax\"\) == h and st\.get\(\"grph_disp\"\) == 1:\n        return True",
     "    return True",
     "GUI に居るかを見ない (CUI のまま RESULT: OK — 元の不具合)"),
    (r" and st\.get\(\"grph_disp\"\) == 1", "",
     "grph_disp を見ない (9801 の CUI は scrn_ymax 400 で GUI と同じ)"),
    (r"    if st\.get\(\"scrn_ymax\"\) == h and ", "    if ",
     "scrn_ymax を見ない (400 ラインで入ったのを 480 の合格にする)"),
    (r"    if not close_rshell\(\):\n        return False\n    enter_gshell\(\)",
     "    enter_gshell()",
     "rshell を閉じずに os32gui を打つ (§4-31 の罠)"),
    (r"    if not one\(first_wait\):",
     "    if not one(first_wait) and False:",
     "印が出なくても閉じたと言う"),
    (r"    while n <= max_extra and one\(extra_wait\):",
     "    while False:",
     "重なった rshell の外側を閉じない (§4-31)"),
    (r"    return n_after > n_before or a_tail != _tail\(before, 2\)",
     "    return a_tail != _tail(before, 2)",
     "印の数を見ない (末尾が同じ形の新しい印を見落とす)"),
    (r"    return n_after > n_before or a_tail != _tail\(before, 2\)",
     "    return n_after > n_before",
     "末尾の変化を見ない (画面が流れたときの新しい印を見落とす)"),
    (r"    a_tail = _tail\(after, 2\)\n    if not any\(RSHELL_CLOSED in l for l in a_tail\):",
     "    a_tail = _tail(after, 2)\n    if not any(RSHELL_CLOSED in l for l in after):",
     "印が末尾に無くても数える (古い印の後で rshell が生きている)"),
    (r"    back_to_cui\(st, h\)\n    return False",
     "    return False",
     "GUI に入れなかった後に rshell を戻さない"),
    (r"    if not begin_gui\(h\):\n        return False\n    run_dialog\(m, \"/usr/bin/gui_demo.bin\"\)\n    shots\.take\(\"v11_1",
     "    begin_gui(h)\n    run_dialog(m, \"/usr/bin/gui_demo.bin\")\n    shots.take(\"v11_1",
     "v11 が入口の NG を無視する (元の不具合)"),
    (r"    if not begin_gui\(h\):\n        return False\n    shots\.take\(\"g1_1",
     "    begin_gui(h)\n    shots.take(\"g1_1",
     "v12g1 が入口の NG を無視する (元の不具合)"),
    (r"    if not begin_gui\(h\):\n        return False\n    run_dialog\(m, \"/usr/bin/gui_demo.bin\"\)\n    shots\.take\(\"g4_1",
     "    begin_gui(h)\n    run_dialog(m, \"/usr/bin/gui_demo.bin\")\n    shots.take(\"g4_1",
     "v12g4 が入口の NG を無視する (元の不具合)"),
    (r"        if not begin_gui\(h\):\n            return False\n        m\.click\(30, tb\(h\)\)",
     "        enter_gshell()\n        m.click(30, tb(h))",
     "--halt の 2 回目で rshell を閉じない"),
    (r"    if st\.get\(\"grph_disp\"\) == 1:\n        return st\.get\(\"scrn_ymax\"\) or 0\n", "",
     "高さ違いで GUI に入っても CUI の前提で rshell を打つ (Cirrus 試験で GUI に残った)"),
    (r"leave_gshell\(Mouse\(real_h\)\)", "leave_gshell(Mouse(h))",
     "GUI から抜けるのに --h の座標を使う (実際の高さでないと Start に当たらない)"),
    (r"    if real_h is not None:", "    if True:",
     "GUI に入っていなくても leave_gshell を通す (従来どおりでない)"),
    # --- Cirrus (WAB 中継、2026-09-29 夕) ---
    (r"    return st\.get\(\"wab_relay\"\) == 1 and st\.get\(\"wab_height\"\) == h",
     "    return False",
     "WAB 中継を見ない (Cirrus の GUI を NG にする — 試験担当が踏んだ不具合)"),
    (r"    return st\.get\(\"wab_relay\"\) == 1 and ", "    return ",
     "wab_relay を見ない (CUI でも wab_height が残っていれば GUI と読む)"),
    (r" and st\.get\(\"wab_height\"\) == h", "",
     "wab_height を見ない (Cirrus の高さ違いを合格にする)"),
    (r"\"wab_width\", \"wab_height\",", "\"wab_width\",",
     "status() が wab_height を読まない"),
    (r"    if st\.get\(\"wab_relay\"\) == 1:\n        return st\.get\(\"wab_height\"\) or 0\n", "",
     "back_to_cui が WAB 中継を見ない (grph_disp 0 で CUI と読み gshell に rshell を打つ)"),
    (r"return st\.get\(\"wab_height\"\) or 0", "return st.get(\"scrn_ymax\") or 0",
     "Cirrus で抜けるのに scrn_ymax (400) の座標を使う"),
    (r"        self\._record\(name, src, size\)\n", "",
     "撮影の X-Screen-Source を記録しない"),
]


def run(mod, names, tmp):
    global FAILED
    bad = 0
    for name in names:
        FAILED = []
        log = io.StringIO()
        with contextlib.redirect_stdout(log):   # 道具の手順表示は落ちたときだけ出す
            CASES[name](mod, tmp)
        rc = 1 if FAILED else 0
        if rc:
            print(log.getvalue(), end="", flush=True)
        for f in FAILED:
            print("  FAIL %s: %s" % (name, f), flush=True)
        print("EXIT %s=%d" % (name, rc), flush=True)
        bad += rc
    print("SUMMARY %d/%d PASS" % (len(names) - bad, len(names)), flush=True)
    return bad


def mutate(tmp):
    global FAILED
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        text, n = re.subn(pattern, repl, original, count=1)
        shared = False
        if n == 0:
            text, n = re.subn(pattern, repl, SHARED.read_text(encoding="utf-8"), count=1)
            shared = True
        if n != 1:
            print("MUTATION %d NOT APPLICABLE: %s" % (i, why), flush=True)
            bad += 1
            continue
        # **実物は書き換えない** — 写しの上で変異させる (check-par で並列可)。
        path = pathlib.Path(tmp) / ("mut%d.py" % i)
        path.write_text(text, encoding="utf-8")
        try:
            if shared:
                spec = importlib.util.spec_from_file_location("shared_mutant", str(path))
                mutant = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mutant)
                mod = load()
                mod.gui_entered = mutant.gui_entered
                mod.gui_height = mutant.gui_height
            else:
                mod = load(path)
        except Exception:
            print("MUTATION %d RED (import): %s" % (i, why), flush=True)
            continue
        hits = 0
        for name in CASES:
            FAILED = []
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    CASES[name](mod, tmp)
            except (Exception, Runaway):
                FAILED.append("raised")
            hits += 1 if FAILED else 0
        status = "RED" if hits else "**GREEN (見逃し)**"
        print("MUTATION %d %s (%d 件): %s" % (i, status, hits, why), flush=True)
        bad += not hits
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    names = [a for a in args if not a.startswith("--")] or list(CASES)
    with tempfile.TemporaryDirectory(prefix="os32-gui-gate-") as tmp:
        mod = load()
        print("HOST import PASS (real tools/gui_gate.py)", flush=True)
        rc = run(mod, names, tmp)
        if "--mutate" in args:
            rc += mutate(tmp)
    sys.exit(bool(rc))
