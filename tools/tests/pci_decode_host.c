/* ======================================================================== */
/*  PCI_DECODE_HOST.C — drivers/pci_decode.c をそのままホストで回す         */
/*                                                                          */
/*  実物の復号を 1 行も写さずに #include する。pci_decode.c は I/O も        */
/*  静的配列も構造体の padding も触らないので、模型は 1 つも要らない。       */
/*                                                                          */
/*  なぜホストで試すのか: **NP21/W は PCI を実装していない** (`0CF8h` が     */
/*  無い)。走らせて確かめられるのは実機 PC-9821Ra266 だけで、実機の 1 回は   */
/*  高い (シリアル 115200 で会話する)。ビットの読み違いはここで全部潰す。    */
/*                                                                          */
/*  期待値の出どころ (記録: tools/tests/pci_decode_tdd.md):                 */
/*    - `docs/hw/undocumented/io_pci.md` 図2 (76〜109 行) と                */
/*      0CF8h のビット定義 (446〜462 行)                                    */
/*    - Intel 8255x SDM テキスト版 Table 1 (697〜713 行) の Type 0 ヘッダ    */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/pci_decode.c"
/* `lspci -v` の行づくり (userland/shell/pci_verbose.c) も実物をそのまま。 */
#include "../../userland/shell/pci_verbose.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* ------------------------------------------------------------------ */
/*  (1) アドレス語の組み立て (io_pci.md 図2 / 446〜456 行)              */
/* ------------------------------------------------------------------ */
static void cfg_addr(void)
{
    /* bit31 (CONE) は常に立つ。立てないとメカニズム #1 にならず、
     * 0CF8h はマーキュリー互換の別レジスタとして振る舞う (458 行)。 */
    CHECK((pci_cfg_addr(0, 0, 0, 0) & PCI_CFG_ADDR_ENABLE) != 0);
    CHECK(pci_cfg_addr(0, 0, 0, 0) == 0x80000000UL);

    /* 欄の位置: bus bit23〜16 / dev bit15〜11 / fn bit10〜8 / reg bit7〜2。 */
    CHECK(pci_cfg_addr(0, 1, 0, 0x3C) == 0x8000083CUL);   /* C バスブリッヂ */
    CHECK(pci_cfg_addr(0, 8, 0, 0x10) == 0x80004010UL);   /* PCI スロット #0 */
    CHECK(pci_cfg_addr(1, 0, 0, 0x00) == 0x80010000UL);   /* bus 1 */
    CHECK(pci_cfg_addr(0, 31, 7, 0xFC) == 0x8000FFFCUL);  /* 右端 */

    /* reg の bit1〜0 は落ちる。PCI アドレスの下位 2 ビットは 00b 固定
     * (図2 の末尾)。0x3D (Interrupt Pin) を渡しても 0x3C の DWORD を指す。 */
    CHECK(pci_cfg_addr(0, 0, 0, 0x3D) == 0x8000003CUL);
    CHECK(pci_cfg_addr(0, 0, 0, 0x3F) == 0x8000003CUL);
    CHECK(pci_cfg_addr(0, 0, 0, 0x02) == 0x80000000UL);

    /* 範囲外は**隣の欄を侵さない**。dev は 5 ビットなので 32 は 0 に、
     * fn は 3 ビットなので 8 は 0 に落ちる。マスクを忘れると
     * dev=32 が bus 欄へ、fn=8 が dev 欄へ溢れて**別のデバイスの
     * config を読む** — 列挙の上限を書き間違えた日に静かに壊れる。 */
    CHECK(pci_cfg_addr(0, 32, 0, 0) == 0x80000000UL);
    CHECK(pci_cfg_addr(0, 0, 8, 0) == 0x80000000UL);
    CHECK(pci_cfg_addr(256, 0, 0, 0) == 0x80000000UL);
    /* 溢れが予約ビット (bit30〜24) を汚さないことも見る。 */
    CHECK((pci_cfg_addr(256, 32, 8, 0xFF) & 0x7F000000UL) == 0);
}

/* ------------------------------------------------------------------ */
/*  (2) PCI 有無の判定に使う値                                         */
/* ------------------------------------------------------------------ */
static void probe_values(void)
{
    /* 読み戻しで確かめる以上、**本物のメカニズム #1 が保持できる値**
     * でなければならない。bit1〜0 は 0 に落ちる (図2) ので、そこを
     * 立てた値で探ると PCI があっても「無い」と答える。 */
    CHECK((PCI_MECH1_PROBE_A & 0x3UL) == 0);
    CHECK((PCI_MECH1_PROBE_B & 0x3UL) == 0);
    /* 予約 bit30〜24 も 0 でなければ読み戻しが一致しない。 */
    CHECK((PCI_MECH1_PROBE_A & 0x7F000000UL) == 0);
    CHECK((PCI_MECH1_PROBE_B & 0x7F000000UL) == 0);
    /* どちらも CONE が立っている。 */
    CHECK((PCI_MECH1_PROBE_A & PCI_CFG_ADDR_ENABLE) != 0);
    CHECK((PCI_MECH1_PROBE_B & PCI_CFG_ADDR_ENABLE) != 0);
    /* 2 本が**違う値**であること。同じ値を 2 回書いても区別は増えない。 */
    CHECK(PCI_MECH1_PROBE_A != PCI_MECH1_PROBE_B);
    /* B は書き込める全ビットを使う = 素通しのラッチと本物を見分ける力。
     * A だけだと bit31 しか動かさないので、0CF8h が単なる 32 ビット
     * ラッチの機械 (マーキュリー互換領域) を PCI と誤認し得る。 */
    CHECK(PCI_MECH1_PROBE_B == pci_cfg_addr(0xFF, 31, 7, 0xFC));
}

