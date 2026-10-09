#!/usr/bin/env python3
"""gui_gate.py — NP21/W ai-debug の HTTP API で GUI のゲート操作列を回す (PM 所有)。

前提: NP21/W (ai-debug 版) が 127.0.0.1:8025 で動き、OS32 が CUI (rshell) で起動済み。
台本は先頭で `/api/key seq=ESC` を送って rshell を閉じ、tvram に `[Remote shell closed]` が
新しく出たことを確かめてから `os32gui` を打つ (`close_rshell`)。確かめられなければ GUI へ
入らずに NG で終わる。rshell が有効なまま `os32gui` を 4 文字ずつ打つと `os32` / `gui` の
2 コマンドになり GUI に入らない (POLICY_DEBUG §4-31。2026-09-29 に R2 の予備調査で再発)。
マウスは `/api/mouse` の **ax/ay (シームレス絶対座標)** を使う (OS32 は NP21/W では
np2sysp getmpos で位置を取るので dx/dy は効かない。POLICY_DEBUG §4-23)。

使い方:
  python3 tools/gui_gate.py v11            # v1.1 回帰: gui_demo のドラッグ / 重なり / クリック
  python3 tools/gui_gate.py v11 --h 400    # 9801 (400 ライン) で
  python3 tools/gui_gate.py shot NAME      # 今の画面を NAME.png に保存するだけ
  python3 tools/gui_gate.py click X Y      # 1 回クリック (デバッグ)

出力: 各手順の要点 1 行と、スクリーンショット (--out、既定 build/out/gui_gate/) の PNG。
判定は人間 (PM) がする。数値だけ自動で照合する: GUI に入った直後の `/api/status` が
「`scrn_ymax == --h` かつ `grph_disp == 1`」(98 の GDC / PEGC) でも「`wab_relay == 1` かつ
`wab_height == --h`」(Cirrus の WAB 中継) でもなければ NG (`gui_entered`。CUI のままでも
`leave_gshell` の `ver` は通るので、これが無いと GUI に入らなくても RESULT: OK になった)。
Cirrus の gshell 中は 98 の表示レジスタが `scrn_ymax 400 grph_disp 0` のまま残る (2026-09-29 夕)。
v11 は加えて最後の `wab_relay == 0`。撮影ごとの `X-Screen-Source` は `--out` の `shots.json` に残る。
v1.2 の台本 (Start / taskbar / dialog / filer / session) は W3〜C5 の結合時にここへ足す。

CUI へ戻る経路について (G5 で ESC の即時切替と上部バーを撤去した。契約 S6 / 票 W3 §4.1):
  どの台本も Start → "CUI mode" → 確認ダイアログ Yes で戻る (`leave_gshell`)。この経路は
  `/etc/system.cfg` の `GUI=0` を**永続化する**が、台本は必ず CUI (rshell) から始まり、
  GUI へは `os32gui` コマンドで入る (`enter_gshell`) ので支障は無い。`os32gui` は cfg の
  値に関わらず GUI へ入り、次回の自動起動だけが CUI になる。GUI 自動起動へ戻したい
  ときは、ゲート後に CUI で `os32gui` を使うか cfg の `GUI=1` を書き戻すこと。
"""
import argparse
import os
import sys
import re
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASE = "http://127.0.0.1:8025"


