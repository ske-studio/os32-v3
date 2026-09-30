/* ======================================================================== */
/*  ECS_CORE.C — Entity 管理・フリーリスト・遅延破棄                        */
/*                                                                          */
/*  Entity の生成・破棄・ライフサイクル管理、                                */
/*  System 登録・実行、コールバックを担当する。                              */
/* ======================================================================== */

#include "libos32ecs.h"
#include <string.h>              /* memset */

/* ====================================================================== */
/*  内部データ                                                             */
/* ====================================================================== */

/* Entity メタデータ配列 */
static EcsEntity g_entities[ECS_MAX_ENTITIES];

/* フリーリスト (スタック方式) */
static i16 g_free_stack[ECS_MAX_ENTITIES];
static int g_free_top;           /* スタックトップ (次に取り出す位置) */

/* 統計 */
static int g_active_count;

/* コールバック */
static ecs_lifecycle_cb g_on_create;
static ecs_lifecycle_cb g_on_destroy;
static ecs_timer_cb     g_timer_cb;

/* ====================================================================== */
/*  System 管理                                                            */
/* ====================================================================== */

typedef struct {
    ecs_system_fn fn;            /* 処理関数 */
    u32  required_mask;          /* 必要コンポーネントマスク */
    int  priority;               /* 優先度 (小さいほど先) */
    u8   enabled;                /* 有効フラグ */
    u8   _pad[3];
} EcsSystemEntry;

static EcsSystemEntry g_systems[ECS_MAX_SYSTEMS];
static int g_num_systems;

/* 実行順序キャッシュ (priority でソート済みのインデックス) */
static int g_exec_order[ECS_MAX_SYSTEMS];
static int g_exec_dirty;        /* ソートが必要か */

/* ====================================================================== */
/*  外部参照 (SoA配列 — ecs_component.c で定義)                            */
/* ====================================================================== */

extern EcsEntity *ecs__get_entities(void);
extern void       ecs__clear_components(ecs_entity_t e);

/* ====================================================================== */
/*  フリーリスト構築                                                       */
/* ====================================================================== */

static void build_free_list(void)
{
    int i;
    g_free_top = ECS_MAX_ENTITIES;
    /* 逆順にプッシュすることで、ID 0 が最初に取り出される */
    for (i = ECS_MAX_ENTITIES - 1; i >= 0; i--) {
        g_free_stack[i] = (i16)(ECS_MAX_ENTITIES - 1 - i);
    }
}

/* ====================================================================== */
/*  実行順序ソート (挿入ソート — 最大16要素なので十分)                     */
/* ====================================================================== */

static void sort_exec_order(void)
{
    int i, j, key, key_prio;
    for (i = 0; i < g_num_systems; i++) {
        g_exec_order[i] = i;
    }
    for (i = 1; i < g_num_systems; i++) {
        key = g_exec_order[i];
        key_prio = g_systems[key].priority;
        j = i - 1;
        while (j >= 0 && g_systems[g_exec_order[j]].priority > key_prio) {
            g_exec_order[j + 1] = g_exec_order[j];
            j--;
        }
        g_exec_order[j + 1] = key;
    }
    g_exec_dirty = 0;
}

/* ====================================================================== */
/*  API — システム管理                                                     */
/* ====================================================================== */

int ecs_init(void)
{
    memset(g_entities, 0, sizeof(g_entities));
    memset(g_systems, 0, sizeof(g_systems));
    g_num_systems = 0;
    g_active_count = 0;
    g_on_create = 0;
    g_on_destroy = 0;
    g_timer_cb = 0;
    g_exec_dirty = 1;
    build_free_list();
    return 0;
}

void ecs_shutdown(void)
{
    ecs_reset();
}

void ecs_reset(void)
{
    int i;
    for (i = 0; i < ECS_MAX_ENTITIES; i++) {
        if (g_entities[i].alive != ECS_DEAD) {
            ecs__clear_components((ecs_entity_t)i);
        }
    }
    memset(g_entities, 0, sizeof(g_entities));
    g_active_count = 0;
    build_free_list();
}

/* ====================================================================== */
/*  API — Entity 生成・破棄                                                */
/* ====================================================================== */

ecs_entity_t ecs_create(void)
{
    ecs_entity_t e;
    if (g_free_top <= 0) {
        return ECS_INVALID;     /* 満杯 */
    }
    g_free_top--;
    e = g_free_stack[g_free_top];

    g_entities[e].comp_mask = 0;
    g_entities[e].alive = ECS_ALIVE;
    g_entities[e].layer = 0;
    /* gen はインクリメントのみ (再利用時のダングリング検出) */
    g_entities[e].gen++;
    g_active_count++;

    if (g_on_create) {
        g_on_create(e);
    }
    return e;
}

void ecs_destroy(ecs_entity_t e)
{
    if (e < 0 || e >= ECS_MAX_ENTITIES) return;
    if (g_entities[e].alive != ECS_ALIVE) return;
    g_entities[e].alive = ECS_PENDING_DESTROY;
}

