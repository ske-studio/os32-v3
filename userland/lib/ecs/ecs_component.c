/* ======================================================================== */
/*  ECS_COMPONENT.C — SoA 配列管理・コンポーネント操作・カスタム登録        */
/*                                                                          */
/*  コンポーネントの追加・除去・アクセサ、カスタムコンポーネント保護を担当。  */
/* ======================================================================== */

#include "libos32ecs.h"
#include <string.h>              /* memset */

/* ====================================================================== */
/*  SoA コンポーネント配列 (BSS 静的確保)                                  */
/* ====================================================================== */

static CompTransform g_transform[ECS_MAX_ENTITIES];
static CompVelocity  g_velocity[ECS_MAX_ENTITIES];
static CompSprite    g_sprite[ECS_MAX_ENTITIES];
static CompCollider  g_collider[ECS_MAX_ENTITIES];
static CompHealth    g_health[ECS_MAX_ENTITIES];
static CompChem      g_chem[ECS_MAX_ENTITIES];
static CompAI        g_ai[ECS_MAX_ENTITIES];
static CompTimer     g_timer[ECS_MAX_ENTITIES];
static CompTag       g_tag[ECS_MAX_ENTITIES];
static CompAnim      g_anim[ECS_MAX_ENTITIES];

/* ====================================================================== */
/*  カスタムコンポーネント登録テーブル                                      */
/* ====================================================================== */

static u32 g_registered_custom;  /* 登録済みカスタムビットの合成マスク */
static int g_custom_count;       /* 登録済み数 */

/* デバッグ用の名前テーブル (将来 ecs_debug_dump で使用) */
#ifdef ECS_DEBUG
static const char *g_custom_names[ECS_MAX_CUSTOM_COMP];
static u32         g_custom_bits[ECS_MAX_CUSTOM_COMP];
#endif

/* ====================================================================== */
/*  外部参照 (ecs_core.c で定義)                                           */
/* ====================================================================== */

extern EcsEntity *ecs__get_entities(void);

/* ====================================================================== */
/*  内部: Entity の全コンポーネントをクリア                                */
/* ====================================================================== */

void ecs__clear_components(ecs_entity_t e)
{
    if (e < 0 || e >= ECS_MAX_ENTITIES) return;
    memset(&g_transform[e], 0, sizeof(CompTransform));
    memset(&g_velocity[e], 0, sizeof(CompVelocity));
    memset(&g_sprite[e], 0, sizeof(CompSprite));
    memset(&g_collider[e], 0, sizeof(CompCollider));
    memset(&g_health[e], 0, sizeof(CompHealth));
    memset(&g_chem[e], 0, sizeof(CompChem));
    g_chem[e].chem_id = -1;
    memset(&g_ai[e], 0, sizeof(CompAI));
    g_ai[e].target_entity = -1;
    memset(&g_timer[e], 0, sizeof(CompTimer));
    memset(&g_tag[e], 0, sizeof(CompTag));
    memset(&g_anim[e], 0, sizeof(CompAnim));
}

/* ====================================================================== */
/*  API — コンポーネント操作                                               */
/* ====================================================================== */

int ecs_add(ecs_entity_t e, u32 comp_bit)
{
    u32 custom_bits;
    EcsEntity *ents;

    if (e < 0 || e >= ECS_MAX_ENTITIES) return -1;
    ents = ecs__get_entities();
    if (ents[e].alive != ECS_ALIVE) return -1;

    /* カスタムビットの登録チェック */
    custom_bits = comp_bit & COMP_CUSTOM_MASK;
    if (custom_bits) {
        /* 未登録のカスタムビットがあれば拒否 */
        if ((custom_bits & g_registered_custom) != custom_bits) {
            return -1;
        }
    }

    ents[e].comp_mask |= comp_bit;
    return 0;
}

void ecs_remove(ecs_entity_t e, u32 comp_bit)
{
    EcsEntity *ents;
    if (e < 0 || e >= ECS_MAX_ENTITIES) return;
    ents = ecs__get_entities();
    ents[e].comp_mask &= ~comp_bit;
}

int ecs_has(ecs_entity_t e, u32 mask)
{
    EcsEntity *ents;
    if (e < 0 || e >= ECS_MAX_ENTITIES) return 0;
    ents = ecs__get_entities();
    if (ents[e].alive != ECS_ALIVE) return 0;
    return (ents[e].comp_mask & mask) == mask;
}

/* ====================================================================== */
/*  API — 型付きアクセサ                                                   */
/* ====================================================================== */

CompTransform *ecs_get_transform(ecs_entity_t e)
{
    return &g_transform[e];
}

CompVelocity *ecs_get_velocity(ecs_entity_t e)
{
    return &g_velocity[e];
}

CompSprite *ecs_get_sprite(ecs_entity_t e)
{
    return &g_sprite[e];
}

CompCollider *ecs_get_collider(ecs_entity_t e)
{
    return &g_collider[e];
}

CompHealth *ecs_get_health(ecs_entity_t e)
{
    return &g_health[e];
}

CompChem *ecs_get_chem(ecs_entity_t e)
{
    return &g_chem[e];
}

CompAI *ecs_get_ai(ecs_entity_t e)
{
    return &g_ai[e];
}

CompTimer *ecs_get_timer(ecs_entity_t e)
{
    return &g_timer[e];
}

CompTag *ecs_get_tag(ecs_entity_t e)
{
    return &g_tag[e];
}

CompAnim *ecs_get_anim(ecs_entity_t e)
{
    return &g_anim[e];
}

/* ====================================================================== */
/*  API — カスタムコンポーネント登録                                       */
/* ====================================================================== */

int ecs_register_custom_comp(u32 comp_bit, const char *name)
{
    /* L2: システム予約ビットへの侵入防止 */
    if (comp_bit & COMP_SYSTEM_MASK) {
        return -1;
    }

    /* カスタム範囲外のビットがあれば拒否 */
    if ((comp_bit & COMP_CUSTOM_MASK) == 0) {
        return -1;
    }

    /* 二重登録チェック */
    if (comp_bit & g_registered_custom) {
        return -2;
    }

    /* 上限チェック */
    if (g_custom_count >= ECS_MAX_CUSTOM_COMP) {
        return -3;
    }

    g_registered_custom |= comp_bit;

#ifdef ECS_DEBUG
    g_custom_bits[g_custom_count] = comp_bit;
    g_custom_names[g_custom_count] = name;
#endif
    (void)name;  /* リリースビルドでの未使用警告抑制 */

    g_custom_count++;
    return 0;
}

int ecs_custom_comp_count(void)
{
    return g_custom_count;
}
