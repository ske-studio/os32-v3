#!/usr/bin/env python3
"""check_gui_proto.py — GUI 共有プロトコルの C ⇄ Rust 照合 (PM 所有)。

GUI v1.1 の作業分担票 §4 で予告し、v1.2 の G0 (契約凍結) の前提になる検査。
`sdk/include/os32/os32_gui_shared.h` (C 側の正典) と
`sdk/rust/os32api/src/gui/proto.rs` (Rust 側の写し) について:

  1. 定数: 名前が `GUI_` / `OS32_ERR_` で始まる `#define` と `pub const` を突き合わせ、
     両方にある名前は値が一致すること、下の REQUIRED_PREFIXES の名前は両方に存在すること。
  2. 構造体: `typedef struct {...} GuiX;` と `#[repr(C)] pub struct GuiX {...}` の
     大きさとフィールド列 (型の大きさの並び) が一致すること。C 側の末尾コメント
     `/* ... 260B */` があればその数字とも一致すること。
     レイアウトは i386 の自然アライン (u8/u16/u32) で両側とも同じ規則で計算する。

終了コード 0 = 一致、1 = ずれあり。`make check` と GitHub Actions が呼ぶ。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C_PATH = os.path.join(ROOT, "sdk/include/os32/os32_gui_shared.h")
RS_PATH = os.path.join(ROOT, "sdk/rust/os32api/src/gui/proto.rs")

# 両側に必ず存在しなければならない名前の接頭辞 (契約で凍結した数値)
REQUIRED_PREFIXES = (
    "GUI_PROTO_VERSION", "GUI_OP_", "GUI_EV_", "GUI_MODAL_", "GUI_SESSION_",
    "GUI_QUIT_REASON_", "GUI_WF_", "GUI_STYLE_", "GUI_SHM_", "GUI_SLOT_",
    "GUI_RING_", "GUI_MAX_",
)
# C 側にだけあってよい名前 (カーネル内部 op など)。Rust 側に無くても NG にしない。
C_ONLY_OK = {"GUI_OP_OWNER_EXIT"}

PRIM = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4, "i32": 4}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def c_eval(expr):
    e = expr.strip()
    e = re.sub(r"\b(0x[0-9A-Fa-f]+|\d+)[uUlL]+\b", r"\1", e)
    e = e.replace("<<", "<<").replace("|", "|")
    try:
        return int(eval(e, {"__builtins__": {}}, {}))
    except Exception:
        return None


def parse_c(text):
    """定数 {name: value} と構造体 {name: [(fieldname, type, count)]} を返す。"""
    raw = text
    text = strip_comments(text)
    consts = {}
    for m in re.finditer(r"^\s*#define\s+((?:GUI_|OS32_ERR_)\w+)\s+(.+?)\s*$", text, re.M):
        v = c_eval(m.group(2))
        if v is not None:
            consts[m.group(1)] = v
    # 定義済み定数を使う式 (GUI_SHM_OFFSET など) の二度目の解決
    for m in re.finditer(r"^\s*#define\s+((?:GUI_|OS32_ERR_)\w+)\s+(.+?)\s*$", text, re.M):
        if m.group(1) in consts:
            continue
        e = m.group(2)
        for k, v in consts.items():
            e = re.sub(r"\b%s\b" % k, str(v), e)
        v = c_eval(e)
        if v is not None:
            consts[m.group(1)] = v

    structs = {}
    sizes_hint = {}
    for m in re.finditer(r"typedef\s+(struct|union)\s*\{(.*?)\}\s*(__attribute__\s*\(\(\s*packed\s*\)\)\s*)?(Gui\w+)\s*;", text, re.S):
        kind, body, packed, name = m.group(1), m.group(2), m.group(3) is not None, m.group(4)
        fields = []
        for decl in body.split(";"):
            decl = decl.strip()
            if not decl:
                continue
            mm = re.match(r"(\w+)\s+(.+)", decl)
            if not mm:
                continue
            ty, names = mm.group(1), mm.group(2)
            for one in names.split(","):
                one = one.strip()
                am = re.match(r"(\w+)\s*\[(\w+)\]", one)
                if am:
                    cnt = c_eval(am.group(2))
                    if cnt is None:
                        cnt = consts.get(am.group(2))
                    fields.append((am.group(1), ty, cnt))
                else:
                    fields.append((one, ty, 1))
        structs[name] = (kind, fields, packed)
    # 末尾コメントの大きさ (raw から): "} GuiX; /* ... 260B */"
    for m in re.finditer(r"\}\s*(?:__attribute__\s*\(\(\s*packed\s*\)\)\s*)?(Gui\w+)\s*;[ \t]*/\*[^*]*?(\d+)B\b", raw):
        sizes_hint[m.group(1)] = int(m.group(2))
    return consts, structs, sizes_hint


def parse_rs(text):
    raw = text
    text = strip_comments(text)
    consts = {}
    for m in re.finditer(r"pub\s+const\s+((?:GUI_|OS32_ERR_)\w+)\s*:\s*\w+\s*=\s*(.+?);", text):
        e = m.group(2).strip()
        e = re.sub(r"\s+as\s+\w+", "", e)
        e = re.sub(r"\b(\d+)(?:u8|u16|u32|i8|i16|i32|usize)\b", r"\1", e)
        for k, v in consts.items():
            e = re.sub(r"\b%s\b" % k, str(v), e)
        v = c_eval(e)
        if v is not None:
            consts[m.group(1)] = v
    structs = {}
    for m in re.finditer(r"#\[repr\(C(\s*,\s*packed)?\)\]\s*(?:#\[[^\]]*\]\s*)*pub\s+(struct|union)\s+(Gui\w+)\s*\{(.*?)\}", text, re.S):
        packed, kind, name, body = m.group(1) is not None, m.group(2), m.group(3), m.group(4)
        fields = []
        for decl in body.split(","):
            decl = decl.strip()
            if not decl:
                continue
            fm = re.match(r"(?:pub(?:\(crate\))?\s+)?(\w+)\s*:\s*(.+)", decl)
            if not fm:
                continue
            fname, fty = fm.group(1), fm.group(2).strip()
            am = re.match(r"\[\s*(\w+)\s*;\s*(\w+)\s*\]", fty)
            if am:
                cnt = c_eval(am.group(2))
                if cnt is None:
                    cnt = consts.get(am.group(2))
                fields.append((fname, am.group(1), cnt))
            else:
                fields.append((fname, fty, 1))
        structs[name] = (kind, fields, packed)
    asserts = {}
    for m in re.finditer(r"size_of::<(Gui\w+)>\(\)\s*==\s*(\d+)", raw):
        asserts[m.group(1)] = int(m.group(2))
    return consts, structs, asserts


def layout(name, structs, memo):
    """(size, align, [(offset, size)...]) を自然アラインで計算する。"""
    if name in PRIM:
        return PRIM[name], PRIM[name], [(0, PRIM[name])]
    if name in memo:
        return memo[name]
    if name not in structs:
        return None
    kind, fields, packed = structs[name]
    off = 0
    align = 1
    cells = []
    for _fname, ty, cnt in fields:
        sub = layout(ty, structs, memo)
        if sub is None or cnt is None:
            return None
        s, a, _ = sub
        if packed:
            a = 1
        if kind == "union":
            cells.append((0, s * cnt))
            off = max(off, s * cnt)
        else:
            off = (off + a - 1) // a * a
            cells.append((off, s * cnt))
            off += s * cnt
        align = max(align, a)
    size = (off + align - 1) // align * align
    memo[name] = (size, align, cells)
    return memo[name]


def main():
    with open(C_PATH, encoding="utf-8") as f:
        c_text = f.read()
    with open(RS_PATH, encoding="utf-8") as f:
        rs_text = f.read()
    c_consts, c_structs, c_hint = parse_c(c_text)
    rs_consts, rs_structs, rs_assert = parse_rs(rs_text)

    bad = []

    # 1. 定数
    for name in sorted(set(c_consts) | set(rs_consts)):
        required = name.startswith(REQUIRED_PREFIXES)
        if name in c_consts and name in rs_consts:
            if c_consts[name] != rs_consts[name]:
                bad.append("定数 %s: C=%d Rust=%d" % (name, c_consts[name], rs_consts[name]))
        elif required:
            if name in C_ONLY_OK and name not in rs_consts:
                continue
            side = "Rust" if name in c_consts else "C"
            bad.append("定数 %s: %s 側に無い" % (name, side))

    # 2. 構造体
    c_memo, rs_memo = {}, {}
    for name in sorted(set(c_structs) & set(rs_structs)):
        cl = layout(name, c_structs, c_memo)
        rl = layout(name, rs_structs, rs_memo)
        if cl is None or rl is None:
            bad.append("構造体 %s: 型を解決できない (C=%s Rust=%s)" % (name, cl is not None, rl is not None))
            continue
        if cl[0] != rl[0]:
            bad.append("構造体 %s: 大きさ C=%dB Rust=%dB" % (name, cl[0], rl[0]))
        elif cl[2] != rl[2]:
            bad.append("構造体 %s: フィールド配置が違う C=%s Rust=%s" % (name, cl[2], rl[2]))
        if name in c_hint and c_hint[name] != cl[0]:
            bad.append("構造体 %s: C の末尾コメント %dB と実寸 %dB がずれ" % (name, c_hint[name], cl[0]))
        if name in rs_assert and rs_assert[name] != rl[0]:
            bad.append("構造体 %s: Rust の size_of assert %dB と実寸 %dB がずれ" % (name, rs_assert[name], rl[0]))
    only_c = sorted(set(c_structs) - set(rs_structs))
    only_rs = sorted(set(rs_structs) - set(c_structs))

    n_const = len(set(c_consts) & set(rs_consts))
    n_struct = len(set(c_structs) & set(rs_structs))
    print("GUI プロトコル照合: 定数 %d 件 / 構造体 %d 件を突き合わせ" % (n_const, n_struct))
    if only_c:
        print("  [--] C 側だけの構造体: %s" % ", ".join(only_c))
    if only_rs:
        print("  [--] Rust 側だけの構造体: %s" % ", ".join(only_rs))
    if bad:
        for b in bad:
            print("  [NG] " + b)
        return 1
    print("  一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
