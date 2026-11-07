/* Fake module memory ownership; the real broker validates real declaration tables. */
#include "debug_probe.h"
#include "kstring.h"
struct module_record module_records[MODULE_RECORD_CAP];
struct module_record *module_record_get(struct module_handle h)
{
    return h.slot < 2 && h.generation == 1 ? &module_records[h.slot] : 0;
}
int module_ref_acquire(struct module_handle h, u32 owner,
                       enum module_ref_kind kind, u32 handler)
{
    struct module_record *r=module_record_get(h);
    (void)handler;
    if (!r || r->owner != owner || kind != MODULE_REF_REGISTRATION) return 0;
    ++r->registration_refs;
    return 1;
}
int module_ref_release(struct module_handle h, enum module_ref_kind kind)
{
    struct module_record *r=module_record_get(h);
    if (!r || !r->registration_refs || kind != MODULE_REF_REGISTRATION) return 0;
    --r->registration_refs;
    return 1;
}
int concrete_register(const struct probe_decl *decls, u32 count)
{
    u32 ro=(u32)(uptr)decls, end=ro+count*sizeof(*decls);
    u32 low=U32_MAX, high=0;
    for (u32 i=0;i<count;++i) {
        u32 name=(u32)(uptr)decls[i].name, slot=(u32)(uptr)decls[i].slot;
        if (name < ro) ro=name;
        if (name+kstrlen(decls[i].name)+1 > end) end=name+kstrlen(decls[i].name)+1;
        if (slot < low) low=slot;
        if (slot+sizeof(struct probe_slot) > high) high=slot+sizeof(struct probe_slot);
    }
    low &= ~4095U;
    module_records[0]=(struct module_record){.state=MODULE_RUNNING,.generation=1,
        .owner=1,.base=low,.backing=low,.pages=(high-low+4095)/4096,
        .ro=ro,.ro_size=end-ro};
    int rc=debug_probe_register((struct module_handle){0,1},decls,count);
    if (rc) return rc;
    struct probe_list list;
    rc=debug_probe_control(PROBE_LIST,&list,sizeof(list));
    if (rc || list.count != count) return -1;
    for (u32 i=0;i<count;++i)
        if (kstrcmp(list.points[i].name,decls[i].name) || list.points[i].enabled ||
            decls[i].slot->hit || decls[i].slot->id) return -1;
    return 0;
}
int concrete_arm(probe_hit_fn callback,u32 count)
{
    module_records[1]=(struct module_record){.state=MODULE_RUNNING,.generation=1,
        .owner=2,.module_id=MODULE_ID_KDEBUG,.text=(u32)(uptr)callback,.text_size=1};
    struct probe_attach a={.owner={1,1},.callback=callback,.count=count};
    for (u32 i=0;i<count;++i) a.ids[i]=i;
    return debug_probe_control(PROBE_ATTACH,&a,sizeof(a));
}
int concrete_stop(void)
{
    struct module_handle owner={1,1};
    return debug_probe_control(PROBE_DETACH,&owner,sizeof(owner));
}