def post(path, data, timeout=20):
    body = urllib.parse.urlencode(data).encode()
    req = urllib.request.Request(BASE + path, data=body, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def get(path, timeout=20):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as r:
        return r.read(), dict(r.headers)


# Pass the current module attributes: existing host tests patch these names.
from tools.np21w_mcp import gui as _gui

expand_escapes = _gui.expand_escapes


def _transport():
    return _gui.Transport(post=post, get=get, clock=time)


def key(seq=None, text=None, escapes=True):
    return _gui.key(seq, text, escapes, transport=_transport())


def cmd(line, timeout=60):
    return _cmd_raw(line, timeout)


def _cmd_raw(line, timeout):
    req = urllib.request.Request(BASE + "/api/cmd", data=line.encode(), method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


STATUS_KEYS = ("scrn_ymax", "grph_disp", "wab_relay", "wab_width", "wab_height",
               "fault_generation")


def status(keys=STATUS_KEYS):
    import json
    d = json.loads(get("/api/status")[0])
    return {k: d.get(k) for k in keys}


def tvram_lines():
    """`/api/tvram` の 25 行 (右端の空白を落とす)。"""
    import json
    d = json.loads(get("/api/tvram")[0])
    return [l.rstrip() for l in d.get("lines", [])]


RSHELL_CLOSED = "[Remote shell closed]"   # userland/shell/rshell.c の rshell_exit


def _tail(lines, n):
    return [l for l in lines if l.strip()][-n:]


def rshell_closed_fresh(before, after):
    """ESC の前後の tvram (行の列) から、**この ESC で** rshell が閉じたと言えるか。

    rshell は閉じるときに `\n[Remote shell closed]\n` を出し、その後ろには CUI の
    プロンプトしか続かない。だから「印が画面の末尾 (空でない最後の 2 行) にあり、
    かつ ESC の前から変わった」ことを見る。前の回の印が画面に残っているだけでは
    数えない (末尾が同じで、印の数も増えていない)。
    印の数は、同じ末尾 (印 + プロンプト) が 2 回続いたときの判定に使う。"""
    a_tail = _tail(after, 2)
    if not any(RSHELL_CLOSED in l for l in a_tail):
        return False
    n_before = sum(l.count(RSHELL_CLOSED) for l in before)
    n_after = sum(l.count(RSHELL_CLOSED) for l in after)
    return n_after > n_before or a_tail != _tail(before, 2)


def close_rshell(first_wait=5.0, extra_wait=3.0, max_extra=2, poll=0.5):
    """ESC で rshell を閉じ、tvram に `[Remote shell closed]` が新しく出たことを確かめる。

    rshell は重なりうる (§4-31: 手打ちの `rshell` が 2 段になる)。最初の ESC で閉じたのを
    確かめた後も、印が新しく出なくなるまで ESC を足す (最大 `max_extra` 回)。CUI の
    プロンプトでの ESC は行を捨てるだけなので害は無い。
    最初の ESC で印が出なければ False (rshell が居ない・ESC が届かない — どちらにしても
    GUI へ入る前提が確かめられない)。"""
    def one(wait):
        before = tvram_lines()
        key(seq="ESC")
        t0 = time.time()
        while True:
            time.sleep(poll)
            after = tvram_lines()
            if rshell_closed_fresh(before, after):
                return True
            if time.time() - t0 >= wait:
                return False

    if not one(first_wait):
        print("  rshell close: NG (%r が tvram に出ない)" % RSHELL_CLOSED)
        return False
    n = 1
    while n <= max_extra and one(extra_wait):
        n += 1
    print("  rshell close: ok (%d 段)" % n)
    return True


gui_entered = _gui.gui_entered
gui_height = _gui.gui_height


def restore_rshell():
    """GUI に入れなかった後 (CUI・rshell は閉じている) に rshell を戻す。

    CUI のプロンプトは行単位で読むので、ここでは 4 文字ずつの text で困らない。"""
    key(text="rshell")
    time.sleep(0.5)
    key(seq="RETURN")
    time.sleep(2)
    ok = "OS32" in _cmd_raw("ver", 20)
    print("  rshell restore: %s" % ("ok" if ok else "NG"))
    return ok


def begin_gui(h, settle=10.0, poll=1.0):
    """台本の入口: rshell を閉じる → `os32gui` → `/api/status` で GUI に居ることを確かめる。

    False なら台本はそこで NG で終わる (CUI に Run... のパスなどを打ち込まない)。
    NG のときは CUI + rshell へ戻しておく (次の台本・/api/cmd が使えるように)。
    高さ違いで GUI に入ってしまった場合は GUI から抜けてから戻す (`back_to_cui`)。"""
    if not close_rshell():
        return False
    enter_gshell()
    t0 = time.time()
    while True:
        st = status()
        if gui_entered(st, h):
            print("  status %s (GUI ok)" % st)
            return True
        if time.time() - t0 >= settle:
            break
        time.sleep(poll)
    print("  status %s" % st)
    print("  NG: GUI に入っていない (期待 scrn_ymax=%d grph_disp=1 "
          "または wab_relay=1 wab_height=%d)" % (h, h))
    back_to_cui(st, h)
    return False


def back_to_cui(st, h):
    """入口の NG の後始末: CUI + rshell へ戻す。

    `gui_height` が高さを返すなら GUI には入っている (高さが --h と違うだけ)。そこで CUI の
    前提の `restore_rshell` を打つと `rshell` が gshell に打ち込まれ、ゲストが GUI に残る
    (2026-09-29 の Cirrus 試験で 2 回発生: 昼は 98 の `scrn_ymax` 違い、夕は WAB 中継で
    `grph_disp 0` のまま)。その場合は**実際の高さ** (`gui_height`) の座標で
    `leave_gshell` を通して CUI へ戻す (rshell の復旧も leave_gshell がする)。
    GUI に入っていなければ従来どおり `restore_rshell`。"""
    real_h = gui_height(st)
    if real_h is not None:
        real_h = real_h or h
        print("  GUI には入っている (高さ %s) -> その高さで CUI へ戻す" % real_h)
        return leave_gshell(Mouse(real_h))
    return restore_rshell()


class Mouse(_gui.Mouse):
    def __init__(self, h):
        self.h = h

    @property
    def transport(self):
        return _transport()


class Shots:
    """撮影。1 枚ごとに名前・`X-Screen-Source`・寸法を `outdir/shots.json` に書き足す。

    Cirrus では `src=auto` が WAB の画面 (`X-Screen-Source: wab`) を返す — 98 の GDC の
    画面を撮っていないことを後から確かめられるように、撮るたびに記録を書き直す
    (台本が途中で落ちても、それまでの分は残る)。"""

    def __init__(self, outdir):
        self.outdir = outdir
        self.log = []
        os.makedirs(outdir, exist_ok=True)

    def _record(self, name, src, size):
        import json
        self.log.append({"name": name, "src": src,
                         "size": list(size) if size else None})
        with open(os.path.join(self.outdir, "shots.json"), "w", encoding="utf-8") as f:
            json.dump(self.log, f, ensure_ascii=False, indent=1)

    def take(self, name):
        data, hdr = get("/api/screenshot?src=auto")
        bmp = os.path.join(self.outdir, name + ".bmp")
        with open(bmp, "wb") as f:
            f.write(data)
        png = os.path.join(self.outdir, name + ".png")
        size = None
        try:
            from PIL import Image
            im = Image.open(bmp)
            size = im.size
            im.convert("RGB").save(png)
            os.remove(bmp)
            out = png
        except Exception:
            out = bmp
        src = hdr.get("X-Screen-Source", "?")
        self._record(name, src, size)
        print("  shot %-24s src=%s%s" % (name, src,
                                        " %dx%d" % size if size else ""))
        return out


# ---------------------------------------------------------------------------
#  v1.2 の座標 (W3 が報告した値。ax/ay 換算は Mouse が行う)
#  taskbar: Start (30,H-12)、窓ボタン #n (110+100n,H-12)、時計 (614,H-12)
#  Start menu 行 r: start_row(H, r) — 項目数から導く (下の注記)。順は
#    Programs / File Manager / Run... / Settings... / CUI mode / Shut Down (S4 で 6 項目)
#  確認ダイアログ Yes (410, H/2+11) / No (494, H/2+11)、Run... の OK (360, H/2+23)
# ---------------------------------------------------------------------------
def tb(h):
    return h - 12


# Start メニューはタスクバーから**上へ**伸びるので、項目数が増えると全行が上へ
# ずれる。固定値 (H-107+18r) を使っていた頃は、項目が 1 行増えただけで行 r が
# r+1 に当たっていた (2026-09-10: 「CUI mode」のクリックが Shut Down に当たり
# ゲストが halt)。以後は項目数から導く。T5b 撤去で 5 行に戻した (2026-09-10)。
# 項目数は
# userland/gshell/src/startmenu.rs の ROOT_ITEMS、行高 ITEM_H=18、枠 BORDER=2、
# taskbar.rs の TASKBAR_H=24 と一致させること。
START_MENU_ITEMS = 6
# 項目名 → 行番号 (startmenu.rs の IT_* と同じ順)。呼び出し側は数字ではなくこれを使う
# (2026-09-13 S4: 6 項目化で「行 3」が Settings に当たった。start_row は座標を導く
# だけで項目の意味は追従しない)。
ROW_PROGRAMS = 0
ROW_FILEMAN = 1
ROW_RUN = 2
ROW_SETTINGS = 3
ROW_CUI = 4
ROW_HALT = 5
START_MENU_ITEM_H = 18
START_MENU_BORDER = 2
TASKBAR_H = 24


def start_row(h, r):
    top = h - TASKBAR_H - (START_MENU_ITEMS * START_MENU_ITEM_H + START_MENU_BORDER * 2)
    return (82, top + START_MENU_BORDER + START_MENU_ITEM_H * r + START_MENU_ITEM_H // 2)


def enter_gshell():
    """CUI (rshell を抜けた状態) から `os32gui` で GUI へ入る。

    **rshell が有効なまま呼ばない。** rshell は `kbd_trygetchar` の生読みで、入力が
    途切れるたびに 1 コマンドとして実行するので、4 文字ずつの text が
    `os32` / `gui` という別々のコマンドになる (POLICY_DEBUG §4-31)。台本は `begin_gui`
    から呼ぶこと — `close_rshell` で抜けたのを tvram で確かめ、入った後は
    `gui_entered` で `/api/status` を照合する。text と RETURN の間は少し待つ (打鍵の取りこぼし避け)。"""
    key(text="os32gui")
    time.sleep(1.0)
    key(seq="RETURN")
    time.sleep(7)


def run_dialog(mouse, path):
    """Start → "Run..." (行 2) にパスを打って RETURN。アプリが立ち上がるまで待つ。"""
    mouse.click(30, tb(mouse.h))
    mouse.click(*start_row(mouse.h, ROW_RUN))
    time.sleep(1.5)
    key(text=path)
    time.sleep(1)
    key(seq="RETURN")
    time.sleep(5)


def leave_gshell(mouse, shots=None, shot_name=None):
    """Start → "CUI mode" (`ROW_CUI`、S4 で 6 項目化) → 確認ダイアログ Yes で CUI へ戻り、rshell を復旧する。

    G5 で ESC の即時切替は製品から撤去したので、**これが唯一の CUI 復帰経路**
    (契約 S6 / 票 W3 §4.1〜4.2)。`shots` と `shot_name` を渡すと、Yes を押す前の
    確認ダイアログを撮る。

    この経路は `/etc/system.cfg` の `GUI=0` を永続化する。台本は CUI から始めて
    `os32gui` で GUI へ入る前提なので、それで構わない (モジュール先頭の注記を参照)。
    """
    mouse.click(30, tb(mouse.h))
    mouse.click(*start_row(mouse.h, ROW_CUI))
    time.sleep(1.5)
    if shots is not None and shot_name:
        shots.take(shot_name)
    mouse.click(410, mouse.h // 2 + 11)
    time.sleep(6)
    mouse.off()
    # CUI シェルは起動時に自動で rshell へ入る。その上から `rshell` を手打ちすると
    # rshell が 2 段重なり、次の ESC は内側の 1 段しか閉じないので、以降の
    # `/api/key` の text が外側の rshell に食われて GUI へ入れなくなる
    # (2026-09-11: リセット直後の台本だけ成功し、leave 後の台本が全滅した原因)。
    # `ver` が通るなら rshell は既に生きているので打たない。
    out = _cmd_raw("ver", 20)
    if "OS32" not in out:
        key(text="rshell")
        key(seq="RETURN")
        time.sleep(2)
        out = _cmd_raw("ver", 20)
    ok = "OS32" in out
    print("  CUI back: %s" % ("ok" if ok else "NG (rshell?)"))
    return ok


def scenario_v11(h, shots):
    """v1.1 G2 相当: gui_demo の窓 2 枚でドラッグ / 重なり / クリック配送。"""
    m = Mouse(h)
    print("[v11] enter gshell + gui_demo (Start -> Run...)")
    if not begin_gui(h):
        return False
    run_dialog(m, "/usr/bin/gui_demo.bin")
    shots.take("v11_1_two_windows")
    print("[v11] drag Widgets title (100,58) -> (300,208) with XOR frame")
    m.move(100, 58)
    m.press()
    m.move(200, 120)
    m.move(300, 208)
    shots.take("v11_2_drag_frame")
    m.release()
    shots.take("v11_3_dropped_overlap")
    print("[v11] click Help title (450,90) -> raise")
    m.click(450, 90)
    shots.take("v11_4_help_raised")
    # drop 後の Widgets は (240,198) 付近 (drag の差分 +150 を 48 に足した位置)。
    # チェックボックスは窓原点 +(17,63)、OK は +(260,134)。
    print("[v11] checkbox (257,261) / OK (500,332) on moved Widgets")
    m.click(257, 261)
    m.click(500, 332)
    shots.take("v11_5_widgets_clicked")
    print("[v11] close Help via x (604,90)")
    m.click(604, 90)
    shots.take("v11_6_help_closed")
    print("[v11] bottom edge (20,%d)" % (h - 20))
    m.move(20, h - 20)
    shots.take("v11_7_bottom_edge")
    # ESC は gui_demo 自身の終了キー (アプリが処理する)。gshell は ESC を横取り
    # しなくなった (G5) ので、デスクトップへ戻った後は Start 経由で CUI へ抜ける。
    print("[v11] ESC closes demo (app's own quit key), then CUI via Start")
    key(seq="ESC")
    time.sleep(2)
    ok = leave_gshell(m)
    st2 = status()
    print("  status %s" % st2)
    return ok and st2.get("wab_relay") == 0


def scenario_v12_g1(h, shots):
    """G1: taskbar / Start / Programs / context menu / clock / window button."""
    m = Mouse(h)
    print("[g1] enter gshell")
    if not begin_gui(h):
        return False
    shots.take("g1_1_desktop_taskbar")
    print("[g1] Start menu open")
    m.click(30, tb(h))
    shots.take("g1_2_start_menu")
    print("[g1] Programs page")
    m.click(*start_row(h, ROW_PROGRAMS))
    time.sleep(1.5)
    shots.take("g1_3_programs")
    key(seq="ESC")
    time.sleep(0.4)
    key(seq="ESC")
    time.sleep(0.4)
    print("[g1] desktop context menu (right button)")
    m.click(320, 240, btn=2)
    shots.take("g1_4_context_menu")
    key(seq="ESC")
    time.sleep(0.4)
    print("[g1] Run... -> /usr/bin/gui_demo.bin")
    run_dialog(m, "/usr/bin/gui_demo.bin")
    shots.take("g1_5_demo_with_taskbar")
    print("[g1] taskbar window button #1 -> raise Help")
    m.click(210, tb(h))
    shots.take("g1_6_window_button")
    print("[g1] wait for clock minute change (up to 65 s)")
    t0 = time.time()
    a = shots.take("g1_7_clock_a")
    while time.time() - t0 < 65:
        time.sleep(5)
    shots.take("g1_8_clock_b")
    print("[g1] quit demo (ESC = app's own quit key) and back to CUI via Start")
    key(seq="ESC")
    time.sleep(2)
    return leave_gshell(m)


def scenario_v12_g4(h, shots, do_halt=False):
    """G4: app replacement (Run... while an app runs), CUI mode with confirmation,
    optionally Shut Down (halt: NP21/W must be restarted afterwards)."""
    m = Mouse(h)
    print("[g4] gshell + gui_demo via Run...")
    if not begin_gui(h):
        return False
    run_dialog(m, "/usr/bin/gui_demo.bin")
    shots.take("g4_1_demo")
    print("[g4] Run... again while demo runs -> v12_api_test replaces it")
    run_dialog(m, "/usr/bin/v12_api_test.bin")
    time.sleep(1)
    shots.take("g4_2_replaced")
    print("[g4] app-initiated launch (key 4 = session_launch gui_demo)")
    key(text="4")
    time.sleep(6)
    shots.take("g4_3_app_launch")
    print("[g4] CUI mode -> confirmation -> Yes")
    ok = leave_gshell(m, shots, "g4_4_cui_confirm")
    cfg = _cmd_raw("cat /etc/system.cfg", 20)
    print("  system.cfg: %s" % " | ".join(l for l in cfg.splitlines() if "GUI" in l))
    if do_halt:
        print("[g4] Shut Down -> halt (NP21/W must be restarted by os32-cycle deploy)")
        # leave_gshell が rshell を戻しているので、もう一度閉じてから入る。
        if not begin_gui(h):
            return False
        m.click(30, tb(h))
        m.click(*start_row(h, ROW_HALT))
        time.sleep(1.5)
        m.click(410, h // 2 + 11)
        time.sleep(4)
        shots.take("g4_5_halt_screen")
        print("  status %s" % status())
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=["v11", "v12g1", "v12g4", "shot", "click"])
    ap.add_argument("--halt", action="store_true", help="v12g4: 最後に Shut Down まで行う")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--h", type=int, default=480, help="画面高 (9801=400, PEGC/Cirrus=480)")
    ap.add_argument("--out", default="build/out/gui_gate")
    a = ap.parse_args()
    shots = Shots(a.out)
    if a.scenario == "shot":
        shots.take(a.args[0] if a.args else "shot")
        return 0
    if a.scenario == "click":
        Mouse(a.h).click(int(a.args[0]), int(a.args[1]))
        return 0
    if a.scenario == "v12g1":
        ok = scenario_v12_g1(a.h, shots)
    elif a.scenario == "v12g4":
        ok = scenario_v12_g4(a.h, shots, a.halt)
    else:
        ok = scenario_v11(a.h, shots)
    print("RESULT: %s (判定はスクリーンショットで)" % ("OK" if ok else "NG"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
