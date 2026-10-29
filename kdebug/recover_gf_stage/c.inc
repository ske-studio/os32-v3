static int mode_vram_access(struct recover *r)
{
    const struct recover_plan_node *n = &r->plan->nodes[r->cursor];
    const struct recover_bar_extent *b = &r->bars[GF_MODE_VRAM_BAR];
    if (r->state != REC_RUNNING || !r->valid || r->accessed || !r->ops->mode_vram ||
        !node_ok(r->profile,r->plan,r->cursor) || !b->valid || b->length < GF_MODE_VRAM_BYTES ||
        (unsigned long long)b->base + GF_MODE_VRAM_BYTES > RECOVER_ADDRESS_END ||
        !raw_matches(r,true)) return access_error(r,r->cursor);
    u32 dir = r->plan->args[n->arg_first + 3].value;
    u32 bad_offset = RECOVER_NONE, expected = RECOVER_NONE, actual = RECOVER_NONE;
    if (mode_vram_record(r,"BEGIN",dir,bad_offset,expected,actual,0) || recover_safe_point(r))
        return access_error(r,r->cursor);
    if (dir == REC_WRITE) r->gpu_written = true;
    int rc = r->ops->mode_vram(r->opaque,r,dir == REC_WRITE,&bad_offset,&expected,&actual);
    if (r->state != REC_RUNNING) return -1; /* safe point already cleaned up */
    bool mismatch = !rc && bad_offset != RECOVER_NONE;
    int log = mode_vram_record(r,"END",dir,bad_offset,expected,actual,rc ? rc : mismatch ? -1 : 0);
    if (rc || (mismatch && !r->verify_record) || log) {
        fail(r,log ? REC_LOG : rc ? REC_BUS : REC_VERIFY,
             REC_PHASE_VERIFY,bad_offset);
        return access_error(r,bad_offset);
    }
    r->accessed = true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
static int mode_color_record(struct recover *r, const char *event, u32 row, int rc)
{
    if (!r->transport) return 0;
    struct rec_row b;
    row_start(r,&b,"gf_bus",event);
    row_number(&b," pc=",RECOVER_NONE);
    row_number(&b," bar=",GF_MODE_REGISTER_BAR);
    row_number(&b," row=",row);
    row_number(&b," off=",row == GF_MODE_ROW_DAC ? GF_MODE_DAC_MASK : GF_MODE_AR_PORT);
    row_number(&b," len=",row == GF_MODE_ROW_DAC ? GF_MODE_DAC_BYTES : 1U);
    row_number(&b," dir=",REC_WRITE);
    if (event[0]=='E') {
        row_text(&b," rc="); if (rc<0) row_text(&b,"-");
        row_number(&b,"",rc<0 ? 0U-(u32)rc : (u32)rc);
    }
    return row_send(r,&b);
}
static int mode_color_access(struct recover *r)
{
    const struct recover_plan_node *n=&r->plan->nodes[r->cursor];
    const struct recover_bar_extent *b=&r->bars[GF_MODE_REGISTER_BAR];
    if (r->state != REC_RUNNING || !r->valid || r->accessed || !r->ops->mode_color ||
        !node_ok(r->profile,r->plan,r->cursor) || !b->valid || b->length<=GF_MODE_DAC_DATA ||
        (unsigned long long)b->base+GF_MODE_DAC_DATA>=RECOVER_ADDRESS_END || !raw_matches(r,true))
        return access_error(r,r->cursor);
    u32 row=r->plan->args[n->arg_first].value, value=r->plan->args[n->arg_first+1].value;
    if (mode_color_record(r,"BEGIN",row,0) || recover_safe_point(r)) return access_error(r,r->cursor);
    r->gpu_written=true;
    int rc=r->ops->mode_color(r->opaque,r,row,value);
    if (r->state != REC_RUNNING) return -1;
    int log=mode_color_record(r,"END",row,rc);
    if (rc || log) {
        fail(r,log ? REC_LOG : REC_BUS,REC_PHASE_VERIFY,row);
        return access_error(r,row);
    }
    r->accessed=true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
static int mode_check_record(struct recover *r,const char *phase,u32 source,u32 key,
                              u32 mask,u32 expected,u32 actual,int rc)
{
    if (!r->transport) return 0;
    struct rec_row row;
    row_start(r,&row,phase,"END");
    row_number(&row," source=",source); row_number(&row," key=",key);
    row_number(&row," mask=",mask); row_number(&row," expected=",expected);
    row_number(&row," actual=",actual);
    row_text(&row,rc ? " rc=-1" : " rc=0");
    return row_send(r,&row);
}
/* Used by the hardware callback too: 01 is valid only while executing
 * this already-entered, fixed C route operation. S0 still writes only 00. */
bool recover_mode_route_active(const struct recover *r)
{
    return r && r->state==REC_RUNNING && r->valid && r->entered && !r->accessed &&
        r->plan && r->cursor<r->plan->node_count &&
        r->node_layout[r->cursor]->flags==REC_LAYOUT_MODE_ROUTE &&
        node_ok(r->profile,r->plan,r->cursor);
}
static int mode_route_access(struct recover *r)
{
    if (!recover_mode_route_active(r) || !r->ops->route_write || !r->ops->route_read)
        return access_error(r,r->cursor);
    if (recover_record(r,"route","BEGIN",r->cursor,RECOVER_ROUTE_PORT,0,0) ||
        recover_safe_point(r)) return access_error(r,r->cursor);
    u32 actual=RECOVER_NONE;
    int rc=r->ops->route_write(r->opaque,RECOVER_ROUTE_RGB_IN);
    if (!rc && !recover_safe_point(r)) rc=r->ops->route_read(r->opaque,&actual);
    if (r->state!=REC_RUNNING) return -1;
    bool mismatch=!rc && ((actual ^ RECOVER_ROUTE_RGB_IN) & RECOVER_ROUTE_MASK);
    int log=mode_check_record(r,"route",REC_CHECK_ROUTE,RECOVER_NONE,
        RECOVER_ROUTE_MASK,RECOVER_ROUTE_RGB_IN,actual,rc || mismatch);
    if (rc || mismatch || log) {
        fail(r,log ? REC_LOG : rc ? REC_BUS : REC_VERIFY,REC_PHASE_VERIFY,RECOVER_ROUTE_PORT);
        return access_error(r,RECOVER_ROUTE_PORT);
    }
    r->accessed=true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
/* One place for C's read, masked comparison, evidence and failure exit.
 * Route and cell use fixed callbacks; no route I/O target is added. */
static int mode_check_access(struct recover *r, u32 *actual)
{
    const struct recover_plan_node *n=&r->plan->nodes[r->cursor];
    const struct recover_plan_arg *a=&r->plan->args[n->arg_first];
    if (r->state != REC_RUNNING || !r->valid || r->accessed ||
        !node_ok(r->profile,r->plan,r->cursor)) return access_error(r,r->cursor);
    *actual=RECOVER_NONE;
    u32 source=a[REC_CHECK_SOURCE].value, key=a[REC_CHECK_KEY].value;
    u32 mask=a[REC_CHECK_MASK].value, expected=a[REC_CHECK_EXPECTED].value;
    if (a[REC_CHECK_EXPECTED].kind == REC_ARG_SAVED) {
        if (expected < r->profile->count) expected=r->saved[expected];
        else {
            u32 slot=expected-r->profile->count;
            if (!(r->capture_valid & (1ULL << slot))) return access_error(r,key);
            expected=r->capture_values[slot];
        }
    }
    expected &= a[REC_CHECK_KEEP].value;
    if (recover_safe_point(r)) return -1;
    int rc;
    if (source == REC_CHECK_IDENTITY) {
        struct recover_identity id;
        rc=r->ops->identity(r->opaque,&id);
        if (!rc) *actual=same_identity(&id,&r->token.identity);
    } else if (source == REC_CHECK_ROUTE) rc=r->ops->route_read(r->opaque,actual);
    else if (source == REC_CHECK_CELL) rc=r->ops->mode_cell ? r->ops->mode_cell(r->opaque,r,actual) : -1;
    else {
        const struct recover_target *t=&r->profile->targets[key];
        if (t->space == REC_VGA_INDEXED && r->port_selected[r->index_port[key]] != key)
            return access_error(r,key);
        rc=bus_read(r,key,actual);
    }
    if (r->state != REC_RUNNING) return -1;
    bool mismatch=!rc && ((*actual ^ expected) & mask);
    int log=mode_check_record(r,"check",source,key,mask,expected,*actual,rc || mismatch);
    bool required = !r->verify_record || (n->flags & REC_NODE_CHECK_ENTRY) ||
        source != REC_CHECK_TARGET;
    if (rc || (mismatch && required) || log) {
        fail(r,log ? REC_LOG : rc ? REC_BUS : REC_VERIFY,REC_PHASE_VERIFY,key);
        return access_error(r,key);
    }
    for (u32 i=0;i<r->plan->capture_count;i++)
        if (r->plan->captures[i].consumer_node == r->cursor) r->capture_valid &= ~(1ULL << i);
    r->accessed=true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
static int mode_wait_access(struct recover *r)
{
    if (r->state != REC_RUNNING || !r->plan || !r->entered || r->accessed ||
        r->cursor >= r->plan->node_count ||
        r->node_layout[r->cursor]->flags != REC_LAYOUT_MODE_WAIT ||
        !node_ok(r->profile,r->plan,r->cursor)) return -1;
    u32 ms=r->plan->args[r->plan->nodes[r->cursor].arg_first].value;
    if (recover_record(r,"wait","BEGIN",r->cursor,ms,0,0)) return access_error(r,r->cursor);
    u32 tick=r->ops->ticks(r->opaque);
    int rc=recover_delay_ms(r,ms,r->ops->mode_wait,r->opaque);
    if (r->state != REC_RUNNING) return -1;
    int log=recover_record(r,"wait","END",r->cursor,ms,rc,r->ops->ticks(r->opaque)-tick);
    if (rc || log) return access_error(r,r->cursor);
    r->accessed=true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