/* ------------------------------------------------------------------ */
/*  (3) BAR の種別                                                     */
/* ------------------------------------------------------------------ */
static void bar_kind(void)
{
    /* 生値 0 = そのレジスタは実装されていない。 */
    CHECK(pci_bar_kind(0x00000000UL) == PCI_BAR_NONE);

    /* bit0 = 1 は I/O 空間。 */
    CHECK(pci_bar_kind(0x0000E801UL) == PCI_BAR_IO);
    CHECK(pci_bar_kind(0xFFFFFFFFUL) == PCI_BAR_IO);

    /* **番地未割り当ての I/O BAR (生値 1) は NONE ではない。**
     * 票 §5-2 R1 が実機で見たいのは「BIOS が I/O 窓を割り当てたか」で、
     * 0x00000001 は「I/O BAR はあるが番地が 0」= 自分で割り当てが要る。
     * ここを NONE に畳むと、実機の 1 回でその事実が消える。 */
    CHECK(pci_bar_kind(0x00000001UL) == PCI_BAR_IO);

    /* bit0 = 0 はメモリ。bit2〜1 が型。 */
    CHECK(pci_bar_kind(0xF8000000UL) == PCI_BAR_MEM32);  /* 00b */
    CHECK(pci_bar_kind(0x000F0002UL) == PCI_BAR_MEM1M);  /* 01b */
    CHECK(pci_bar_kind(0xF8000004UL) == PCI_BAR_MEM64);  /* 10b */
    CHECK(pci_bar_kind(0xF8000006UL) == PCI_BAR_MEMRSV); /* 11b */
    /* prefetchable (bit3) は型を変えない。 */
    CHECK(pci_bar_kind(0xF8000008UL) == PCI_BAR_MEM32);
    CHECK(pci_bar_kind(0xF800000CUL) == PCI_BAR_MEM64);
}

/* ------------------------------------------------------------------ */
/*  (4) BAR の番地と prefetchable                                      */
/* ------------------------------------------------------------------ */
static void bar_base(void)
{
    /* I/O は下位 2 ビットだけを落とす (~3)。**~0xF で落とすと
     * 0xE808 が 0xE800 になる** — 82557 の I/O 窓は 32 バイト境界に
     * 置かれ得るので、これは実機で外す形の間違い。 */
    CHECK(pci_bar_base(0x0000E801UL) == 0x0000E800UL);
    CHECK(pci_bar_base(0x0000E809UL) == 0x0000E808UL);
    CHECK(pci_bar_base(0x00000001UL) == 0x00000000UL);

    /* メモリは下位 4 ビットを落とす (~0xF)。型と prefetch が混ざる。 */
    CHECK(pci_bar_base(0xF8000000UL) == 0xF8000000UL);
    CHECK(pci_bar_base(0xF8000004UL) == 0xF8000000UL);
    CHECK(pci_bar_base(0xF800000CUL) == 0xF8000000UL);
    CHECK(pci_bar_base(0x000F0002UL) == 0x000F0000UL);

    /* 生値 0 は 0。 */
    CHECK(pci_bar_base(0x00000000UL) == 0);

    /* prefetchable は**メモリ BAR のときだけ** bit3。I/O では bit3 は
     * 番地の一部なので、無条件に bit3 を見ると 0xE808 が
     * 「prefetchable な I/O」という存在しないものに化ける。 */
    CHECK(pci_bar_prefetchable(0xF8000008UL) == 1);
    CHECK(pci_bar_prefetchable(0xF800000CUL) == 1);
    CHECK(pci_bar_prefetchable(0xF8000000UL) == 0);
    CHECK(pci_bar_prefetchable(0x0000E809UL) == 0);
    CHECK(pci_bar_prefetchable(0x0000E80DUL) == 0);
    CHECK(pci_bar_prefetchable(0x00000000UL) == 0);
}

