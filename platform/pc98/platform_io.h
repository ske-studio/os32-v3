/* ======================================================================== */
/*  platform/pc98/platform_io.h — ポート I/O の PC-98 実装                   */
/*                                                                          */
/*  契約は include/io.h にある。ここは PC-9801/9821 での実現方法だけを書く。  */
/*  このファイルを直接 #include してはいけない — 常に "io.h" を引くこと。     */
/*  ビルドは -Iplatform/$(PLATFORM) で解決する (build/config.mk)。           */
/*                                                                          */
/*  ここが arch/ ではなく platform/ にある理由: 独立した I/O 空間と          */
/*  `in` / `out` 命令は x86 と PC-98 の**機器のつなぎ方**に属する概念で、     */
/*  ARM のように I/O 空間を持たない CPU には対応物が無い。移植先では         */
/*  同じ関数名が memory-mapped I/O (レジスタ番地への読み書き) になる。       */
/* ======================================================================== */

#ifndef PLATFORM_IO_H
#define PLATFORM_IO_H

/* ---- I/Oポート操作 (8-bit) ---- */
static inline unsigned int inp(unsigned int port) {
    unsigned char ret;
    __asm__ volatile("inb %w1, %b0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outp(unsigned int port, unsigned int value) {
    __asm__ volatile("outb %b0, %w1" : : "a"((unsigned char)value), "Nd"(port));
}

/* ---- I/Oポート操作 (16-bit) ---- */
static inline unsigned int inpw(unsigned int port) {
    unsigned short ret;
    __asm__ volatile("inw %w1, %w0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outpw(unsigned int port, unsigned int value) {
    __asm__ volatile("outw %w0, %w1" : : "a"((unsigned short)value), "Nd"(port));
}

/* ---- I/Oポート操作 (32-bit) ---- */
/* PCI コンフィギュレーション (0CF8h) が **DWORD アクセス必須**
 * (io_pci.md 456 行: 0CF8h〜0CFBh へのバイト/ワードアクセスは通常の
 * I/O アクセスとして扱われ、PCI ではなくチップセットの別レジスタに当たる)。
 * `inl` / `outl` は 386 以降の命令で、OS32 の最低要件 (i386) を満たす。
 * u32 は `unsigned long` なので引数と戻り値もそちらに合わせる。 */
static inline unsigned long inpd(unsigned int port) {
    unsigned int ret;
    __asm__ volatile("inl %w1, %0" : "=a"(ret) : "Nd"(port));
    return (unsigned long)ret;
}

static inline void outpd(unsigned int port, unsigned long value) {
    __asm__ volatile("outl %0, %w1" : : "a"((unsigned int)value), "Nd"(port));
}

/* ---- I/Oポート操作 (REP INSW: バッファ読み込み) ---- */
static inline void insw_rep(unsigned int port, void *buf, unsigned int count) {
    __asm__ volatile("rep insw"
                     : "+D"(buf), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/* ---- PC-98 I/Oウェイト (ポート0x5Fダミーアクセス, 約0.6µs) ---- */
/* PC9800Bible §4-4 準拠。                                          */
static inline void io_wait(void) {
    outp(0x5F, 0);
}

/* n 回ウェイト (約 0.6µs × n)。 */
static inline void io_wait_n(int n) {
    while (n-- > 0) io_wait();
}

#endif /* PLATFORM_IO_H */
