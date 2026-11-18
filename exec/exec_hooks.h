/* exec のフックが在るかどうか (docs/design/exec/exec_hooks.md)。
 * 製品の像は -DOS32_HOOK_<名>=0/1 を全部渡す (build/sources/hooks.mk)。
 * 渡さない組み方 (host の試験など) は OS32_HOOK_DEFAULT (既定 1 = 全部のフックが在る)。
 * PowerPC 側のソースを単体で組む試験は -DOS32_HOOK_DEFAULT=0 を 1 つ渡す。 */
#ifndef OS32_EXEC_HOOKS_H
#define OS32_EXEC_HOOKS_H
#ifndef OS32_HOOK_DEFAULT
#define OS32_HOOK_DEFAULT 1
#endif
#ifndef OS32_HOOK_GFX
#define OS32_HOOK_GFX OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_GFX_POLL
#define OS32_HOOK_GFX_POLL OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_SURFACE
#define OS32_HOOK_SURFACE OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_GUI
#define OS32_HOOK_GUI OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_FONT_GUARD
#define OS32_HOOK_FONT_GUARD OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_FONT_LEASE
#define OS32_HOOK_FONT_LEASE OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_IME
#define OS32_HOOK_IME OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_SND
#define OS32_HOOK_SND OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_SHLIB
#define OS32_HOOK_SHLIB OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_LEGACY
#define OS32_HOOK_LEGACY OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_R1
#define OS32_HOOK_R1 OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_RECLAIM
#define OS32_HOOK_RECLAIM OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_KAPI_DB
#define OS32_HOOK_KAPI_DB OS32_HOOK_DEFAULT
#endif
#ifndef OS32_HOOK_SERIAL_DIAG
#define OS32_HOOK_SERIAL_DIAG OS32_HOOK_DEFAULT
#endif
#endif
