#ifndef OS32_POWERMAC_RH_OBSERVE_H
#define OS32_POWERMAC_RH_OBSERVE_H
#include "types.h"
#define PM_RH_FB_LIMIT 0x40000U
#define PM_RH_FB_FULL_LIMIT 0x200000U
#define PM_RH_FB_PAGE 4096U
#define PM_RH_FB_ROWS 64U
/* Camera legend: hex 0..D = reached stage; red E = OF ESCC prepare,
 * red F = mapped ESCC init failure. PHY page: port, addr, id1, id2 (hex).
 * Previously reached digits stay visible above the PHY page. */
#define PM_RH_ESCC_PREPARE 14U
#define PM_RH_ESCC_INIT 15U
struct pm_rh_fb { u32 pa, bytes, stride, width, height, depth; };
/* 0: absent (continue), 1: captured, -1: unsupported/invalid (stop in OF). */
int pm_rh_capture(int stdout_ih);
const struct pm_rh_fb *pm_rh_framebuffer(void);
enum pm_rh_full_result { PM_RH_FULL_OK, PM_RH_FULL_ABSENT, PM_RH_FULL_SIZE, PM_RH_FULL_TRANSLATE, PM_RH_FULL_ALIAS };
enum pm_rh_color_result { PM_RH_COLOR_UNCHECKED, PM_RH_COLOR_OK,
    PM_RH_COLOR_FAILED, PM_RH_COLOR_EQUAL };
struct pm_rh_colors { u32 rc, foreground, background; };
const struct pm_rh_colors *pm_rh_full_colors(void);
int pm_rh_capture_full(void);
void pm_rh_discard_full(void);
const struct pm_rh_fb *pm_rh_full_framebuffer(void);
int pm_rh_of_mark(unsigned stage);
void pm_rh_bind(u32 fb_va);
void pm_rh_stage(unsigned stage);
void pm_rh_phy(u32 port, u32 addr, u32 id1, u32 id2);
u32 pm_rh_reached(void);
unsigned pm_rh_last_stage(void);
#endif
