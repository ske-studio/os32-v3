import json
import re
import sys

# 使い方:
#   python3 sdk/gen_kapi.py                     生成する (既定)
#   python3 sdk/gen_kapi.py --check-only [json] 検査だけして何も書かない
#                                               (make check の check-kapi-out)
CHECK_ONLY = "--check-only" in sys.argv
_rest = [a for a in sys.argv[1:] if not a.startswith("-")]
JSON_PATH = _rest[0] if _rest else "sdk/kapi.json"

with open(JSON_PATH, "r", encoding="utf-8") as f:
    data = json.load(f)

def get_arg_names(args):
    names = []
    for a in args:
        if a == "...":
            names.append("...")
            continue
        m = re.search(r'([a-zA-Z0-9_]+)(\[[0-9]*\])?$', a)
        if m:
            names.append(m.group(1))
        else:
            names.append(a)
    return names


# ======================================================================== #
#  出力ポインタの書き込み可検査 (票 docs/tasks/memory/TASK_KAPI_OUTPUT_GUARD)
#
#  OS32 は CR0.WP = 0 で走るので、CPL=0 の wrapper は CPL=3 が渡した
#  **読み取り専用の USER ページ** (共有ライブラリの .text/.rodata、全アプリで
#  同じ物理) にも #PF なしで書けてしまう。ディスパッチャの早期検査
#  (kapi_argptr → ring3_ptr_ok) は「帯の中か」しか見ない。
#
#  そこで kapi.json の各エントリに `out` を書き、**target を呼ぶ前**に
#  出力範囲が present + RW + USER であることを確かめる wrapper を生成する。
#
#    "out": [{"arg": "buf",  "len": "size"}]            長さ引数つき
#           [{"arg": "info", "size": 40}]               固定長 (int か C 式)
#           [{"arg": "buf",  "len": "count", "unit": 512}]  個数 × 単位
#    "out": "none"    非 const ポインタが**入力**か関数ポインタ (書かない)
#    "out": "target"  target / body 自身が ring3_user_ranges_writable で
#                     検査している (kapi_sys_time_now / kapi_pci_bind_info)。
#                     ここで二重に見ると CR3 の往復が 2 倍になる。
#
#  **非 const のポインタ引数を持つのに `out` が無いエントリは生成エラー**
#  (check-kapi-out)。足したときに書き忘れると穴が開くため。
# ======================================================================== #

def _arg_decl_map(args):
    """引数名 → 宣言文字列 (可変長は除く)"""
    m = {}
    for a in args:
        if a == "...":
            continue
        m[get_arg_names([a])[0]] = a
    return m


def _is_out_ptr(decl):
    """非 const のポインタ引数か (= 書かれ得る引数)"""
    return ("*" in decl) and ("const" not in decl)


def _len_is_signed(decl):
    """長さ引数が符号つきか。負の長さは 0 に丸めて検査を飛ばす
    (既存の KAPI ごとの「長さ <= 0 のときの扱い」を変えないため)。"""
    d = decl.replace("*", " ")
    if re.search(r"\b(u8|u16|u32|u64|unsigned|size_t)\b", d):
        return False
    return True


