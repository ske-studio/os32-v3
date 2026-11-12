/*-
 * Copyright (C) 2003 Matt Thomas
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
/* Subsystem constants shared by C, assembler and the generated linker script.
 * PPC_BOOT_DESIGN §2/3; BAT values: NetBSD Locore.c:108-113.
 * No public ABI constant is introduced by this independent boot stage. */
#ifndef OS32_PPC_LAYOUT_H
#define OS32_PPC_LAYOUT_H
#define PM_LINK_BASE 0x20000
#define PM_IMAGE_LIMIT 0x10000
#define PM_ELF_BUDGET 0x6000
#define PM_STACK_SIZE 8192
#define PM_STACK_ALIGN 16
#define PM_ABI_FRAME 32
#define PM_CELL_BYTES 4
#define PM_BAT_RAM_LOWER 0x12
#define PM_BAT_RAM_UPPER 0x1ffe
#define PM_MSR_EE 0x8000
#define PM_ESCC_WINDOW 0x20
#define PM_ESCC_STRIDE 0x10
/* NetBSD oea/pte.h:96-107: PTE_I | PTE_G | PTE_RW. */
#define PM_MAP_MODE_IO (0x20 | 0x08 | 0x02)
#define PM_POLL_LIMIT 1000000
#define PM_RR0_TX_READY 0x04
#define PM_RR1_ALL_SENT 0x01
#define PM_RR1_INDEX 1
#define PM_PROP_CAP 4096
#define PM_PATH_CAP 1024
#define PM_OPTION_CAP 32
#define PM_FAIL_CAP 64
#define PM_CI_CAP 16
#define PM_METHOD_MAX_INPUTS 6
#define PM_NOTE_NAMESZ 8
#define PM_NOTE_DESCSZ 20
#define PM_NOTE_TYPE 0x1275
/* Independent MMU test image budgets and reserved address windows. */
#define PM_MMU_IMAGE_LIMIT 0x20000
#define PM_MMU_ELF_BUDGET 0x18000
#define PM_LOW_RESERVED_END 0x3000
#define PM_BAT_RAM_END 0x10000000
#define PM_IO_ALIAS 0xf0000000
#define PM_TEST_ALIAS 0x40000000
#define PM_MAPPING_COUNT 16
#define PM_REGION_COUNT 35
#define PM_MEMORY_REGIONS 32
#define PM_PTEG_INITIAL 0x1000
#define PM_DIAG_DEC_HZ 100
#define PM_DEC_DUMP_N 2
#define PM_MEMORY "/memory"
#define PM_ROOT "/"
#define PM_CHOSEN "/chosen"
#define PM_OPTIONS "/options"
#define PM_OPENPROM "/openprom"
#define PM_SCREEN "screen"
#endif