/* ------------------------------------------------------------------ */
/*  (5) Header Type                                                    */
/* ------------------------------------------------------------------ */
static void header_type(void)
{
    /* bit7 = マルチファンクション。fn0 だけを見て 1〜7 を飛ばす判定に使う。 */
    CHECK(pci_is_multifunction(0x00) == 0);
    CHECK(pci_is_multifunction(0x01) == 0);
    CHECK(pci_is_multifunction(0x80) == 1);
    CHECK(pci_is_multifunction(0x81) == 1);

    /* bit6〜0 = レイアウト。**bit7 を落とさずに比較すると、
     * マルチファンクションのブリッヂ (0x81) が Type 0 扱いになり、
     * secondary bus を読まずに配下のバスを丸ごと見落とす。** */
    CHECK(pci_header_layout(0x00) == PCI_HDR_LAYOUT_DEVICE);
    CHECK(pci_header_layout(0x80) == PCI_HDR_LAYOUT_DEVICE);
    CHECK(pci_header_layout(0x01) == PCI_HDR_LAYOUT_BRIDGE);
    CHECK(pci_header_layout(0x81) == PCI_HDR_LAYOUT_BRIDGE);
    CHECK(pci_header_layout(0x82) == PCI_HDR_LAYOUT_CARDBUS);
    /* 不在の読み (0xFF) がブリッヂに見えないこと — 0xFF & 0x7F = 0x7F。 */
    CHECK(pci_header_layout(0xFF) != PCI_HDR_LAYOUT_BRIDGE);
}

/* ------------------------------------------------------------------ */
/*  (6) DWORD からの切り出し                                           */
/* ------------------------------------------------------------------ */
static void extract(void)
{
    /* 0CF8h は DWORD 必須 (io_pci.md 456 行) なので、8/16 ビットの
     * 読みも DWORD 1 回から切り出す。レーンは reg の下位 2 ビット。 */
    u32 d = 0x12345678UL;
    CHECK(pci_extract8(d, 0x00) == 0x78);
    CHECK(pci_extract8(d, 0x01) == 0x56);
    CHECK(pci_extract8(d, 0x02) == 0x34);
    CHECK(pci_extract8(d, 0x03) == 0x12);
    /* オフセットがそろっていなくても同じレーンを指す (0x3D → bit15〜8)。 */
    CHECK(pci_extract8(d, 0x3D) == 0x56);
    CHECK(pci_extract8(d, 0x3C) == 0x78);
    CHECK(pci_extract8(d, 0x0B) == 0x12);

    CHECK(pci_extract16(d, 0x00) == 0x5678);
    CHECK(pci_extract16(d, 0x02) == 0x1234);
    /* u16 の欄は PCI では必ず偶数オフセットに置かれ、**バイト境界を
     * またがない**。奇数を渡されても 16 ビットのレーンへ丸める
     * (bit ずれた値を返すより、隣の欄を返す方が表示で気づける)。 */
    CHECK(pci_extract16(d, 0x01) == 0x5678);
    CHECK(pci_extract16(d, 0x03) == 0x1234);
    /* Vendor ID (0x00) と Device ID (0x02) が同じ DWORD から取れる。 */
    CHECK(pci_extract16(0x12298086UL, PCI_CFG_VENDOR_ID) == PCI_VENDOR_INTEL);
    CHECK(pci_extract16(0x12298086UL, PCI_CFG_DEVICE_ID) == PCI_DEVICE_82557);
}

/* ------------------------------------------------------------------ */
/*  (7) 名前引き                                                       */
/* ------------------------------------------------------------------ */
static void names(void)
{
    CHECK(!strcmp(pci_class_name(0x02, 0x00), "Network/Ethernet"));
    CHECK(!strcmp(pci_class_name(0x06, 0x00), "Bridge/Host"));
    CHECK(!strcmp(pci_class_name(0x06, 0x01), "Bridge/ISA"));
    CHECK(!strcmp(pci_class_name(0x06, 0x04), "Bridge/PCI"));
    CHECK(!strcmp(pci_class_name(0x03, 0x00), "Display/VGA"));
    CHECK(!strcmp(pci_class_name(0x01, 0x01), "Storage/IDE"));
    /* サブクラスが違っても親クラスは分かる。 */
    CHECK(!strcmp(pci_class_name(0x02, 0x80), "Network"));
    CHECK(!strcmp(pci_class_name(0x06, 0x80), "Bridge"));
    /* 知らない組み合わせは空欄にしない (欄がずれる)。 */
    CHECK(!strcmp(pci_class_name(0x7F, 0x00), "Unknown"));
    /* TV チューナー / キャプチャのクラス (lspci -v で未知のカードを見る)。 */
    CHECK(!strcmp(pci_class_name(0x04, 0x00), "Multimedia/Video"));
    CHECK(!strcmp(pci_class_name(0x04, 0x01), "Multimedia/Audio"));
    CHECK(!strcmp(pci_class_name(0x04, 0x80), "Multimedia"));
    CHECK(pci_class_name(0xFF, 0xFF)[0] != '\0');

    /* io_pci.md 271〜280 行 / 336〜345 行の表にあるベンダ。 */
    CHECK(!strcmp(pci_vendor_name(0x8086), "Intel"));
    CHECK(!strcmp(pci_vendor_name(0x1033), "NEC"));
    CHECK(!strcmp(pci_vendor_name(0x102B), "Matrox"));
    CHECK(!strcmp(pci_vendor_name(0x9004), "Adaptec"));
    CHECK(!strcmp(pci_vendor_name(0x1023), "Trident"));
    /* TV チューナーの候補 (カード自身 / 搭載チップ)。 */
    CHECK(!strcmp(pci_vendor_name(0x10FC), "I-O DATA"));
    CHECK(!strcmp(pci_vendor_name(0x14F1), "Conexant"));
    CHECK(!strcmp(pci_vendor_name(0x1131), "Philips"));
    CHECK(!strcmp(pci_vendor_name(0x109E), "Brooktree"));
    /* 知らないベンダは空文字 — 行には vendor:device の 16 進が既に出る。 */
    CHECK(pci_vendor_name(0x1234)[0] == '\0');
    /* 不在 (0xFFFF) に名前を付けない。 */
    CHECK(pci_vendor_name(PCI_VENDOR_NONE)[0] == '\0');
}

