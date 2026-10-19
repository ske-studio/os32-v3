#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_isa386.py — 最終リンク後の実行コードが 386 (+387) の命令だけか検査する (D36)

docs/tasks/v3/RUST_VS_C11.md §7-2 の 4: 検査するのは .o / .rlib ではなく最終成果物
(LTO・最終リンク後、compiler_builtins・依存クレート・asm 込み) の ELF。CPU 設定や
「エミュレータ/実機で動く」は 386 互換の証拠にならない (NP21/W は CMOV/BSWAP を実装)。

方法は check_privileged.py と同じ: 既存の i386-elf-objdump -d (全セクション強制の -D は
使わない) の命令欄を読む。判定は禁止表ではなく **386/387 の許可表**:
  - lock/rep 等の接頭辞は剥がして次の語を見る
  - AT&T の大きさ接尾辞 (b/w/l/s/t/ll) を 1 つ剥がして許可表と照合する
  - 許可表に無いもの (cmov*/bswap/cmpxchg*/xadd/cpuid/rdtsc/fcomi/fucomi/fcmov*/
    fisttp/SSE/MMX ...) と、operand を持つ nop (0F 1F の多バイト NOP、P6 以降) と、
    %xmm/%ymm/%zmm/%mm レジスタの参照は全部違反
  - ud2 は許す: 386 でも 0F 0B は未定義命令で #UD になり、置いた目的 (止める) と同じ
使い方: check_isa386.py <ELF> [<ELF> ...]   違反 0 で exit 0、あれば 1、道具が無ければ 2。
"""

import os
import re
import subprocess
import sys

from check_privileged import objdump_bin

PREFIXES = {"lock", "rep", "repz", "repe", "repnz", "repne", "data16", "addr16",
            "cs", "ds", "es", "fs", "gs", "ss"}
I386 = set("""
aaa aad aam aas adc add and arpl bound bsf bsr bt btc btr bts call lcall clc cld cli clts
cmc cmp cmps cbtw cwtl cwtd cltd daa das dec div enter hlt idiv imul in inc ins int int3
into iret ja jae jb jbe jc je jg jge jl jle jna jnae jnb jnbe jnc jne jng jnge jnl jnle
jno jnp jns jnz jo jp jpe jpo js jz jcxz jecxz jmp ljmp lahf lar lds lea leave les lfs
lgdt lgs lidt lldt lmsw lods loop loope loopne loopz loopnz lsl lss ltr mov movs movsb
movsw movsbw movsbl movswl movzb movzw movzbw movzbl movzwl mul neg nop not or out outs
pop popa popf push pusha pushf rcl rcr ret lret rol ror sahf sal sar sbb scas seta setae
setb setbe setc sete setg setge setl setle setna setnae setnb setnbe setnc setne setng
setnge setnl setnle setno setnp setns setnz seto setp setpe setpo sets setz sgdt shl shld
shr shrd sidt sldt smsw stc std sti stos str sub test verr verw wait fwait xchg xlat
xor ud2
f2xm1 fabs fadd faddp fbld fbstp fchs fclex fnclex fcom fcomp fcompp fcos fdecstp fdiv
fdivp fdivr fdivrp ffree fiadd ficom ficomp fidiv fidivr fild fimul fincstp finit fninit
fist fistp fisub fisubr fld fld1 fldcw fldenv fldl2e fldl2t fldlg2 fldln2 fldpi fldz fmul
fmulp fnop fpatan fprem fprem1 fptan frndint frstor fsave fnsave fscale fsin fsincos
fsqrt fst fstcw fnstcw fstenv fnstenv fstp fstsw fnstsw fsub fsubp fsubr fsubrp ftst
fucom fucomp fucompp fxam fxch fxtract fyl2x fyl2xp1
""".split())
SUFFIXES = ("ll", "b", "w", "l", "s", "t")
VECTOR_RE = re.compile(r"%(?:[xyz]mm\d+|mm[0-7])\b")
LINE_RE = re.compile(r"^\s*([0-9a-f]+):\t[0-9a-f ]+\t\s*([a-z][a-z0-9.]*)\s*(.*)$")
FUNC_RE = re.compile(r"^[0-9a-f]+ <(.*)>:$")


def allowed(mnemonic):
    if mnemonic in I386:
        return True
    for suffix in SUFFIXES:
        if mnemonic.endswith(suffix) and mnemonic[:-len(suffix)] in I386:
            return True
    return False


def scan(objdump, path):
    """{finding: (count, first 'addr <func>')}."""
    out = subprocess.run([objdump, "-d", "-w", path], capture_output=True, text=True,
                         errors="replace", timeout=120)
    if out.returncode != 0:
        raise RuntimeError(f"{path}: objdump rc={out.returncode}: {out.stderr.strip()}")
    findings, func, lines = {}, "?", 0
    for line in out.stdout.splitlines():
        f = FUNC_RE.match(line)
        if f:
            func = f.group(1)
            continue
        m = LINE_RE.match(line)
        if not m:
            continue
        lines += 1
        addr, mnem, ops = m.groups()
        while mnem in PREFIXES and ops:
            parts = ops.split(None, 1)
            mnem, ops = parts[0], parts[1] if len(parts) > 1 else ""
        bad = []
        if mnem in PREFIXES:
            pass  # bare prefix byte (e.g. data16 before a 386 instruction)
        elif not allowed(mnem):
            bad.append(mnem)
        elif mnem.startswith("nop") and ops.strip():
            bad.append(mnem + " <operand> (0F 1F multi-byte NOP)")
        if VECTOR_RE.search(ops):
            bad.append("vector register " + VECTOR_RE.search(ops).group(0))
        for b in bad:
            count, first = findings.get(b, (0, f"0x{addr} <{func}>"))
            findings[b] = (count + 1, first)
    if lines == 0:
        raise RuntimeError(f"{path}: no instructions disassembled")
    return findings, lines


def main(argv):
    paths = [a for a in argv if not a.startswith("-")]
    if not paths:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    objdump = objdump_bin()
    if not objdump:
        print("Error: i386-elf-objdump が見つからない (CROSS_DIR を確認)", file=sys.stderr)
        return 2
    bad = 0
    for path in paths:
        if not os.path.isfile(path):
            print(f"[ISA386] {path}: 無い (先にビルド)", file=sys.stderr)
            return 2
        findings, lines = scan(objdump, path)
        if not findings:
            print(f"[ISA386] OK {path}: {lines} instructions, 386/387 only")
            continue
        bad += 1
        for name, (count, first) in sorted(findings.items()):
            print(f"[ISA386] NG {path}: {name} x{count} (first {first})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
