/*
 * Default DAC, xorg-server vgaHW.c:87-156 (MIT/X11).
 * Copyright 1991-1999 by The XFree86 Project, Inc.
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * vgaHW.c license: xorg-server-21.1.13 COPYING:54-76
 * Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.
 * Copyright (C) Colin Harrison 2005-2008
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 * Except as contained in this notice, the name of the XFree86 Project shall not
 * be used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from the XFree86 Project.
 */
#include "trident.h"

const u8 trident_default_dac[TRI_DAC_BYTES] = {
    0, 0, 0, 0, 0, 42, 0, 42, 0, 0, 42, 42,
    42, 0, 0, 42, 0, 42, 42, 21, 0, 42, 42, 42,
    21, 21, 21, 21, 21, 63, 21, 63, 21, 21, 63, 63,
    63, 21, 21, 63, 21, 63, 63, 63, 21, 63, 63, 63,
    0, 0, 0, 5, 5, 5, 8, 8, 8, 11, 11, 11,
    14, 14, 14, 17, 17, 17, 20, 20, 20, 24, 24, 24,
    28, 28, 28, 32, 32, 32, 36, 36, 36, 40, 40, 40,
    45, 45, 45, 50, 50, 50, 56, 56, 56, 63, 63, 63,
    0, 0, 63, 16, 0, 63, 31, 0, 63, 47, 0, 63,
    63, 0, 63, 63, 0, 47, 63, 0, 31, 63, 0, 16,
    63, 0, 0, 63, 16, 0, 63, 31, 0, 63, 47, 0,
    63, 63, 0, 47, 63, 0, 31, 63, 0, 16, 63, 0,
    0, 63, 0, 0, 63, 16, 0, 63, 31, 0, 63, 47,
    0, 63, 63, 0, 47, 63, 0, 31, 63, 0, 16, 63,
    31, 31, 63, 39, 31, 63, 47, 31, 63, 55, 31, 63,
    63, 31, 63, 63, 31, 55, 63, 31, 47, 63, 31, 39,
    63, 31, 31, 63, 39, 31, 63, 47, 31, 63, 55, 31,
    63, 63, 31, 55, 63, 31, 47, 63, 31, 39, 63, 31,
    31, 63, 31, 31, 63, 39, 31, 63, 47, 31, 63, 55,
    31, 63, 63, 31, 55, 63, 31, 47, 63, 31, 39, 63,
    45, 45, 63, 49, 45, 63, 54, 45, 63, 58, 45, 63,
    63, 45, 63, 63, 45, 58, 63, 45, 54, 63, 45, 49,
    63, 45, 45, 63, 49, 45, 63, 54, 45, 63, 58, 45,
    63, 63, 45, 58, 63, 45, 54, 63, 45, 49, 63, 45,
    45, 63, 45, 45, 63, 49, 45, 63, 54, 45, 63, 58,
    45, 63, 63, 45, 58, 63, 45, 54, 63, 45, 49, 63,
    0, 0, 28, 7, 0, 28, 14, 0, 28, 21, 0, 28,
    28, 0, 28, 28, 0, 21, 28, 0, 14, 28, 0, 7,
    28, 0, 0, 28, 7, 0, 28, 14, 0, 28, 21, 0,
    28, 28, 0, 21, 28, 0, 14, 28, 0, 7, 28, 0,
    0, 28, 0, 0, 28, 7, 0, 28, 14, 0, 28, 21,
    0, 28, 28, 0, 21, 28, 0, 14, 28, 0, 7, 28,
    14, 14, 28, 17, 14, 28, 21, 14, 28, 24, 14, 28,
    28, 14, 28, 28, 14, 24, 28, 14, 21, 28, 14, 17,
    28, 14, 14, 28, 17, 14, 28, 21, 14, 28, 24, 14,
    28, 28, 14, 24, 28, 14, 21, 28, 14, 17, 28, 14,
    14, 28, 14, 14, 28, 17, 14, 28, 21, 14, 28, 24,
    14, 28, 28, 14, 24, 28, 14, 21, 28, 14, 17, 28,
    20, 20, 28, 22, 20, 28, 24, 20, 28, 26, 20, 28,
    28, 20, 28, 28, 20, 26, 28, 20, 24, 28, 20, 22,
    28, 20, 20, 28, 22, 20, 28, 24, 20, 28, 26, 20,
    28, 28, 20, 26, 28, 20, 24, 28, 20, 22, 28, 20,
    20, 28, 20, 20, 28, 22, 20, 28, 24, 20, 28, 26,
    20, 28, 28, 20, 26, 28, 20, 24, 28, 20, 22, 28,
    0, 0, 16, 4, 0, 16, 8, 0, 16, 12, 0, 16,
    16, 0, 16, 16, 0, 12, 16, 0, 8, 16, 0, 4,
    16, 0, 0, 16, 4, 0, 16, 8, 0, 16, 12, 0,
    16, 16, 0, 12, 16, 0, 8, 16, 0, 4, 16, 0,
    0, 16, 0, 0, 16, 4, 0, 16, 8, 0, 16, 12,
    0, 16, 16, 0, 12, 16, 0, 8, 16, 0, 4, 16,
    8, 8, 16, 10, 8, 16, 12, 8, 16, 14, 8, 16,
    16, 8, 16, 16, 8, 14, 16, 8, 12, 16, 8, 10,
    16, 8, 8, 16, 10, 8, 16, 12, 8, 16, 14, 8,
    16, 16, 8, 14, 16, 8, 12, 16, 8, 10, 16, 8,
    8, 16, 8, 8, 16, 10, 8, 16, 12, 8, 16, 14,
    8, 16, 16, 8, 14, 16, 8, 12, 16, 8, 10, 16,
    11, 11, 16, 12, 11, 16, 13, 11, 16, 15, 11, 16,
    16, 11, 16, 16, 11, 15, 16, 11, 13, 16, 11, 12,
    16, 11, 11, 16, 12, 11, 16, 13, 11, 16, 15, 11,
    16, 16, 11, 15, 16, 11, 13, 16, 11, 12, 16, 11,
    11, 16, 11, 11, 16, 12, 11, 16, 13, 11, 16, 15,
    11, 16, 16, 11, 15, 16, 11, 13, 16, 11, 12, 16,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