/* ------------------------------------------------------------------ */
/*  (8) 実機で見るはずの姿 — 82557 の config を丸ごと復号する           */
/*                                                                    */
/*  8255x SDM Table 1 (697〜713 行) の並び。BAR は 0x10 がメモリ、      */
/*  0x14 が I/O、0x18 がフラッシュ (票の例示とは順が逆 — **SDM が正**)。 */
/*  値は「BIOS が割り当て済み」の想定。実機で違ったらここを直す。       */
/* ------------------------------------------------------------------ */
static void story_82557(void)
{
    u32 cfg[16];
    u16 vendor, device, command;
    u8 cls, sub, progif, hdr, line, pin;
    int i;

    for (i = 0; i < 16; i++) cfg[i] = 0;
    cfg[0x00 / 4] = 0x12298086UL;   /* Device ID : Vendor ID */
    cfg[0x04 / 4] = 0x02800007UL;   /* Status : Command (IO+MEM+BusMaster) */
    cfg[0x08 / 4] = 0x02000001UL;   /* Class 200000h : Revision 01 */
    cfg[0x0C / 4] = 0x00002008UL;   /* BIST:Header:Latency:CacheLine */
    cfg[0x10 / 4] = 0xF8000000UL;   /* CSR memory mapped */
    cfg[0x14 / 4] = 0x0000E801UL;   /* CSR I/O mapped */
    cfg[0x18 / 4] = 0xF8100000UL;   /* Flash */
    cfg[0x3C / 4] = 0x2814010BUL;   /* MaxLat:MinGnt:IntPin(01h):IntLine(0Bh) */

    vendor = pci_extract16(cfg[PCI_CFG_VENDOR_ID / 4], PCI_CFG_VENDOR_ID);
    device = pci_extract16(cfg[PCI_CFG_DEVICE_ID / 4], PCI_CFG_DEVICE_ID);
    CHECK(vendor == PCI_VENDOR_INTEL);
    CHECK(device == PCI_DEVICE_82557);
    CHECK(!strcmp(pci_vendor_name(vendor), "Intel"));

    command = pci_extract16(cfg[PCI_CFG_COMMAND / 4], PCI_CFG_COMMAND);
    /* 票 §5-2 R2: I/O Enable と Bus Master が立っているか。 */
    CHECK((command & PCI_CMD_IO_ENABLE) != 0);
    CHECK((command & PCI_CMD_BUS_MASTER) != 0);

    progif = pci_extract8(cfg[PCI_CFG_PROG_IF / 4], PCI_CFG_PROG_IF);
    sub    = pci_extract8(cfg[PCI_CFG_SUBCLASS / 4], PCI_CFG_SUBCLASS);
    cls    = pci_extract8(cfg[PCI_CFG_CLASS / 4], PCI_CFG_CLASS);
    CHECK(cls == PCI_CLASS_NETWORK);
    CHECK(sub == PCI_SUB_NET_ETHERNET);
    CHECK(progif == 0x00);
    CHECK(!strcmp(pci_class_name(cls, sub), "Network/Ethernet"));

    hdr = pci_extract8(cfg[PCI_CFG_HEADER_TYPE / 4], PCI_CFG_HEADER_TYPE);
    CHECK(pci_is_multifunction(hdr) == 0);
    CHECK(pci_header_layout(hdr) == PCI_HDR_LAYOUT_DEVICE);

    /* BAR: 0x10 メモリ / 0x14 I/O / 0x18 メモリ、残りは未実装。 */
    CHECK(pci_bar_kind(cfg[0x10 / 4]) == PCI_BAR_MEM32);
    CHECK(pci_bar_base(cfg[0x10 / 4]) == 0xF8000000UL);
    CHECK(pci_bar_kind(cfg[0x14 / 4]) == PCI_BAR_IO);
    CHECK(pci_bar_base(cfg[0x14 / 4]) == 0x0000E800UL);
    CHECK(pci_bar_kind(cfg[0x18 / 4]) == PCI_BAR_MEM32);
    CHECK(pci_bar_kind(cfg[0x1C / 4]) == PCI_BAR_NONE);
    CHECK(pci_bar_kind(cfg[0x24 / 4]) == PCI_BAR_NONE);

    /* Interrupt Line / Pin (SDM 4.1.15 / 4.1.16)。PC-98 では PIRQ0〜3 が
     * C バスブリッヂ経由で 8259 に配られ、その結果がここに入る
     * (io_pci.md 315〜320 行)。82557 の Pin は常に 1 = INTA#。 */
    line = pci_extract8(cfg[PCI_CFG_INT_LINE / 4], PCI_CFG_INT_LINE);
    pin  = pci_extract8(cfg[PCI_CFG_INT_PIN / 4], PCI_CFG_INT_PIN);
    CHECK(line == 11);
    CHECK(pin == 1);

    /* この 1 行がアドレス語になって 0CF8h へ出る。 */
    CHECK(pci_cfg_addr(0, 11, 0, PCI_CFG_INT_LINE) == 0x8000583CUL);
}

