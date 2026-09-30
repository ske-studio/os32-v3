/* NOP.C — 何もしないで戻るだけのテストプログラム */
typedef unsigned long u32;
typedef unsigned char u8;
typedef struct {
    u32 magic;
    u32 version;
} KernelAPI;

void main(int argc, char **argv, KernelAPI *api)
{
    /* 最小テスト: VRAMに直接1文字書いて戻る */
    volatile unsigned short *tvram = (volatile unsigned short *)0xA0000;
    volatile unsigned short *avram = (volatile unsigned short *)0xA2000;
    /* 画面右上にOKマーク */
    tvram[78] = 'O'; avram[78] = 0x00E1;
    tvram[79] = 'K'; avram[79] = 0x00E1;
    (void)api;
}