def out_errors(api):
    """1 エントリ分の `out` の検査。エラー文字列の配列を返す。"""
    errs = []
    name = api.get("name", "?")
    args = api.get("args", []) or []
    decls = _arg_decl_map(args)
    outs = [n for n, d in decls.items() if _is_out_ptr(d)]
    spec = api.get("out", None)

    if spec is None:
        if outs:
            errs.append(
                "%s: 非 const のポインタ引数 %s があるのに \"out\" が無い。"
                "出力なら [{\"arg\":\"%s\",\"size\":N}] / [{\"arg\":\"%s\","
                "\"len\":\"<長さ引数>\"}]、書かないなら \"none\" を書く "
                "(票 TASK_KAPI_OUTPUT_GUARD)" % (name, ",".join(outs), outs[0], outs[0]))
        return errs

    if spec in ("none", "target"):
        if not outs:
            errs.append("%s: 非 const のポインタ引数が無いのに \"out\": \"%s\" がある"
                        % (name, spec))
        return errs

    if not isinstance(spec, list) or not spec:
        errs.append("%s: \"out\" は非空の配列か \"none\" / \"target\"" % name)
        return errs

    if not outs:
        errs.append("%s: 非 const のポインタ引数が無いのに \"out\" の範囲がある" % name)

    if api.get("direct", False):
        errs.append("%s: \"direct\" のエントリには wrapper が無いので範囲を検査できない "
                    "(\"none\" / \"target\" のみ)" % name)

    seen = []
    for r in spec:
        if not isinstance(r, dict) or "arg" not in r:
            errs.append("%s: \"out\" の要素は {\"arg\": ...} の辞書" % name)
            continue
        a = r["arg"]
        if a not in decls:
            errs.append("%s: \"out\" の arg \"%s\" は引数に無い" % (name, a))
            continue
        if not _is_out_ptr(decls[a]):
            errs.append("%s: \"out\" の arg \"%s\" は非 const のポインタではない (%s)"
                        % (name, a, decls[a]))
        if a in seen:
            errs.append("%s: \"out\" の arg \"%s\" が重複している" % (name, a))
        seen.append(a)
        has_len = "len" in r
        has_size = "size" in r
        if has_len == has_size:
            errs.append("%s/%s: \"len\" か \"size\" のどちらか一方を書く" % (name, a))
        if has_len:
            if r["len"] not in decls:
                errs.append("%s/%s: \"len\" の \"%s\" は引数に無い" % (name, a, r["len"]))
            elif _is_out_ptr(decls[r["len"]]) or "*" in decls[r["len"]]:
                errs.append("%s/%s: \"len\" の \"%s\" はポインタ" % (name, a, r["len"]))
            if "unit" in r:
                u = r["unit"]
                if isinstance(u, bool) or not (isinstance(u, int) or isinstance(u, str)):
                    errs.append("%s/%s: \"unit\" は正の整数か C の式の文字列" % (name, a))
                elif isinstance(u, int) and u <= 0:
                    errs.append("%s/%s: \"unit\" は正の整数" % (name, a))
                elif isinstance(u, str) and not u.strip():
                    errs.append("%s/%s: \"unit\" の式が空" % (name, a))
        if has_size:
            if "unit" in r:
                errs.append("%s/%s: \"unit\" は \"len\" と組で使う" % (name, a))
            s = r["size"]
            if isinstance(s, bool) or not (isinstance(s, int) or isinstance(s, str)):
                errs.append("%s/%s: \"size\" は正の整数か C の式の文字列" % (name, a))
            elif isinstance(s, int) and s <= 0:
                errs.append("%s/%s: \"size\" は正の整数" % (name, a))
            elif isinstance(s, str) and not s.strip():
                errs.append("%s/%s: \"size\" の式が空" % (name, a))

    for a in outs:
        if a not in seen:
            errs.append("%s: 非 const のポインタ引数 \"%s\" が \"out\" に無い "
                        "(出力でないなら \"out\": \"none\"、target が検査するなら "
                        "\"target\")" % (name, a))
    return errs


def check_all_out(data):
    errs = []
    for api in data.get("api", []):
        errs.extend(out_errors(api))
    return errs


def out_ranges_c(api):
    """wrapper の先頭に出す (addr, len) の C 式の配列。無ければ空。"""
    spec = api.get("out", None)
    if not isinstance(spec, list):
        return []
    decls = _arg_decl_map(api.get("args", []) or [])
    ranges = []
    for r in spec:
        a = r["arg"]
        if "size" in r:
            s = r["size"]
            sz = ("%uu" % s) if isinstance(s, int) else str(s)
            ln = "KAPI_OUT_LEN(%s, %s)" % (a, sz)
        else:
            ln_arg = r["len"]
            if _len_is_signed(decls[ln_arg]):
                ln = "KAPI_OUT_LEN_S(%s, %s)" % (a, ln_arg)
            else:
                ln = "KAPI_OUT_LEN(%s, %s)" % (a, ln_arg)
            if "unit" in r:
                u = r["unit"]
                ustr = ("%uu" % u) if isinstance(u, int) else "(u32)(%s)" % u
                ln = "kapi_out_mul(%s, %s)" % (ln, ustr)
        ranges.append(("(u32)%s" % a, ln))
    return ranges


