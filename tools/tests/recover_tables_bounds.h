/* Included in the product TU: test private range validation without mutants. */
static int rt_bounds(void)
{
    struct recover_layout_range ranges[] = {
        {0, 0, 0, 1, 0, 0, REC_TABLE_NONE8, 0, 0},
        {2, 3, 0, 1, 1, 0, REC_TABLE_NONE8, 0, 0},
        {0xffffffffU, 0xffffffffU, 0, 2, 3, 0, REC_TABLE_NONE8, 0, 0},
    };
    if (gf_layout_ranges_valid(ranges, 3, 4, 4)) return -1;
    if (!gf_layout_ranges_valid(ranges, 3, 4, 3)) return -1;
    if (!gf_layout_ranges_valid(ranges, 3, 5, 5)) return -1;
    ranges[0].ordinal = 1;
    if (!gf_layout_ranges_valid(ranges, 3, 4, 4)) return -1;
    ranges[0].ordinal = 0;
    ranges[1].ordinal = 0;
    if (!gf_layout_ranges_valid(ranges, 3, 4, 4)) return -1;
    ranges[1].ordinal = 1;
    ranges[1].last = 1;
    if (!gf_layout_ranges_valid(ranges, 3, 4, 4)) return -1;
    ranges[1].last = 0xffffffffU;
    if (!gf_layout_ranges_valid(ranges, 3, 4, 4)) return -1;
    ranges[0].last = 0xffffffffU;
    if (!gf_layout_ranges_valid(ranges, 1, 0xffffffffU, 0xffffffffU)) return -1;
    return 0;
}
