/* Compile-only placeholder. Geometry is unvalidated: DO NOT RUN with this
 * provider. Real RAM/device ranges must be established before MM connection. */
#ifndef POWERMAC_PLATFORM_MEMORY_DESC_H
#define POWERMAC_PLATFORM_MEMORY_DESC_H

static const struct platform_memory_geometry platform_memory_geometry = {
    0, 0, 0, 0, 0
};

static const struct platform_memory_attributes platform_memory_attributes = {
    { {0, 0}, {0, 0}, {0, 0} },
    {0, 0},
    {0, 0}
};

#endif /* POWERMAC_PLATFORM_MEMORY_DESC_H */