/* ------------------------------------------------------------------ */
/*  (9) 不在の読み — PCI が居ない / そのスロットが空                    */
/* ------------------------------------------------------------------ */
static void absent(void)
{
    /* 応答の無いバスサイクルは全ビット 1。vendor が 0xFFFF なら飛ばす。 */
    CHECK(pci_extract16(0xFFFFFFFFUL, PCI_CFG_VENDOR_ID) == PCI_VENDOR_NONE);
    CHECK(pci_extract16(0xFFFFFFFFUL, PCI_CFG_DEVICE_ID) == PCI_VENDOR_NONE);
    /* 0x0000FFFF (vendor だけ 0xFFFF) も不在。**DWORD 全体を
     * 0xFFFFFFFF と比べる判定は、device 側にゴミが乗った機械で
     * 空スロットを拾う。** */
    CHECK(pci_extract16(0x0000FFFFUL, PCI_CFG_VENDOR_ID) == PCI_VENDOR_NONE);
    /* vendor 0 も実在しない (PCI 規格)。ただし列挙は 0xFFFF だけを見る
     * ので、ここでは「0xFFFF と 0 は別物」であることだけ確かめる。 */
    CHECK(pci_extract16(0x00000000UL, PCI_CFG_VENDOR_ID) != PCI_VENDOR_NONE);
}

/* ------------------------------------------------------------------ */
/*  (10) `lspci -v` — 偽の config 空間から出る行を丸ごと突き合わせる    */
/* ------------------------------------------------------------------ */

/* 全行を '\n' でつないで out へ (実機の画面と同じ順・同じ文字)。 */
static void render(const u32 *cfg, u32 bus, u32 dev, u32 fn,
                   char *out, size_t cap)
{
    char line[PCI_VERBOSE_LINE_MAX];
    int k, r;
    out[0] = '\0';
    for (k = 0; k < 64; k++) {
        r = pci_verbose_line(cfg, bus, dev, fn, k, line, (int)sizeof(line));
        if (r == PCI_VERBOSE_END) break;
        if (r == PCI_VERBOSE_SKIP) continue;
        /* 1 行は PCI_VERBOSE_LINE_MAX に収まり、切れていない
         * (切れていれば最後の欄が欠ける = 識別の材料が消える)。 */
        if (strlen(line) + 1 >= sizeof(line)) failed++;
        strncat(out, line, cap - strlen(out) - 1);
        strncat(out, "\n", cap - strlen(out) - 1);
    }
    if (k >= 64) failed++;   /* END が返らない */
}

static void expect_text(const char *got, const char *want, int line)
{
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL verbose:%d\n--- want\n%s--- got\n%s---\n",
                line, want, got);
        failed++;
    }
}

static void cfg_clear(u32 *cfg)
{
    int i;
    for (i = 0; i < PCI_VERBOSE_CFG_DWORDS; i++) cfg[i] = 0;
}

/* TV チューナーの想定 (CX23880 = 14F1:8800 を載せた I-O DATA のカード)。
 * **subsystem の値は仮** — 実機の GV-MVP/HX2 がどう名乗るかは未確認。
 * ここで見るのは欄の並びと復号で、値の真偽ではない。 */