int ecs_alive(ecs_entity_t e)
{
    if (e < 0 || e >= ECS_MAX_ENTITIES) return 0;
    return g_entities[e].alive == ECS_ALIVE;
}

int ecs_active_count(void)
{
    return g_active_count;
}

/* ====================================================================== */
/*  API — フレーム制御                                                     */
/* ====================================================================== */

void ecs_update(void)
{
    int i, sys_id;

    if (g_exec_dirty) {
        sort_exec_order();
    }

    for (i = 0; i < g_num_systems; i++) {
        sys_id = g_exec_order[i];
        if (g_systems[sys_id].enabled && g_systems[sys_id].fn) {
            g_systems[sys_id].fn();
        }
    }
}

void ecs_cleanup(void)
{
    int i;
    for (i = 0; i < ECS_MAX_ENTITIES; i++) {
        if (g_entities[i].alive == ECS_PENDING_DESTROY) {
            if (g_on_destroy) {
                g_on_destroy((ecs_entity_t)i);
            }
            ecs__clear_components((ecs_entity_t)i);
            g_entities[i].comp_mask = 0;
            g_entities[i].alive = ECS_DEAD;
            g_entities[i].layer = 0;
            /* gen は維持 (次の create 時にインクリメント) */

            /* フリーリストに戻す */
            g_free_stack[g_free_top] = (i16)i;
            g_free_top++;
            g_active_count--;
        }
    }
}

/* ====================================================================== */
/*  API — System 登録・制御                                                */
/* ====================================================================== */

int ecs_register_system(ecs_system_fn fn, int priority,
                        u32 required_mask)
{
    int id;
    if (!fn) return -1;
    if (g_num_systems >= ECS_MAX_SYSTEMS) return -1;

    id = g_num_systems;
    g_systems[id].fn = fn;
    g_systems[id].priority = priority;
    g_systems[id].required_mask = required_mask;
    g_systems[id].enabled = 1;
    g_num_systems++;
    g_exec_dirty = 1;
    return id;
}

void ecs_enable_system(int sys_id, int enable)
{
    if (sys_id < 0 || sys_id >= g_num_systems) return;
    g_systems[sys_id].enabled = enable ? 1 : 0;
}

/* ====================================================================== */
/*  API — コールバック                                                     */
/* ====================================================================== */

void ecs_on_create(ecs_lifecycle_cb cb)
{
    g_on_create = cb;
}

void ecs_on_destroy(ecs_lifecycle_cb cb)
{
    g_on_destroy = cb;
}

void ecs_set_timer_callback(ecs_timer_cb cb)
{
    g_timer_cb = cb;
}

/* ====================================================================== */
/*  内部アクセサ (ecs_component.c / ecs_query.c から参照)                  */
/* ====================================================================== */

EcsEntity *ecs__get_entities(void)
{
    return g_entities;
}

ecs_timer_cb ecs__get_timer_cb(void)
{
    return g_timer_cb;
}

/* ====================================================================== */
/*  デバッグ                                                               */
/* ====================================================================== */

extern KernelAPI *kapi;

void ecs_debug_dump(void)
{
    int i, count = 0;

    if (!kapi) return;

    kapi->kprintf(ATTR_CYAN, "[ECS] active=%d / %d, systems=%d\n",
                  g_active_count, ECS_MAX_ENTITIES, g_num_systems);

    for (i = 0; i < ECS_MAX_ENTITIES; i++) {
        if (g_entities[i].alive == ECS_DEAD) continue;

        kapi->kprintf(ATTR_WHITE,
            "  e[%d] alive=%d gen=%d mask=0x%04lx",
            i, g_entities[i].alive,
            (int)g_entities[i].gen,
            (unsigned long)g_entities[i].comp_mask);

        /* Transform座標を表示 (保有している場合) */
        if (g_entities[i].comp_mask & COMP_TRANSFORM) {
            CompTransform *t = ecs_get_transform((ecs_entity_t)i);
            kapi->kprintf(ATTR_WHITE, " pos=(%d,%d)",
                          (int)t->tile_x, (int)t->tile_y);
        }

        /* HP表示 (保有している場合) */
        if (g_entities[i].comp_mask & COMP_HEALTH) {
            CompHealth *hp = ecs_get_health((ecs_entity_t)i);
            kapi->kprintf(ATTR_WHITE, " hp=%d/%d",
                          (int)hp->hp, (int)hp->max_hp);
        }

        /* タグ表示 */
        if (g_entities[i].comp_mask & COMP_TAG) {
            CompTag *tag = ecs_get_tag((ecs_entity_t)i);
            kapi->kprintf(ATTR_WHITE, " tag=%d",
                          (int)tag->tag);
        }

        kapi->kprintf(ATTR_WHITE, "\n");
        count++;
        if (count >= 32) {
            kapi->kprintf(ATTR_YELLOW, "  ... (%d more)\n",
                          g_active_count - count);
            break;
        }
    }
}
