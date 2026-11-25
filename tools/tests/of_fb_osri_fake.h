/* Ordinary scanout fixtures have no published OSRI image. */
#ifndef TEST_OF_FB_OSRI_FAKE_H
#define TEST_OF_FB_OSRI_FAKE_H
volatile u32 tick_count;
const struct gfx_kernel_fb *pm_osri_decoded(unsigned long long *generation)
{ *generation = 0; return 0; }
int gfx_osri_poll_abort(int caller) { (void)caller; return 0; }
int pm_osri_key(void) { return -1; }
void idle_wait_step(void) { }
#endif
