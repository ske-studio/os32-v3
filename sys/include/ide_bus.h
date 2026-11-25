#ifndef IDE_BUS_H
#define IDE_BUS_H

#include "types.h"

#define IDE_REG_ERROR 1  /* W は FEATURES */
#define IDE_REG_SECT_CNT 2
#define IDE_REG_SECT_NUM 3
#define IDE_REG_CYL_LO 4
#define IDE_REG_CYL_HI 5
#define IDE_REG_DRV_HEAD 6
#define IDE_REG_STATUS 7  /* W は COMMAND */
#define IDE_BUS_OPEN8 0xFF   /* 無いバンクを読んだ値 */
static inline void ide_bus_init(void);            /* 装置の IRQ を止める・窓の用意。失敗しない */
static inline void ide_bus_select(int bank);      /* 以後の読み書きの相手 (0/1)。無いバンクも選べる */
static inline u8   ide_bus_in(int reg);           /* taskfile 1〜7 の 8 bit の読み。無いバンクは IDE_BUS_OPEN8 */
static inline void ide_bus_out(int reg, u8 v);    /* 同 書き。無いバンクへは何もしない */
static inline u8   ide_bus_alt(void);             /* Alt Status。無いバンクは IDE_BUS_OPEN8 */
static inline void ide_bus_ctl(u8 v);             /* Device Control */
static inline u16  ide_bus_data_word(void);       /* Data を 1 語、ATA の語の値で (IDENTIFY・読み捨て) */
static inline void ide_bus_data_read(void *dst, u32 words);        /* Data → メモリ。媒体のバイト順のまま */
static inline void ide_bus_data_write(const void *src, u32 words); /* メモリ → Data。同上 */
#include "platform_ide_bus.h"

#endif