static void verbose_tuner(void)
{
    u32 cfg[PCI_VERBOSE_CFG_DWORDS];
    char out[2048];

    cfg_clear(cfg);
    cfg[0x00 / 4] = 0x880014F1UL;   /* Device 8800 : Vendor 14F1 */
    cfg[0x04 / 4] = 0x02900006UL;   /* Status 0290 : Command 0006 (Mem+BM) */
    cfg[0x08 / 4] = 0x04000005UL;   /* Class 04.00.00 : Rev 05 */
    cfg[0x0C / 4] = 0x00802008UL;   /* Header 80 (multi-function, type 0) */
    cfg[0x10 / 4] = 0xF8000000UL;   /* BAR0 mem32 */
    cfg[0x2C / 4] = 0xD00310FCUL;   /* Subsystem D003 : SubVendor 10FC (仮) */
    cfg[0x3C / 4] = 0x2804010BUL;   /* Pin A : Line 11 */

    render(cfg, 0, 9, 0, out, sizeof(out));
    expect_text(out,
        "0:9.0 14f1:8800 Conexant\n"
        "  revision 05\n"
        "  class 04.00.00 Multimedia/Video\n"
        "  header 80 type 0 (device) multi-function\n"
        "  command 0006 I/O- Mem+ BusMaster+\n"
        "  status 0290\n"
        "  subsystem 10fc:d003\n"
        "  bar0 f8000000 mem32 base 0xf8000000\n"
        "  bar1 00000000 zero (unimplemented or unassigned)\n"
        "  bar2 00000000 zero (unimplemented or unassigned)\n"
        "  bar3 00000000 zero (unimplemented or unassigned)\n"
        "  bar4 00000000 zero (unimplemented or unassigned)\n"
        "  bar5 00000000 zero (unimplemented or unassigned)\n"
        "  interrupt line 11 pin A\n", __LINE__);

    /* 読み取り専用: 渡した config が 1 ビットも変わっていない。 */
    CHECK(cfg[0x10 / 4] == 0xF8000000UL);
    CHECK(cfg[0x2C / 4] == 0xD00310FCUL);
}

/* BAR の種類を全部: mem64 の下位・上位、prefetch、io、未割り当ての io、
 * 最後の欄の mem64 (上位の欄が無い)。 */
static void verbose_bars(void)
{
    u32 cfg[PCI_VERBOSE_CFG_DWORDS];
    char out[2048];

    cfg_clear(cfg);
    cfg[0x00 / 4] = 0x12298086UL;
    cfg[0x04 / 4] = 0x02800007UL;
    cfg[0x08 / 4] = 0x02000001UL;
    cfg[0x0C / 4] = 0x00000000UL;
    cfg[0x10 / 4] = 0xE000000CUL;   /* mem64 prefetch 下位 */
    cfg[0x14 / 4] = 0x00000000UL;   /* その上位 = 0 (none ではない) */
    cfg[0x18 / 4] = 0x0000E809UL;   /* io (bit3 は番地の一部) */
    cfg[0x1C / 4] = 0x00000001UL;   /* io、番地未割り当て */
    cfg[0x20 / 4] = 0x000F0002UL;   /* mem1m */
    cfg[0x24 / 4] = 0xF0000004UL;   /* 最後の欄の mem64 */
    cfg[0x2C / 4] = 0x00000000UL;
    cfg[0x3C / 4] = 0x000000FFUL;   /* Line 255 = 未割り当て、Pin 0 */

    render(cfg, 1, 31, 7, out, sizeof(out));
    expect_text(out,
        "1:31.7 8086:1229 Intel\n"
        "  revision 01\n"
        "  class 02.00.00 Network/Ethernet\n"
        "  header 00 type 0 (device) single-function\n"
        "  command 0007 I/O+ Mem+ BusMaster+\n"
        "  status 0280\n"
        "  subsystem 0000:0000\n"
        "  bar0 e000000c mem64-lo base 0xe0000000 prefetch\n"
        "  bar1 00000000 mem64-hi (bar0)\n"
        "  bar2 0000e809 io base 0x0000e808\n"
        "  bar3 00000001 io base 0x00000000 unassigned\n"
        "  bar4 000f0002 mem1m base 0x000f0000\n"
        "  bar5 f0000004 mem64-lo (no hi) base 0xf0000000\n"
        "  interrupt line 255 (unassigned) pin - (none)\n", __LINE__);

    /* 上位が 0 でなければ 4G 超と明示する (32 ビットの機械では届かない)。 */
    cfg[0x14 / 4] = 0x00000001UL;
    render(cfg, 1, 31, 7, out, sizeof(out));
    CHECK(strstr(out, "  bar1 00000001 mem64-hi (bar0 above 4G)\n") != 0);
}

/* Type 1 (ブリッヂ): BAR は 2 本だけ、Subsystem の代わりにバス番号。 */
static void verbose_bridge(void)
{
    u32 cfg[PCI_VERBOSE_CFG_DWORDS];
    char out[2048];

    cfg_clear(cfg);
    cfg[0x00 / 4] = 0x00011033UL;
    cfg[0x04 / 4] = 0x02200007UL;
    cfg[0x08 / 4] = 0x06040002UL;
    cfg[0x0C / 4] = 0x00810000UL;   /* Header 81 = multi-function bridge */
    cfg[0x10 / 4] = 0x00000000UL;
    cfg[0x14 / 4] = 0x00000000UL;
    cfg[0x18 / 4] = 0x40020100UL;   /* SecLat 40 : Sub 2 : Sec 1 : Pri 0 */
    cfg[0x1C / 4] = 0xE0E0F0F1UL;   /* BAR3 として読んではいけない */
    cfg[0x2C / 4] = 0x12345678UL;   /* Type 1 では Subsystem ではない */
    cfg[0x3C / 4] = 0x00000000UL;

    render(cfg, 0, 1, 0, out, sizeof(out));
    expect_text(out,
        "0:1.0 1033:0001 NEC\n"
        "  revision 02\n"
        "  class 06.04.00 Bridge/PCI\n"
        "  header 81 type 1 (pci bridge) multi-function\n"
        "  command 0007 I/O+ Mem+ BusMaster+\n"
        "  status 0220\n"
        "  bus primary 0 secondary 1 subordinate 2\n"
        "  bar0 00000000 zero (unimplemented or unassigned)\n"
        "  bar1 00000000 zero (unimplemented or unassigned)\n"
        "  interrupt line 0 pin - (none)\n", __LINE__);

    /* CardBus (Type 2) は BAR 欄も Subsystem も出さない。 */
    cfg[0x0C / 4] = 0x00020000UL;
    render(cfg, 0, 1, 0, out, sizeof(out));
    CHECK(strstr(out, "bar0") == 0);
    CHECK(strstr(out, "subsystem") == 0);
    CHECK(strstr(out, "type 2 (cardbus)") != 0);
}