_out_errs = check_all_out(data)
if _out_errs:
    sys.stderr.write("ERROR: sdk/kapi.json の \"out\" 記述 (票 TASK_KAPI_OUTPUT_GUARD):\n")
    for e in _out_errs:
        sys.stderr.write("  - %s\n" % e)
    sys.stderr.write("  → %d 件。docs/KAPI_SPEC.md §3-3 を読む。\n" % len(_out_errs))
    sys.exit(1)

# ======================================================================== #
#  関数表の容量とデータ欄の固定配置 (票 TASK_KAPI_DATA_FIELDS、KAPI v63)
#
#  v62 まではデータ欄 (sbrk_heap_limit / shm_base) を関数表の直後に置いて
#  いたので、関数を 1 つ足すたびにオフセットが 4 バイト動き、旧バイナリが
#  黙って別の値を読んでいた。v63 から関数表の容量を `func_capacity` (R) で
#  予約し、データ欄を 8 + 4 × R に**固定**する。
#
#  R はトランポリン 1 ページの容量から決めた (exec/exec.c の STATIC_ASSERT):
#    sizeof(KernelAPI) + スタブ 8B × R + 写し場 256B ≤ 4096
#    → 8 + 4R + 8 + 8R + 256 ≤ 4096 → R ≤ 318
#  **関数数が R を超えたら生成を拒否する** — 越えるとデータ欄を動かすしか
#  なくなり、全バイナリの作り直しになる (次の R を決める票を起こす)。
# ======================================================================== #

def layout_errors(data):
    """容量と配置の検査。エラー文字列の配列を返す。"""
    errs = []
    n = len(data.get("api", []))
    cap = data.get("func_capacity", None)
    if cap is None:
        errs.append("\"func_capacity\" が無い (関数表の容量 R。データ欄は 8 + 4R に固定)")
        return errs
    if isinstance(cap, bool) or not isinstance(cap, int) or cap <= 0:
        errs.append("\"func_capacity\" は正の整数 (いま %r)" % (cap,))
        return errs
    if n > cap:
        errs.append("関数が %d 本あり、容量 func_capacity=%d を超えた。データ欄を"
                    "動かさずに足せるのは %d 本まで — 次の R を決める票を起こす"
                    " (docs/ROADMAP.md の目安)" % (n, cap, cap))
    sym = data.get("crt_kapi_symbol", None)
    if not isinstance(sym, str) or not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", sym or ""):
        errs.append("\"crt_kapi_symbol\" は C の識別子 (crt の大域変数 kapi の実名)")
    return errs


_layout_errs = layout_errors(data)
if _layout_errs:
    sys.stderr.write("ERROR: sdk/kapi.json の関数表の容量 (票 TASK_KAPI_DATA_FIELDS):\n")
    for e in _layout_errs:
        sys.stderr.write("  - %s\n" % e)
    sys.exit(1)

FUNC_COUNT = len(data["api"])
FUNC_CAPACITY = data["func_capacity"]
RESERVED = FUNC_CAPACITY - FUNC_COUNT
DATA_FIELDS_OFF = 8 + 4 * FUNC_CAPACITY
CRT_KAPI_SYMBOL = data["crt_kapi_symbol"]
LAYOUT_SECTION = ".os32_kapi_layout"

if CHECK_ONLY:
    print("check-kapi-out: %s の \"out\" 記述 OK (%d エントリ)"
          % (JSON_PATH, len(data.get("api", []))))
    sys.exit(0)

# --- HEADER (os32_kapi_generated.h) ---
header_content = """/* AUTOGENERATED FILE - DO NOT EDIT */
#ifndef OS32_KAPI_GENERATED_H
#define OS32_KAPI_GENERATED_H

typedef struct {
    u32 magic;
    u32 version;
"""
for api in data["api"]:
    ret = api["ret"]
    name = api["name"]
    args = ", ".join(api["args"]) if api.get("args") else "void"
    header_content += f"    {ret} (__cdecl *{name})({args});\n"

