static int v86_gp_emulate(u32 *frame, const u8 *pc, u32 len)
{
    u16 port;

    switch (pc[len]) {
    /* ---- IN ---- */
    case 0xE4:  /* IN AL, imm8 */
        port = pc[len + 1];
        v86_set_al(frame, v86_io_in(port, 1));
        v86_advance_ip(frame, len + 2);
        return 0;

    case 0xE5:  /* IN AX, imm8 */
        port = pc[len + 1];
        v86_set_ax(frame, v86_io_in(port, 2));
        v86_advance_ip(frame, len + 2);
        return 0;

    case 0xEC:  /* IN AL, DX */
        port = (u16)(frame[V86F_EDX] & U16_MAX);
        v86_set_al(frame, v86_io_in(port, 1));
        v86_advance_ip(frame, len + 1);
        return 0;

    case 0xED:  /* IN AX, DX */
        port = (u16)(frame[V86F_EDX] & U16_MAX);
        v86_set_ax(frame, v86_io_in(port, 2));
        v86_advance_ip(frame, len + 1);
        return 0;

    /* ---- OUT ---- */
    case 0xE6:  /* OUT imm8, AL */
        port = pc[len + 1];
        v86_io_out(port, 1, frame[V86F_EAX] & U8_MAX);
        v86_advance_ip(frame, len + 2);
        return 0;

    case 0xE7:  /* OUT imm8, AX */
        port = pc[len + 1];
        v86_io_out(port, 2, frame[V86F_EAX] & U16_MAX);
        v86_advance_ip(frame, len + 2);
        return 0;

    case 0xEE:  /* OUT DX, AL */
        port = (u16)(frame[V86F_EDX] & U16_MAX);
        v86_io_out(port, 1, frame[V86F_EAX] & U8_MAX);
        v86_advance_ip(frame, len + 1);
        return 0;

    case 0xEF:  /* OUT DX, AX */
        port = (u16)(frame[V86F_EDX] & U16_MAX);
        v86_io_out(port, 2, frame[V86F_EAX] & U16_MAX);
        v86_advance_ip(frame, len + 1);
        return 0;

    case 0xCD:  /* INT imm8 */
        /* V86 の INT n は IOPL に関係なくプロテクトモードの IDT を引く。
         * DPL0 のゲートはここへ落ちる。DPL3 の 80h は v86_int80 へ行く。
         * (IOPL=3 が消してくれるのは CLI/STI/PUSHF/POPF/IRET のほう) */
        {
            u32 vec = pc[len + 1];
            v86_advance_ip(frame, len + 2);
            if (v86_bios_is_hle(vec)) {
                /* HLE 対象: レジスタと CF を直接書いて戻す。
                 * INT は配送前に落ちているのでゲストスタックは無傷。 */
                v86_bios_dispatch(frame, vec);
            } else {
                /* それ以外はゲスト自身の IVT (多くは BIOS ROM) へ流す */
                if (!v86_inject_int(frame, vec)) {
                    v86_exit_reason = V86_EXIT_FAULT;
                    return 1;
                }
            }
        }
        return 0;

    case 0xF4:  /* HLT — Phase 1 から引き続きセッション終了の合図 */
        v86_exit_reason = V86_EXIT_HLT;
        return 1;

    default:
        /* INS/OUTS (6C-6F) と 0x0F 2 バイトオペコードは未対応。
         * 1987 年のゲームが使う可能性は低いが、必要になったら足す。 */
        v86_exit_reason = V86_EXIT_UNKNOWN_OP;
        return 1;
    }
}