/* 小さな buf でも溢れず、必ず NUL 終端される。 */
static void verbose_bounds(void)
{
    u32 cfg[PCI_VERBOSE_CFG_DWORDS];
    char small[8];
    int i;

    cfg_clear(cfg);
    cfg[0x00 / 4] = 0x880014F1UL;
    memset(small, 'Z', sizeof(small));
    CHECK(pci_verbose_line(cfg, 0, 9, 0, 0, small, 6) == PCI_VERBOSE_LINE);
    CHECK(!strcmp(small, "0:9.0"));
    CHECK(small[6] == 'Z' && small[7] == 'Z');   /* cap の外は触らない */
    CHECK(pci_verbose_line(cfg, 0, 9, 0, 0, small, 0) == PCI_VERBOSE_END);
    CHECK(pci_verbose_line(cfg, 0, 9, 0, -1, small, 8) == PCI_VERBOSE_END);
    /* 行の番号は有限 (呼び手の for が止まる)。 */
    for (i = 0; i < 64; i++) {
        char b[PCI_VERBOSE_LINE_MAX];
        if (pci_verbose_line(cfg, 0, 0, 0, i, b, (int)sizeof(b))
            == PCI_VERBOSE_END) break;
    }
    CHECK(i < 64);
}

static void verbose_parse(void)
{
    u32 b = 99, d = 99, f = 99;
    CHECK(pci_parse_bdf("0:9.0", &b, &d, &f) == 0);
    CHECK(b == 0 && d == 9 && f == 0);
    CHECK(pci_parse_bdf("255:31.7", &b, &d, &f) == 0);
    CHECK(b == 255 && d == 31 && f == 7);
    /* 範囲外・形の違いは断る (隣の欄へ溢れた番地を読まない)。 */
    CHECK(pci_parse_bdf("256:0.0", &b, &d, &f) != 0);
    CHECK(pci_parse_bdf("0:32.0", &b, &d, &f) != 0);
    CHECK(pci_parse_bdf("0:0.8", &b, &d, &f) != 0);
    CHECK(pci_parse_bdf("0:9", &b, &d, &f) != 0);
    CHECK(pci_parse_bdf("0.9:0", &b, &d, &f) != 0);
    CHECK(pci_parse_bdf("0:9.0x", &b, &d, &f) != 0);
    CHECK(pci_parse_bdf(":9.0", &b, &d, &f) != 0);
    CHECK(pci_parse_bdf("", &b, &d, &f) != 0);
    /* 断ったときは出力を書き換えない。 */
    b = d = f = 42;
    CHECK(pci_parse_bdf("0:99.0", &b, &d, &f) != 0);
    CHECK(b == 42 && d == 42 && f == 42);
}

/* Codex P2: mem64 は上位と下位の両方で「未割り当て」を決める。
 * 下位 0x00000004 (番地 0) + 上位 0x00000001 = 0x1_0000_0000 に割り当て済み。
 * また、上位が 0x00000004 (= mem64 の下位と同じ形) でも、その次の欄を
 * 上位と取り違えない (組は先頭から歩いて決まる)。予約型の書式もここで。 */