# 予約スロット (票 TASK_KAPI_DATA_FIELDS)。カーネルは「未実装」の関数
# (kapi_reserved_nosys = OS32_ERR_NOSYS) で埋め、CPL=3 のトランポリンは
# int 0x80 のスタブを置く (ディスパッチャが slot >= KAPI_FUNC_COUNT で kill)。
# どちらも NULL にしない — 旧 SDK のヘッダで新しい関数を呼んだ場合に
# 0 番地へ飛ばないため。
if RESERVED > 0:
    header_content += (f"    /* 予約 (KAPI_FUNC_COUNT..KAPI_FUNC_CAPACITY-1、{RESERVED} 本)。"
                       "末尾追記はここを削って使う */\n")
    header_content += f"    i32 (__cdecl *kapi_reserved[{RESERVED}])(void);\n"

# データフィールド (関数ポインタではない u32 等のフィールド)。
# v63 から 8 + 4 × KAPI_FUNC_CAPACITY に固定 (関数を足しても動かない)。
for field in data.get("data_fields", []):
    comment = field.get("comment", "")
    if comment:
        header_content += f"    {field['type']} {field['name']};  /* {comment} */\n"
    else:
        header_content += f"    {field['type']} {field['name']};\n"

header_content += "} KernelAPI;\n\n"

# --- M2 (KAPI トランポリン): CONTRACTS C5 ---
# KAPI_FUNC_COUNT = 関数スロット数 (トランポリンのスタブ生成ループ / int 0x80
# ディスパッチャの範囲チェックに使う)。データフィールドは含めない。
header_content += f"#define KAPI_FUNC_COUNT {len(data['api'])}\n"
# 関数表の容量 (予約込みのスロット数)。トランポリンのスタブはこの本数ぶん置く。
header_content += f"#define KAPI_FUNC_CAPACITY {FUNC_CAPACITY}\n"
header_content += f"#define KAPI_FUNC_RESERVED {RESERVED}\n"
# データ欄の先頭オフセット (固定、票 TASK_KAPI_DATA_FIELDS)。OS32X ヘッダ v3 の
# kapi_data_off と一致しなければ exec / shlib ローダが断る。
header_content += f"#define KAPI_DATA_FIELDS_OFF 0x{DATA_FIELDS_OFF:X}\n"
for _i, _field in enumerate(data.get("data_fields", [])):
    # u32 単位の添字 (トランポリンの表 tbl[] を書くときに使う)
    header_content += (f"#define KAPI_DATA_IDX_{_field['name'].upper()} "
                       f"{DATA_FIELDS_OFF // 4 + _i}\n")
header_content += f"#define OS32_KAPI_LAYOUT_SECTION \"{LAYOUT_SECTION}\"\n"
header_content += """
/* 配置の刻印 (票 TASK_KAPI_DATA_FIELDS、ヘッダ v3)。ELF の非ロードの
 * セクション .os32_kapi_layout に KAPI_DATA_FIELDS_OFF を 1 語置く。
 * mkos32x.py / mkshlib.py がそれを読んで OS32X ヘッダ v3 の kapi_data_off に
 * 写す (刻印が無ければ生成を断る)。置くのは crt0 (sdk/crt/crt0_c.c) の 1 か所
 * と、crt0 を使わない試験バイナリ・Rust の os32api。ファイルスコープに
 * `OS32_KAPI_LAYOUT_STAMP();` と書く。フラグ "" = 非 alloc なので平らな
 * バイナリには入らない。 */
"""
STAMP_DEFINE = ("#define OS32_KAPI_LAYOUT_STAMP() __asm__(\".pushsection "
                f"{LAYOUT_SECTION},\\\"\\\",@progbits\\n\\t.p2align 2\\n"
                f"\\t.long 0x{DATA_FIELDS_OFF:X}\\n\\t.popsection\")\n")
