#ifndef __TILEMAP_INTERNAL_H
#define __TILEMAP_INTERNAL_H

#include "libos32tilemap.h"

typedef struct {
    KernelAPI *kapi;
    TileDef *tiles;       /* [MAX_TILES] */
    BGPlane *bg_planes;   /* [BG_COUNT] */
    int origin_x;
    int origin_y;
    /* 差分スクロール追跡 */
    i16 prev_scroll_x[BG_COUNT];
    i16 prev_scroll_y[BG_COUNT];
    u8  scroll_changed;   /* いずれかのBGでスクロール変化あり */
} TilemapState;

extern TilemapState _tilemap;

#endif /* __TILEMAP_INTERNAL_H */
