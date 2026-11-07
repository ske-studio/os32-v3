/*
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
/* NetBSD ofwreal.S:215-245; aligned length and extra final line reserved. */
#include "ppc_oea.h"
.text
.globl ppc_cache_copy
ppc_cache_copy:
    li 6,PPC_CACHE_LINE
1:
    lwz 7,0(4)
    lwz 8,4(4)
    lwz 9,8(4)
    lwz 10,12(4)
    stw 7,0(3)
    stw 8,4(3)
    stw 9,8(3)
    stw 10,12(3)
    lwz 7,16(4)
    lwz 8,20(4)
    lwz 9,24(4)
    lwz 10,28(4)
    stw 7,16(3)
    stw 8,20(3)
    stw 9,24(3)
    stw 10,28(3)
    dcbst 0,3
    icbi 0,3
    add 3,3,6
    add 4,4,6
    subf. 5,6,5
    bgt 1b
    dcbst 0,3
    icbi 0,3
    sync
    isync
    blr
.section .note.GNU-stack,"",@progbits