header_content += STAMP_DEFINE
header_content += f"""
/* 作り直し忘れの検出 (ユーザー決裁 2026-09-24)。crt の大域変数 `kapi` の
 * 実名を {CRT_KAPI_SYMBOL} にする。v62 以前にコンパイルしたオブジェクトは
 * `kapi` を参照したままなので、新しい crt とリンクすると未定義参照で落ちる。
 * カーネル (__KERNEL_BUILD__) は自前の kapi を持つので対象外。 */
#ifndef __KERNEL_BUILD__
#define OS32_KAPI_CRT_SYMBOL {CRT_KAPI_SYMBOL}
#define kapi {CRT_KAPI_SYMBOL}
#endif
"""
# kapi_argsize[slot] = 各スロットの cdecl 引数バイト数 (固定分)。i386 では
# int/ポインタ/char/short いずれも 4B スタックスロット。可変長 (...) は
# 固定分のみを数える (ディスパッチャが下限に使う)。カーネル側 (kapi_generated.c)
# で定義。プログラム側では未使用。
header_content += "extern const u16 kapi_argsize[KAPI_FUNC_COUNT];\n"
# kapi_argptr[slot] = 固定引数のうちポインタ型のビットマスク (bit k = 引数 k が
# ポインタ)。int 0x80 ディスパッチャが wrap 呼び出し前に 0x400000帯/SHM/VRAM
# 範囲を早期検証するのに使う (可変長 ... はガードで担保)。カーネル側で定義。
header_content += "extern const u16 kapi_argptr[KAPI_FUNC_COUNT];\n"

header_content += "\n#endif\n"

with open("sdk/include/os32/os32_kapi_generated.h", "w", encoding="utf-8") as f:
    f.write(header_content)

# --- SLOTS HEADER (os32_kapi_slots.h) ---
# int 0x80 のスロット番号を名前で引くための定数。型やプロトタイプに依存しない
# 単独のヘッダにしておく: crt0/KAPI 非依存の自己完結テスト (userland/tests/
# ring3_guard.c 等) が `int 0x80` を直に発行するとき、直値 (84 = sys_exit) を
# 書かずに済むようにするため (レビュー #7 の nit、2026-09-06)。KAPI は末尾追記
# のみなので番号は変わらないが、名前で書く方が読める。
slots_content = """/* AUTOGENERATED FILE - DO NOT EDIT (sdk/gen_kapi.py, from sdk/kapi.json) */
/* int 0x80 のスロット番号 (KernelAPI 関数表の index、CONTRACTS C5)。      */
/* 型に依存しないので、どのソースからも単独で include できる。             */
#ifndef OS32_KAPI_SLOTS_H
#define OS32_KAPI_SLOTS_H

"""
for i, api in enumerate(data["api"]):
    slots_content += f"#define KAPI_SLOT_{api['name'].upper()} {i}\n"
slots_content += f"\n#define KAPI_SLOT_COUNT {len(data['api'])}\n"
# crt0 を使わない試験バイナリ (ring3_hello 等) はこのヘッダだけを引くので、
# 配置の刻印もここに出す (os32_kapi_generated.h と同じ字面 = 再定義しても可)。
slots_content += f"#define KAPI_FUNC_CAPACITY {FUNC_CAPACITY}\n"
slots_content += f"#define KAPI_DATA_FIELDS_OFF 0x{DATA_FIELDS_OFF:X}\n"
slots_content += STAMP_DEFINE
slots_content += "\n#endif\n"

with open("sdk/include/os32/os32_kapi_slots.h", "w", encoding="utf-8") as f:
    f.write(slots_content)


# --- C FILE (kapi_generated.c) ---
c_content = "/* AUTOGENERATED FILE - DO NOT EDIT */\n#include \"os32_kapi_shared.h\"\n"
for inc in data.get("includes", []):
    c_content += f"#include \"{inc}\"\n"

c_content += "\n"
for ext in data.get("externs", []):
    c_content += f"{ext}\n"
c_content += "\n"

# 計測ビルド (-DKAPI_PROFILE) 用のカウンタ。既定では KAPI_HIT が空になる。
# slot 番号は api 配列の並び順 = KernelAPI 表の並び順 (exec_kapi_init.inc と同じ)。
c_content += '#include "kapi_profile.h"\n\n'
c_content += "#ifdef KAPI_PROFILE\n"
c_content += f"volatile u32 kapi_hits[{len(data['api'])}];\n"
c_content += "#endif\n\n"

