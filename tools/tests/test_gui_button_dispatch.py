"""libos32gui の Button 配送が左ボタン限定であることの構造ガード。

**挙動試験ではない。** libos32gui にはホスト実行の足場が無いので、実ソースから
`GUI_EV_BUTTON` の match アームを切り出して、ウィジェット配送が
`MOUSE_BTN_LEFT` の判定の内側にあることだけを機械で確かめる。
併せて `MOUSE_BTN_LEFT` の値が C ヘッダ (SSoT) と一致することを見る ([C4])。

背景: WM は契約 D4 で右ボタンも前面窓のクライアントへ配る。種別を見ずに
ウィジェット層へ流すと、右クリックで on_click やチェック切替が起きる
(2026-09-10 のレビュー指摘)。
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
APP = ROOT / "userland/rust/libos32gui/src/app.rs"
HDR = ROOT / "sdk/include/os32/os32_kapi_shared.h"

FAILED = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        FAILED.append(msg)


def button_arm(src):
    """`GUI_EV_BUTTON => { ... }` の中身を波括弧の対応で切り出す。"""
    m = re.search(r"GUI_EV_BUTTON\s*=>\s*\{", src)
    if not m:
        return None
    i = m.end() - 1
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i + 1 : j]
    return None


def main():
    src = APP.read_text(encoding="utf-8")
    hdr = HDR.read_text(encoding="utf-8")

    m = re.search(r"#define\s+MOUSE_BTN_LEFT\s+0x0*1\b", hdr)
    check(m is not None, "os32_kapi_shared.h の MOUSE_BTN_LEFT が 0x01")

    m = re.search(r"const\s+MOUSE_BTN_LEFT:\s*u8\s*=\s*0x0*1\s*;", src)
    check(m is not None, "app.rs の MOUSE_BTN_LEFT が 0x01 (C ヘッダと一致)")

    arm = button_arm(src)
    check(arm is not None, "app.rs に GUI_EV_BUTTON の match アームがある")
    if arm is None:
        return 1

    check("b.button" in arm, "Button アームがボタン種別 (b.button) を見ている")

    # ウィジェット配送は MOUSE_BTN_LEFT の判定より後ろにしか現れないこと。
    gate = arm.find("MOUSE_BTN_LEFT")
    check(gate >= 0, "Button アームに MOUSE_BTN_LEFT の判定がある")
    for name in ("widget::on_button_down", "widget::on_button_up", "emit("):
        pos = arm.find(name)
        check(pos >= 0, f"Button アームに {name} がある")
        check(
            pos >= 0 and gate >= 0 and pos > gate,
            f"{name} が MOUSE_BTN_LEFT の判定より内側にある",
        )

    # 右ボタンを落としてもアプリが取り出せること (on_raw は match より前)。
    raw = src.find("app.on_raw(ui, ev);")
    match_pos = src.find("match ev.kind {")
    check(
        raw >= 0 and match_pos >= 0 and raw < match_pos,
        "on_raw が match より前にあり、右ボタンもアプリに届く",
    )

    print(f"SUMMARY {'PASS' if not FAILED else str(len(FAILED)) + ' FAILED'}", flush=True)
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
