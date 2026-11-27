    /* Pair the POINT that consumes an interrupt with the next real POINT. */
    u32 index = hw_points[0];
    if (index < 8 && (pending || (index & 1))) {
        hw_points[index + 1] = !!pending;
        hw_points[0] = index + 1;
    }
