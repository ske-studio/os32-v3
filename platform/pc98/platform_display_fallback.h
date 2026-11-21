/* Build-selected last-resort display backend (PC-98: 9801 標準グラフィック)。 */
#ifndef OS32_DISPLAY_FALLBACK_H
#define OS32_DISPLAY_FALLBACK_H
#define DISPLAY_FALLBACK_BACKEND (&gfx_backend_pc98)
#define DISPLAY_V1_WIDTH GFX_WIDTH
#define DISPLAY_V1_PRESENT_OK() 1
#define DISPLAY_V2_SHADOW_PUSH(x, y, w, h) OS32_ERR_NOSYS
#define DISPLAY_V1_PUBLIC_INIT_OK() do { } while (0)
/* PC-98 のテキスト面は別なので、表示を戻す操作は要らない。 */
static inline int display_provider_console_owns_scanout(void) { return 0; }
static inline void display_provider_console_return(void) { }

/* exec の終了通知の通常文脈専用。割り込み・例外からは呼ばない。
 * id は終了するアプリ。所有の返却前に呼び、確保・描画・ログが可能。 */
static inline void display_provider_owner_exit(int id) { (void)id; }
#endif
