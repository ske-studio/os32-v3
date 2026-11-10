/* Build-selected last-resort display backend. PowerMac の描画は未実装で、
 * 何もしない backend を指す (起動の profile は拒否のまま)。 */
#ifndef OS32_DISPLAY_FALLBACK_H
#define OS32_DISPLAY_FALLBACK_H
extern GfxBackend gfx_backend_unsupported;
#define DISPLAY_FALLBACK_BACKEND (&gfx_backend_unsupported)
#endif
