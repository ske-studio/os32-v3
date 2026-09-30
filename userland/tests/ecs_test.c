/* ======================================================================== */
/*  ECS_TEST.C — libos32ecs 基本テスト                                     */
/*                                                                          */
/*  Entity CRUD, コンポーネント操作, System実行, クエリ, カスタム登録       */
/* ======================================================================== */

#include "os32api.h"
#include "libos32ecs.h"
#include "rt/testresult.h"

extern KernelAPI *kapi;
#define api kapi

static int g_total;
static int g_passed;

static void check(const char *label, int cond)
{
    g_total++;
    if (cond) {
        g_passed++;
        api->kprintf(ATTR_GREEN, "  [OK] %s\n", label);
    } else {
        api->kprintf(ATTR_RED, "  [NG] %s\n", label);
    }
}

/* タイマー満了コールバック用 */
static int timer_fired;
static u8  timer_fired_id;

static void on_timer(ecs_entity_t e, u8 cb_id)
{
    (void)e;
    timer_fired = 1;
    timer_fired_id = cb_id;
}

/* ライフサイクルコールバック用 */
static int create_count;
static int destroy_count;

static void on_create_cb(ecs_entity_t e)
{
    (void)e;
    create_count++;
}

static void on_destroy_cb(ecs_entity_t e)
{
    (void)e;
    destroy_count++;
}

/* カスタムコンポーネント */
#define COMP_INVENTORY  0x0400u  /* bit 10 */

/* ecs_core.c の内部データにアクセス */
extern EcsEntity *ecs__get_entities(void);

