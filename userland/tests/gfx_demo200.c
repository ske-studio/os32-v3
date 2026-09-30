/* ======================================================================== */
/*  GFX_DEMO200.C — libos32gfx 200ラインモード統合テスト                    */
/*                                                                          */
/*  gfx_demo.c の200ラインモード版。スプライト+ダーティレクト転送で         */
/*  640x200モードの描画性能を検証する。                                     */
/* ======================================================================== */

#include "os32api.h"
#include "libos32gfx.h"

#define SPRINTF_MAX 64
extern int sprintf(char *str, const char *format, ...);

/* ======== テスト環境の定数 (200ラインモード用) ======== */
#define STAGE_W    320
#define STAGE_H    160
#define STAGE_X    ((640 - STAGE_W) / 2)
#define STAGE_Y    ((200 - STAGE_H) / 2)

#define SPRITE_SIZE 16
#define N_ENTITIES  8

/* ======== 簡易乱数 ======== */
static unsigned long _seed = 1234567UL;
static int rnd(int max)
{
    _seed = _seed * 1103515245UL + 12345UL;
    return (int)((_seed >> 16) % (unsigned long)max);
}

/* ======== デモエンティティ定義 ======== */
typedef struct {
    int x, y;
    int dx, dy;
    GFX_Sprite *spr;
} Entity;

static Entity entities[N_ENTITIES];

/* ======== テストアセットの生成 ======== */
static void build_assets(void)
{
    static const u8 colors[] = {9, 10, 12, 14, 11, 13, 9, 10};
    int i;

    for (i = 0; i < N_ENTITIES; i++) {
        GFX_Surface *surf = gfx_create_surface(SPRITE_SIZE, SPRITE_SIZE);
        if (surf) {
            u8 color = colors[i % 8];
            gfx_surface_clear(surf, 0);

            gfx_surface_fill_rect(surf, 0, 0, 16, 16, color);
            gfx_surface_fill_rect(surf, 0, 0, 16, 1, 15);
            gfx_surface_fill_rect(surf, 0, 15, 16, 1, 15);
            gfx_surface_fill_rect(surf, 0, 0, 1, 16, 15);
            gfx_surface_fill_rect(surf, 15, 0, 1, 16, 15);
            gfx_surface_fill_rect(surf, 4, 7, 8, 1, 15);
            gfx_surface_fill_rect(surf, 7, 4, 1, 8, 15);

            entities[i].spr = gfx_create_sprite(surf, 0);
            gfx_free_surface(surf);
        }
    }
}

/* ======== シーン構築 ======== */
static void setup_scene(KernelAPI *api)
{
    int i;

    gfx_clear(0);

    /* ステージ枠 */
    gfx_rect(STAGE_X - 1, STAGE_Y - 1, STAGE_W + 2, STAGE_H + 2, 7);
    kcg_set_scale(1);
    kcg_draw_utf8(STAGE_X, 2, "640x200 Mode - Sprite Test", 14, 0);

    /* 初回のVRAM転送 */
    gfx_present();

    /* エンティティ初期化 */
    for (i = 0; i < N_ENTITIES; i++) {
        entities[i].x = STAGE_X + rnd(STAGE_W - SPRITE_SIZE);
        entities[i].y = STAGE_Y + rnd(STAGE_H - SPRITE_SIZE);
        entities[i].dx = (rnd(2) ? 2 : -2);
        entities[i].dy = (rnd(2) ? 2 : -2);

        gfx_sprite_save_bg(entities[i].x, entities[i].y, entities[i].spr);
    }
}

/* ======== メインループ ======== */
void main(int argc, char **argv, KernelAPI *api)
{
    int fps, fps_frames;
    u32 last_tick, fps_tick;
    char fps_buf[SPRINTF_MAX];

    (void)argc; (void)argv;

    /* 200ラインモードで初期化 */
    libos32gfx_init(api);
    api->gfx_init_200();
    api->gfx_get_framebuffer(&gfx_fb);

    api->kcg_init();
    kcg_set_scale(1);

    build_assets();
    setup_scene(api);

    fps = 0;
    fps_frames = 0;
    last_tick = api->get_tick();
    fps_tick = last_tick;

    for (;;) {
        int i;
        int ch;

        /* ESCキーで終了 */
        while ((ch = api->kbd_trygetchar()) >= 0) {
            if (ch == 0x1B) goto done;
        }

        /* 背景復旧 */
        for (i = 0; i < N_ENTITIES; i++) {
            gfx_sprite_restore_bg(entities[i].x, entities[i].y, entities[i].spr);
        }

        /* 座標更新 */
        for (i = 0; i < N_ENTITIES; i++) {
            entities[i].x += entities[i].dx;
            entities[i].y += entities[i].dy;
            if (entities[i].x <= STAGE_X || entities[i].x >= STAGE_X + STAGE_W - SPRITE_SIZE) entities[i].dx = -entities[i].dx;
            if (entities[i].y <= STAGE_Y || entities[i].y >= STAGE_Y + STAGE_H - SPRITE_SIZE) entities[i].dy = -entities[i].dy;
        }

        /* 背景退避 */
        for (i = 0; i < N_ENTITIES; i++) {
            gfx_sprite_save_bg(entities[i].x, entities[i].y, entities[i].spr);
        }

        /* スプライト描画 */
        for (i = 0; i < N_ENTITIES; i++) {
            gfx_draw_sprite(entities[i].x, entities[i].y, entities[i].spr);
        }

        /* VRAM転送 (ダーティレクトのみ) */
        api->gfx_present_dirty();

        fps_frames++;
        if (api->get_tick() - fps_tick >= 100) {
            fps = fps_frames;
            fps_frames = 0;
            fps_tick = api->get_tick();

            /* FPS表示 */
            gfx_fill_rect(STAGE_X, STAGE_H + STAGE_Y + 4, 100, 12, 0);
            sprintf(fps_buf, "FPS: %d", fps);
            kcg_draw_utf8(STAGE_X, STAGE_H + STAGE_Y + 4, fps_buf, 10, 0);
            api->gfx_present_rect(STAGE_X, STAGE_H + STAGE_Y + 4, 100, 12);
        }

        /* フレーム待機 */
        while (api->get_tick() == last_tick) {
            api->sys_halt();
        }
        last_tick = api->get_tick();
    }

done:
    api->kprintf(ATTR_WHITE, "[DBG] goto done reached\r\n");
    libos32gfx_shutdown();
    api->kprintf(ATTR_WHITE, "[DBG] libos32gfx_shutdown done\r\n");
    api->tvram_clear();
    api->kprintf(ATTR_WHITE, "[DBG] tvram_clear done\r\n");
    api->kprintf(ATTR_WHITE, "[DBG] returning from main\r\n");
}
