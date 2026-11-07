/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1996, 1998 Bill Studenmund
 * Copyright (c) 1995 Gordon W. Ross
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Independent expected calls, transcribed from NetBSD 36eddebc:
 * NB Locore.c:218-230,654-693,703-783; NL loadfile_machdep.c:179-199,214-216;
 * NZ zs.c:789-798,905-923 (its copyright/permission is in flow_fake.c).
 * Rebuild: read those BSD sources, transcribe CI order/cells and RR operations,
 * then compare this list to the sources; the running tests never read them.
 * Device values below reproduce REVIEW.r1/QEMU mac99: BAR 0x80000000,
 * ch-a offset 0x13020, tuple size 1; regenerate by querying the OF properties.
 * RR0 0x44, RR1 7 reproduce that observation, not product mask constants.
 * Firmware handles are synthetic unique integers; rebuild by keeping references
 * within this fixture consistent. These are a script, not an OF emulator. */
#ifndef OS32_FLOW_FIXTURE_H
#define OS32_FLOW_FIXTURE_H
#define FX_CSR 0x80013020U
#define FX_DATA 0x80013030U
#define FX_LIMIT 1000000U
static const char fx_of[] = "OF: os32 ppc-boot\r\n";
static const char fx_escc[] = "ESCC-A: os32 ppc-boot\r\n";
enum flow_case {
    NORMAL, AC2, REAL, SCREEN, BAD_STDIN, BAD_STDOUT, BAD_CHOSEN,
    BAD_ROOT, BAD_SC, BAD_MEMORY, BAD_MMU, BAD_AC, MAP_CI, MAP_CATCH,
    OPEN_ZERO, OPEN_CI, TREE_EMPTY, BAD_REG, BAD_BAR, HIGH_BAR,
    WRAP_ADDR, BAD_PATH, LEGACY_PARENT, TX_TIMEOUT, DRAIN_TIMEOUT,
    DELAY_READY, OPTIONS_ABSENT, SCREEN_FAIL, CASE_COUNT
};
static const char *const case_names[] = {
    "normal", "ac2", "real", "screen", "bad-stdin", "bad-stdout", "bad-chosen",
    "bad-root", "bad-sc", "bad-memory", "bad-mmu", "bad-ac", "map-ci", "map-catch",
    "open-zero", "open-ci", "tree-empty", "bad-reg", "bad-bar", "high-bar",
    "wrap-addr", "bad-path", "legacy-parent", "tx-timeout", "drain-timeout",
    "delay-ready", "options-absent", "screen-fail"
};
/* CI expectations: all input cells are checked; pointer inputs additionally
 * check pointed-to strings/content. Output buffer cells are checked by cap. */
struct flow_step {
    const char *name, *text;
    unsigned ni, no;
    cell arg[8];
    int rc, result, payload_kind;
    const void *payload;
    unsigned payload_len;
};
#endif