int main(int argc, char **argv, KernelAPI *k)
{
    ecs_entity_t e0, e1, e2;
    ecs_entity_t buf[8];
    int n, ret, i;
    CompTransform *t;
    CompVelocity *v;
    CompHealth *hp;
    CompTag *tag;
    CompTimer *tmr;

    (void)argc; (void)argv; (void)k;
    g_total = 0;
    g_passed = 0;

    api->kprintf(ATTR_CYAN, "=== libos32ecs test ===\n");

    /* ---- 1. 初期化 ---- */
    ecs_init();
    check("init: active=0", ecs_active_count() == 0);

    /* ---- 2. Entity 生成 ---- */
    e0 = ecs_create();
    check("create e0: valid", e0 != ECS_INVALID);
    check("create e0: alive", ecs_alive(e0));
    check("create e0: count=1", ecs_active_count() == 1);

    e1 = ecs_create();
    e2 = ecs_create();
    check("create e1,e2: count=3", ecs_active_count() == 3);

    /* ---- 3. コンポーネント追加・確認 ---- */
    ret = ecs_add(e0, COMP_TRANSFORM | COMP_VELOCITY);
    check("add transform+velocity: ok", ret == 0);
    check("has transform: yes", ecs_has(e0, COMP_TRANSFORM));
    check("has velocity: yes", ecs_has(e0, COMP_VELOCITY));
    check("has health: no", !ecs_has(e0, COMP_HEALTH));

    /* ---- 4. コンポーネント値の操作 ---- */
    t = ecs_get_transform(e0);
    t->x = FIX16_FROM_INT(10);
    t->y = FIX16_FROM_INT(20);
    check("transform x=10", FIX16_TO_INT(t->x) == 10);

    v = ecs_get_velocity(e0);
    v->vx = FIX16_FROM_INT(1);
    v->vy = FIX16_FROM_INT(2);

    /* ---- 5. System 登録・実行 (physics) ---- */
    ecs_register_system(sys_physics, 10, COMP_TRANSFORM | COMP_VELOCITY);
    ecs_update();

    t = ecs_get_transform(e0);
    check("physics: x=11", FIX16_TO_INT(t->x) == 11);
    check("physics: y=22", FIX16_TO_INT(t->y) == 22);
    check("physics: tile_x=11", t->tile_x == 11);
    check("physics: tile_y=22", t->tile_y == 22);

    /* ---- 6. 遅延破棄 ---- */
    ecs_destroy(e1);
    check("destroy e1: count still 3", ecs_active_count() == 3);
    ecs_cleanup();
    check("cleanup: count=2", ecs_active_count() == 2);
    check("e1 dead", !ecs_alive(e1));

    /* ---- 7. Entity再利用 ---- */
    e1 = ecs_create();
    check("reuse: valid", e1 != ECS_INVALID);
    check("reuse: count=3", ecs_active_count() == 3);

    /* ---- 8. コンポーネント除去 ---- */
    ecs_remove(e0, COMP_VELOCITY);
    check("remove velocity: gone", !ecs_has(e0, COMP_VELOCITY));
    check("remove velocity: transform stays", ecs_has(e0, COMP_TRANSFORM));

    /* ---- 9. タグ検索 ---- */
    ecs_add(e0, COMP_TAG);
    tag = ecs_get_tag(e0);
    tag->tag = 42;
    check("find_by_tag 42: found", ecs_find_by_tag(42) == e0);
    check("find_by_tag 99: not found", ecs_find_by_tag(99) == ECS_INVALID);

    /* ---- 10. マスククエリ ---- */
    n = ecs_query(COMP_TRANSFORM, buf, 8);
    check("query TRANSFORM: found", n >= 1);

    /* ---- 11. 矩形範囲検索 ---- */
    n = ecs_find_in_rect(10, 20, 5, 5, buf, 8);
    check("find_in_rect: found", n >= 1);
    n = ecs_find_in_rect(100, 100, 5, 5, buf, 8);
    check("find_in_rect: empty", n == 0);

    /* ---- 12. Health System ---- */
    ecs_add(e2, COMP_HEALTH);
    hp = ecs_get_health(e2);
    hp->hp = 0;
    hp->max_hp = 10;
    ecs_register_system(sys_health, 90, COMP_HEALTH);
    ecs_update();
    check("health: hp=0 pending",
          ecs__get_entities()[e2].alive == ECS_PENDING_DESTROY);
    ecs_cleanup();
    check("health: e2 dead", !ecs_alive(e2));

    /* ---- 13. Timer System ---- */
    e2 = ecs_create();
    ecs_add(e2, COMP_TIMER);
    tmr = ecs_get_timer(e2);
    tmr->remaining = 3;
    tmr->duration = 3;
    tmr->auto_repeat = 0;
    tmr->callback_id = 7;
    timer_fired = 0;
    ecs_set_timer_callback(on_timer);
    ecs_register_system(sys_timer, 80, COMP_TIMER);

    ecs_update();
    check("timer: not fired at 2", timer_fired == 0);
    ecs_update();
    check("timer: not fired at 1", timer_fired == 0);
    ecs_update();
    check("timer: fired at 0", timer_fired == 1);
    check("timer: cb_id=7", timer_fired_id == 7);

    /* ---- 14. カスタムコンポーネント保護 ---- */
    ret = ecs_add(e0, COMP_INVENTORY);
    check("custom: unregistered fails", ret == -1);

    ret = ecs_register_custom_comp(COMP_INVENTORY, "INVENTORY");
    check("custom: register ok", ret == 0);
    ret = ecs_add(e0, COMP_INVENTORY);
    check("custom: registered add ok", ret == 0);
    check("custom: has INVENTORY", ecs_has(e0, COMP_INVENTORY));

    ret = ecs_register_custom_comp(COMP_INVENTORY, "DUP");
    check("custom: dup fails", ret == -2);

    ret = ecs_register_custom_comp(COMP_TRANSFORM, "HACK");
    check("custom: system bit fails", ret == -1);

    check("custom: count=1", ecs_custom_comp_count() == 1);

    /* ---- 15. ライフサイクルコールバック ---- */
    create_count = 0;
    destroy_count = 0;
    ecs_on_create(on_create_cb);
    ecs_on_destroy(on_destroy_cb);
    {
        ecs_entity_t tmp = ecs_create();
        check("lifecycle: create cb", create_count == 1);
        ecs_destroy(tmp);
        ecs_cleanup();
        check("lifecycle: destroy cb", destroy_count == 1);
    }

    /* ---- 16. リセット ---- */
    ecs_reset();
    check("reset: count=0", ecs_active_count() == 0);

    /* ---- 17. 容量テスト ---- */
    {
        int ok = 1;
        for (i = 0; i < ECS_MAX_ENTITIES; i++) {
            if (ecs_create() == ECS_INVALID) { ok = 0; break; }
        }
        check("capacity: 128 ok", ok);
        check("capacity: 129th fails", ecs_create() == ECS_INVALID);
        check("capacity: count=128", ecs_active_count() == 128);
    }

    /* ---- 結果 ---- */
    ecs_shutdown();

    return os32_test_summary(api, "ecs_test", g_passed, g_total);
}