static void verbose_mem64(void)
{
    u32 cfg[PCI_VERBOSE_CFG_DWORDS];
    char out[2048];

    cfg_clear(cfg);
    cfg[0x00 / 4] = 0x880014F1UL;
    cfg[0x10 / 4] = 0x00000004UL;   /* mem64 下位、番地の下位 32 ビットは 0 */
    cfg[0x14 / 4] = 0x00000001UL;   /* 上位 = 1 → 0x1_0000_0000 */
    cfg[0x18 / 4] = 0xE0000004UL;   /* mem64 下位 */
    cfg[0x1C / 4] = 0x00000004UL;   /* その上位 = 4 (下位と同じ形の値) */
    cfg[0x20 / 4] = 0xF0000004UL;   /* 次の組の下位 — 上位ではない */
    cfg[0x24 / 4] = 0x00000000UL;   /* その上位 */

    render(cfg, 0, 9, 0, out, sizeof(out));
    CHECK(strstr(out,
        "  bar0 00000004 mem64-lo base 0x0000000100000000\n"
        "  bar1 00000001 mem64-hi (bar0 above 4G)\n"
        "  bar2 e0000004 mem64-lo base 0x00000004e0000000\n"
        "  bar3 00000004 mem64-hi (bar2 above 4G)\n"
        "  bar4 f0000004 mem64-lo base 0xf0000000\n"
        "  bar5 00000000 mem64-hi (bar4)\n") != 0);
    CHECK(strstr(out, "unassigned") == 0);
    if (failed) fprintf(stderr, "%s", out);

    /* 上位も下位も 0 のときだけ unassigned。 */
    cfg[0x14 / 4] = 0x00000000UL;
    render(cfg, 0, 9, 0, out, sizeof(out));
    CHECK(strstr(out, "  bar0 00000004 mem64-lo base 0x00000000 unassigned\n") != 0);

    /* 予約型 (bit2〜1 = 11b) と prefetch。 */
    cfg_clear(cfg);
    cfg[0x00 / 4] = 0x880014F1UL;
    cfg[0x10 / 4] = 0xF800000EUL;
    cfg[0x14 / 4] = 0x00000006UL;
    render(cfg, 0, 9, 0, out, sizeof(out));
    CHECK(strstr(out, "  bar0 f800000e mem-rsv base 0xf8000000 prefetch\n") != 0);
    CHECK(strstr(out, "  bar1 00000006 mem-rsv base 0x00000000 unassigned\n") != 0);
}

/* 最長の行: 小さい buf へ書いても溢れず、先頭がそのまま残る。
 * 最長の行も PCI_VERBOSE_LINE_MAX に余裕で収まる。 */
static void verbose_longest(void)
{
    u32 cfg[PCI_VERBOSE_CFG_DWORDS];
    char full[PCI_VERBOSE_LINE_MAX];
    char part[PCI_VERBOSE_LINE_MAX + 8];
    int k, r, cap, best = -1;
    size_t bestlen = 0;

    cfg_clear(cfg);
    cfg[0x00 / 4] = 0xFFFF10FCUL;   /* I-O DATA (いちばん長いベンダ名) */
    cfg[0x0C / 4] = 0x00800000UL;
    cfg[0x10 / 4] = 0x0000000CUL;   /* mem64 prefetch、上位も */
    cfg[0x14 / 4] = 0xFFFFFFFFUL;   /*   64 ビットの番地 = 最長の BAR 行 */
    cfg[0x24 / 4] = 0x0000000CUL;   /* 最後の欄の mem64 prefetch unassigned */
    cfg[0x3C / 4] = 0x000000FFUL;

    for (k = 0; k < 64; k++) {
        r = pci_verbose_line(cfg, 255, 31, 7, k, full, (int)sizeof(full));
        if (r == PCI_VERBOSE_END) break;
        if (r == PCI_VERBOSE_SKIP) continue;
        if (strlen(full) > bestlen) { bestlen = strlen(full); best = k; }
    }
    CHECK(best >= 0);
    CHECK(bestlen + 1 < (size_t)PCI_VERBOSE_LINE_MAX);
    pci_verbose_line(cfg, 255, 31, 7, best, full, (int)sizeof(full));

    for (cap = 1; cap <= (int)bestlen + 1; cap++) {
        memset(part, 'Z', sizeof(part));
        r = pci_verbose_line(cfg, 255, 31, 7, best, part, cap);
        CHECK(r == PCI_VERBOSE_LINE);
        CHECK(strlen(part) == (size_t)(cap - 1));
        CHECK(strncmp(part, full, (size_t)(cap - 1)) == 0);
        CHECK(part[cap] == 'Z');     /* cap の外は触らない */
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    failed = 0;
    if (!strcmp(argv[1], "cfg_addr")) cfg_addr();
    else if (!strcmp(argv[1], "probe_values")) probe_values();
    else if (!strcmp(argv[1], "bar_kind")) bar_kind();
    else if (!strcmp(argv[1], "bar_base")) bar_base();
    else if (!strcmp(argv[1], "header_type")) header_type();
    else if (!strcmp(argv[1], "extract")) extract();
    else if (!strcmp(argv[1], "names")) names();
    else if (!strcmp(argv[1], "story_82557")) story_82557();
    else if (!strcmp(argv[1], "absent")) absent();
    else if (!strcmp(argv[1], "verbose_tuner")) verbose_tuner();
    else if (!strcmp(argv[1], "verbose_bars")) verbose_bars();
    else if (!strcmp(argv[1], "verbose_bridge")) verbose_bridge();
    else if (!strcmp(argv[1], "verbose_bounds")) verbose_bounds();
    else if (!strcmp(argv[1], "verbose_parse")) verbose_parse();
    else if (!strcmp(argv[1], "verbose_mem64")) verbose_mem64();
    else if (!strcmp(argv[1], "verbose_longest")) verbose_longest();
    else return 2;
    if (failed) return 1;
    printf("PASS %s\n", argv[1]);
    return 0;
}
