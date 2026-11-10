/* PC-98 text VRAM addresses for the PC-98 console/IME/emergency code only.
 * (第 5 段 C1: include/tvram.h から移した。共通側は番地を知らない。) */
#ifndef OS32_PC98_TVRAM_PRIVATE_H
#define OS32_PC98_TVRAM_PRIVATE_H

#include "pc98.h"

#define TVRAM_BASE    TVRAM_CHAR_BASE
#define TVRAM_ATTR    TVRAM_ATTR_BASE

#endif
