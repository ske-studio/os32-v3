/* kprintf の書式の処理の本体。取り込む側が、出力先と関数の名前を決める:
 *   KPRINTF_WRITE(buf, n, attr)・KPRINTF_PRINT(str, attr)・KPRINTF_PRINT_UTF8(str, attr) — 出力先
 *   KPRINTF_VNAME・KPRINTF_NAME — 引数の列で受ける関数と、可変長引数で受ける関数の名前
 * 取り込む前に kprintf.h と <stdarg.h> を読んでおく。コンソール向けは sys/kern/kprintf.c、
 * 機種が診断を別の所へ出す時はその機種のソースが取り込む (docs/design/lib/kprintf.md)。 */

#define KPRINTF_NUMBUF_SIZE 16

/* ======== 内部: パディング付き出力 ======== */

/* 文字列をパディング付きで出力 */
static void emit_padded(const char *str, int len, int width, 
                        char pad_char, int left_align, u8 attr)
{
    int pad;
    char pad_buf[2];
    pad_buf[1] = '\0';
    pad_buf[0] = pad_char;

    pad = width - len;
    if (pad < 0) pad = 0;

    if (!left_align) {
        /* 右寄せ: 先にパディング */
        while (pad > 0) {
            KPRINTF_PRINT(pad_buf, attr);
            pad--;
        }
    }

    /* 本体出力 */
    KPRINTF_PRINT(str, attr);

    if (left_align) {
        /* 左寄せ: 後でスペースパディング (ゼロパディングは左寄せと併用しない) */
        pad_buf[0] = ' ';
        while (pad > 0) {
            KPRINTF_PRINT(pad_buf, attr);
            pad--;
        }
    }
}

/* Both interfaces carry 32-bit words, including %d/%c and long. Strings
 * use va_arg's pointer type on the native variadic path. Keep a pointer to
 * va_list: on register ABIs it need not be a simple stack pointer. */
struct print_args {
    const u32 *words;
    va_list *ap;
};

static u32 print_arg(struct print_args *args, char spec)
{
    if (!args->ap) return *args->words++;
    if (spec == 's') return (u32)va_arg(*args->ap, const char *);
    return va_arg(*args->ap, u32);
}

static void print_format(u8 attr, const char *fmt, struct print_args *args)
{
    const char *p = fmt;

    /* 属性は **ここで 1 回だけ** PC-98 のテキスト属性へ直す。
     * 呼び出し側の大半は PC/AT (CGA) 流の 0x07 などを渡しており、
     * それを属性 VRAM へ直書きすると画面に何も出ない
     * (sys/kern/kprintf_attr.c の冒頭に経緯)。下流 (shell_print /
     * shell_print_utf8 / console_write) は全部この attr を使うので、
     * 変換を入口に置けば取りこぼしが無い。 */
    attr = kprintf_attr_to_pc98(attr);

    while (*p) {
        if (*p == '%' && p[1]) {
            int width = 0;
            int left_align = 0;
            char pad_char = ' ';
            char numbuf[KPRINTF_NUMBUF_SIZE];
            int len;

            p++;  /* '%' をスキップ */

            kprintf_spec(&p, &width, &left_align, &pad_char);
            /* A suffix such as "%0" has no conversion or argument. */
            if (!*p) break;

            /* 変換指定子 */
            switch (*p) {
                case 'd':
                case 'i': {
                    int val = (int)print_arg(args, *p);
                    int neg = 0;
                    u32 uval;
                    if (val < 0) {
                        neg = 1;
                        uval = 0u - (u32)val;
                    } else {
                        uval = (u32)val;
                    }
                    len = kutoa_dec(uval, numbuf + 1, sizeof(numbuf) - 2);
                    if (neg) {
                        /* 符号の処理 */
                        if (pad_char == '0' && !left_align) {
                            /* ゼロパディング: 符号を先に出力 */
                            KPRINTF_PRINT("-", attr);
                            emit_padded(numbuf + 1, len, width - 1,
                                        pad_char, left_align, attr);
                        } else {
                            numbuf[0] = '-';
                            emit_padded(numbuf, len + 1, width,
                                        pad_char, left_align, attr);
                        }
                    } else {
                        emit_padded(numbuf + 1, len, width,
                                    pad_char, left_align, attr);
                    }
                    break;
                }
                case 'u': {
                    u32 val = print_arg(args, *p);
                    len = kutoa_dec(val, numbuf, sizeof(numbuf) - 1);
                    emit_padded(numbuf, len, width,
                                pad_char, left_align, attr);
                    break;
                }
                case 'x':
                case 'X': {
                    u32 val = print_arg(args, *p);
                    int upper = (*p == 'X');
                    len = kutoa_hex(val, numbuf, sizeof(numbuf) - 1, upper);
                    emit_padded(numbuf, len, width,
                                pad_char, left_align, attr);
                    break;
                }
                case 's': {
                    const char *str = (const char *)print_arg(args, *p);
                    int slen;
                    const char *s;
                    if (!str) str = "(null)";
                    /* 文字列長を計算 */
                    slen = 0;
                    s = str;
                    while (*s) { slen++; s++; }
                    if (width > 0) {
                        /* パディング付き出力 (UTF-8はshell_print内で処理) */
                        int pad = width - slen;
                        if (pad < 0) pad = 0;
                        if (!left_align) {
                            char sp[2] = {' ', '\0'};
                            while (pad > 0) { KPRINTF_PRINT(sp, attr); pad--; }
                        }
                        KPRINTF_PRINT_UTF8(str, attr);
                        if (left_align) {
                            char sp[2] = {' ', '\0'};
                            while (pad > 0) { KPRINTF_PRINT(sp, attr); pad--; }
                        }
                    } else {
                        KPRINTF_PRINT_UTF8(str, attr);
                    }
                    break;
                }
                case 'c': {
                    char ch_buf[2];
                    ch_buf[0] = (char)print_arg(args, *p);
                    ch_buf[1] = '\0';
                    KPRINTF_PRINT(ch_buf, attr);
                    break;
                }
                case '%':
                    KPRINTF_PRINT("%", attr);
                    break;
                default: {
                    /* 不明な指定子: そのまま出力。
                     * 呼び出し側は指定子ぶんの実引数を積んでいる前提なので、
                     * 消費しないと以降の %d/%s が 1 個ずつずれて別の引数を
                     * (最悪ポインタとして) 読んでしまう。 */
                    char tmp[2];
                    tmp[0] = *p;
                    tmp[1] = '\0';
                    (void)print_arg(args, *p);
                    KPRINTF_PRINT(tmp, attr);
                    break;
                }
            }
        } else {
            const char *start = p;
            while (*p && *p != '%') p++;
            if (p == start) {
                /* 末尾の '%' (p[1]=='\0' なので上の分岐に入らない)。
                 * ここで進めないと *p が '%' のまま無限ループする。 */
                KPRINTF_WRITE(p, 1, attr);
                p++;
                continue;
            }
            KPRINTF_WRITE(start, (u32)(p - start), attr);
            continue;
        }
        p++;
    }
}

void KPRINTF_VNAME(u8 attr, const char *fmt, const u32 *args)
{
    struct print_args input = { .words = args, .ap = 0 };
    print_format(attr, fmt, &input);
}

void __cdecl KPRINTF_NAME(u8 attr, const char *fmt, ...)
{
    va_list ap;
    struct print_args input = { .words = 0, .ap = &ap };
    va_start(ap, fmt);
    print_format(attr, fmt, &input);
    va_end(ap);
}