# --- M2: kapi_argsize[] (CONTRACTS C5) ---
# slot = api 配列の並び順 (KernelAPI 表 / wrap / init と同一)。
c_content += "/* 各スロットの cdecl 引数バイト数 (固定分)。int 0x80 ディスパッチャが\n"
c_content += " * ユーザスタックからこの量をコピーして本物の wrap を呼ぶ (可変長は下限)。 */\n"
c_content += "const u16 kapi_argsize[KAPI_FUNC_COUNT] = {\n"
for _slot, _api in enumerate(data["api"]):
    _fixed = [a for a in _api.get("args", []) if a != "..."]
    _bytes = 4 * len(_fixed)
    _variadic = any(a == "..." for a in _api.get("args", []))
    _tag = "  /* %s%s */" % (_api["name"], " (...)" if _variadic else "")
    c_content += f"    {_bytes},{_tag}\n"
c_content += "};\n\n"

# --- M2e: kapi_argptr[] (CONTRACTS C5 追記) ---
c_content += "/* 各スロットの固定引数のうちポインタ型のビットマスク (bit k = 引数 k)。\n"
c_content += " * ディスパッチャが wrap 前に範囲検証する引数を示す (可変長はガード担保)。 */\n"
c_content += "const u16 kapi_argptr[KAPI_FUNC_COUNT] = {\n"
for _slot, _api in enumerate(data["api"]):
    _mask = 0
    _k = 0
    for _a in _api.get("args", []):
        if _a == "...":
            continue
        if "*" in _a:
            _mask |= (1 << _k)
        _k += 1
    _pnames = [get_arg_names([a])[0] for a in _api.get("args", []) if a != "..." and "*" in a]
    _tag = ("  /* %s: %s */" % (_api["name"], ",".join(_pnames))) if _mask else ("  /* %s */" % _api["name"])
    c_content += f"    0x{_mask:04X},{_tag}\n"
c_content += "};\n\n"

# --- 出力ポインタの書き込み可検査 (票 TASK_KAPI_OUTPUT_GUARD) -------------
_all_ranges = [out_ranges_c(a) for a in data["api"]]
_needs_mul = any("kapi_out_mul(" in ln for rs in _all_ranges for (_p, ln) in rs)

c_content += "/* ---- 出力ポインタの書き込み可検査 (票 TASK_KAPI_OUTPUT_GUARD) --------\n"
c_content += " * OS32 は CR0.WP = 0 なので、CPL=0 の wrapper は CPL=3 が渡した\n"
c_content += " * 読み取り専用の USER ページ (共有ライブラリの .text/.rodata、全アプリで\n"
c_content += " * 同じ物理) にも #PF なしで書ける。ディスパッチャの早期検査\n"
c_content += " * (kapi_argptr → ring3_ptr_ok) は帯しか見ないので、**target を呼ぶ前**に\n"
c_content += " * ring3_user_ranges_writable で present + RW + USER を確かめる。\n"
c_content += " * 断ったら ring3_fault_kill() (戻らない)。CPL=0 の直呼びは素通し。\n"
c_content += " *\n"
c_content += " * 長さの決め方 (kapi.json の \"out\" から生成):\n"
c_content += " *   NULL          → その範囲は見ない (KAPI ごとの NULL の扱いを変えない)\n"
c_content += " *   長さ 0 / 負   → 見ない (target 側も書かないか、既存どおりの扱い)\n"
c_content += " *   個数 × 単位   → あふれたら 0xFFFFFFFF (= 必ず拒否) にする\n"
c_content += " * 1 回の呼び出しで 2 範囲まで見られるので、3 範囲以上は 2 本ずつに割る\n"
c_content += " * (**どれか 1 つでも不可なら 1 バイトも書かない**)。 */\n"
c_content += "#define KAPI_OUT_LEN(p, n)    ((p) ? (u32)(n) : 0u)\n"
c_content += "#define KAPI_OUT_LEN_S(p, n)  (((p) && (int)(n) > 0) ? (u32)(n) : 0u)\n"
if _needs_mul:
    c_content += "\nstatic u32 kapi_out_mul(u32 n, u32 unit)\n{\n"
    c_content += "    if (unit == 0) return 0u;\n"
    c_content += "    if (n > (0xFFFFFFFFUL / unit)) return 0xFFFFFFFFUL;  /* あふれ → 拒否 */\n"
    c_content += "    return n * unit;\n}\n"
c_content += "\n"


