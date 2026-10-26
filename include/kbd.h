/* Common key values; no device or driver declarations. */
#ifndef OS32_KBD_H
#define OS32_KBD_H

/* スキャンコードのビット構成 */
#define SCANCODE_BREAK  0x80    /* ビット7: 1=ブレイク(離した), 0=メイク(押した) */
#define SCANCODE_KEY    0x7F    /* ビット6-0: キーコード */

/* ======== 特殊キーコード (PC9800Bible 表2-13) ======== */
#define KEY_ESC     0x00
#define KEY_BS      0x0E
#define KEY_TAB     0x0F
#define KEY_RETURN  0x1C
#define KEY_SPACE   0x34
#define KEY_XFER    0x35    /* 変換 */
#define KEY_ROLLUP  0x36
#define KEY_ROLLDOWN 0x37
#define KEY_INS     0x38
#define KEY_DEL     0x39
#define KEY_UP      0x3A
#define KEY_LEFT    0x3B
#define KEY_RIGHT   0x3C
#define KEY_DOWN    0x3D
#define KEY_HOME    0x3E    /* HOME/CLR */
#define KEY_HELP    0x3F

/* テンキー (0x40-0x50) */

/* NFER/VFキー */
#define KEY_NFER    0x51    /* 無変換 */
#define KEY_VF1     0x52
#define KEY_VF2     0x53
#define KEY_VF3     0x54
#define KEY_VF4     0x55
#define KEY_VF5     0x56

/* STOP/COPY */
#define KEY_STOP    0x60
#define KEY_COPY    0x61

/* ファンクションキー */
#define KEY_F1      0x62
#define KEY_F2      0x63
#define KEY_F3      0x64
#define KEY_F4      0x65
#define KEY_F5      0x66
#define KEY_F6      0x67
#define KEY_F7      0x68
#define KEY_F8      0x69
#define KEY_F9      0x6A
#define KEY_F10     0x6B

/* シフトキー */
#define KEY_SHIFT   0x70
#define KEY_CAPS    0x71
#define KEY_KANA    0x72
#define KEY_GRPH    0x73
#define KEY_CTRL    0x74

#define KBD_RAW_MAKE          0x100
#define KBD_RAW_MOD_SHIFT     9
#define KBD_RAW_MOD_MASK      0x7F
#define KBD_RAW_KEY_MAKE_MASK 0x1FF

#define SHIFT_SHIFT  0x01
#define SHIFT_CAPS   0x02
#define SHIFT_KANA   0x04
#define SHIFT_GRPH   0x08
#define SHIFT_CTRL   0x10

#endif /* OS32_KBD_H */
