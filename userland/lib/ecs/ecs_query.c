/* ======================================================================== */
/*  ECS_QUERY.C — Entity クエリ (マスク/タグ/矩形範囲検索)                 */
/* ======================================================================== */

#include "libos32ecs.h"

/* ====================================================================== */
/*  外部参照                                                               */
/* ====================================================================== */

extern EcsEntity *ecs__get_entities(void);

/* ====================================================================== */
/*  ecs_query — コンポーネントマスクで Entity を列挙                       */
/* ====================================================================== */

int ecs_query(u32 mask, ecs_entity_t *out, int max_count)
{
    int i, found = 0;
    EcsEntity *ents = ecs__get_entities();

    for (i = 0; i < ECS_MAX_ENTITIES && found < max_count; i++) {
        if (ents[i].alive != ECS_ALIVE) continue;
        if ((ents[i].comp_mask & mask) == mask) {
            out[found] = (ecs_entity_t)i;
            found++;
        }
    }
    return found;
}

/* ====================================================================== */
/*  ecs_find_by_tag — タグで最初のEntityを検索                             */
/* ====================================================================== */

ecs_entity_t ecs_find_by_tag(u16 tag)
{
    int i;
    EcsEntity *ents = ecs__get_entities();

    for (i = 0; i < ECS_MAX_ENTITIES; i++) {
        if (ents[i].alive != ECS_ALIVE) continue;
        if ((ents[i].comp_mask & COMP_TAG) == 0) continue;
        if (ecs_get_tag((ecs_entity_t)i)->tag == tag) {
            return (ecs_entity_t)i;
        }
    }
    return ECS_INVALID;
}

/* ====================================================================== */
/*  ecs_find_in_rect — 矩形範囲内のEntity列挙                             */
/* ====================================================================== */

int ecs_find_in_rect(i16 x, i16 y, i16 w, i16 h,
                     ecs_entity_t *out, int max_count)
{
    int i, found = 0;
    i16 x2, y2;
    EcsEntity *ents = ecs__get_entities();

    x2 = x + w;
    y2 = y + h;

    for (i = 0; i < ECS_MAX_ENTITIES && found < max_count; i++) {
        CompTransform *t;
        if (ents[i].alive != ECS_ALIVE) continue;
        if ((ents[i].comp_mask & COMP_TRANSFORM) == 0) continue;

        t = ecs_get_transform((ecs_entity_t)i);
        if (t->tile_x >= x && t->tile_x < x2 &&
            t->tile_y >= y && t->tile_y < y2) {
            out[found] = (ecs_entity_t)i;
            found++;
        }
    }
    return found;
}