def out_guard_c(api):
    """wrapper 先頭に置く検査。C89 なので宣言の後、文の先頭に出る。"""
    ranges = out_ranges_c(api)
    if not ranges:
        return ""
    pairs = []
    i = 0
    while i < len(ranges):
        a = ranges[i]
        b = ranges[i + 1] if i + 1 < len(ranges) else ("(u32)0", "0u")
        pairs.append((a, b))
        i += 2
    calls = []
    for (a, b) in pairs:
        head = "!ring3_user_ranges_writable("
        one = "%s%s, %s,\n                                    %s, %s)" % (
            head, a[0], a[1], b[0], b[1])
        longest = max(len(x) for x in (a[0] + a[1], b[0] + b[1]))
        # 長い式は 1 行 1 引数にして桁を抑える
        if longest > 56:
            one = "%s\n            %s, %s,\n            %s, %s)" % (
                head, a[0], a[1], b[0], b[1])
        calls.append(one)
    text = "    /* 出力範囲が書けるか (票 TASK_KAPI_OUTPUT_GUARD) */\n"
    text += "    if (" + " ||\n        ".join(calls) + ") {\n"
    text += "        ring3_fault_kill();   /* 戻らない */\n    }\n"
    return text


# 予約スロットの「未実装」(CPL=0 の呼び手向け。CPL=3 はトランポリンの
# スタブ → ディスパッチャが slot >= KAPI_FUNC_COUNT で kill)。
c_content += "/* 予約スロット (KAPI_FUNC_COUNT..KAPI_FUNC_CAPACITY-1) の中身。\n"
c_content += " * CPL=0 の呼び手 (常駐シェル・--cpl0) は NULL ではなくここへ来る。 */\n"
c_content += "i32 __cdecl kapi_reserved_nosys(void)\n{\n    return OS32_ERR_NOSYS;\n}\n\n"

for slot, api in enumerate(data["api"]):
    if api.get("direct", False):
        continue

    ret = api["ret"]
    name = api["name"]
    args_str = ", ".join(api.get("args", []))
    if not args_str: args_str = "void"

    c_content += f"{ret} __cdecl wrap_{name}({args_str})\n{{\n"
    c_content += f"    KAPI_HIT({slot});\n"
    c_content += out_guard_c(api)

    if "body" in api:
        lines = api["body"].split("\n")
        for line in lines:
            c_content += f"    {line}\n"
    else:
        target = api.get("target", name)
        arg_names = get_arg_names(api.get("args", []))
        args_call = ", ".join(arg_names)
        
        if ret == "void":
            c_content += f"    {target}({args_call});\n"
        else:
            c_content += f"    return {target}({args_call});\n"
            
    c_content += "}\n\n"

with open("kapi/kapi_generated.c", "w", encoding="utf-8") as f:
    f.write(c_content)

# --- INC FILE (exec_kapi_init.inc) ---
inc_content = "/* AUTOGENERATED FILE - DO NOT EDIT */\n"

for api in data["api"]:
    if not api.get("direct", False):
        ret = api["ret"]
        name = api["name"]
        args_str = ", ".join(api.get("args", []))
        if not args_str: args_str = "void"
        inc_content += f"extern {ret} __cdecl wrap_{name}({args_str});\n"

inc_content += "extern i32 __cdecl kapi_reserved_nosys(void);\n"
inc_content += "\nkapi->magic = 0x4B415049UL;\n"
inc_content += f"kapi->version = {data['version']};\n\n"

for api in data["api"]:
    name = api["name"]
    if api.get("direct", False):
        target = api.get("target", name)
        inc_content += f"    kapi->{name} = (void *){target};\n"
    else:
        inc_content += f"    kapi->{name} = wrap_{name};\n"

if RESERVED > 0:
    inc_content += "    {\n        u32 _kr;\n"
    inc_content += "        for (_kr = 0; _kr < (u32)KAPI_FUNC_RESERVED; _kr++)\n"
    inc_content += "            kapi->kapi_reserved[_kr] = kapi_reserved_nosys;\n    }\n"

# データフィールド初期化 (デフォルトは0、exec_run等で動的にセットされる)
for field in data.get("data_fields", []):
    inc_content += f"    kapi->{field['name']} = 0;\n"

with open("exec/exec_kapi_init.inc", "w", encoding="utf-8") as f:
    f.write(inc_content)
    
print("SSOT: Successfully generated kapi files.")
