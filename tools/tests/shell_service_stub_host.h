/* ========================================================================
 *  shell_service_stub_host.h — exec/exec.c を切り出して組むホスト試験向けの
 *  T7b-b1/b2 S3b 結線の弱い贋物
 *
 *  exec.c の ring3_wm_enter / leave・ring3_context_*・dispatch 入口・
 *  exec_run / exec_finish の shell 分岐・exec_reclaim_resources は S3b から
 *  exec/shell_service.c (WM 区間の印、publish / retire) と kernel/font_source.c
 *  (font_lease_close_owned) を引く。それらを見ない試験は、切り出した部分を
 *  #include する前にこれを #include する。弱い定義なので、実物か試験自身の
 *  定義があればそちらが勝つ。実物の振る舞いは shell_service_host.c と
 *  font_source_host.c が見る。
 * ======================================================================== */
#ifndef OS32_SHELL_SERVICE_STUB_HOST_H
#define OS32_SHELL_SERVICE_STUB_HOST_H

__attribute__((weak)) volatile unsigned int ring3_wm_shell_gen;
__attribute__((weak)) void shell_service_wm_mark(void) {}
__attribute__((weak)) void shell_service_retire(void) {}
__attribute__((weak)) void shell_service_publish(unsigned int load_addr, unsigned int text_size)
{
    (void)load_addr;
    (void)text_size;
}
__attribute__((weak)) void font_lease_close_owned(int owner) { (void)owner; }

#endif /* OS32_SHELL_SERVICE_STUB_HOST_H */
