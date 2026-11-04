/* Host-only enc2-r3-supplement1 integer oracle port. C11, no libraries.
 * File protocol is versioned, little endian; Python owns PNG, A/B and OSRI.
 * Physical palette slots, candidate ties and finite search limits match S1.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#define SLOTS 16
#define COLORS 4096
#define MAX_W 640
#define MAX_H 400
#define MAX_T (MAX_W * 4)
#define SPILL_WEIGHT 8
#define HBLANK_NS 5900
#define OUT_NS 650
#define ACTIVE_NS 25900
#define MAX_OUT 47

static const int weights[4] = {8, 4, 2, 1};
static const int component_order[3] = {1, 0, 2};
typedef struct { uint16_t q[SLOTS]; } Palette;
typedef struct { uint8_t n, slot[SLOTS]; } Order;
typedef struct { int64_t score; Order order, next; int valid; } Value;
typedef struct { int h, w, budget, lookahead, iters, trips, moves, passes;
    int delay, offset, timed, dither, cuts[68]; } Config;
typedef struct { Palette p; Value v; uint64_t hash; } Entry;
typedef struct { int y; int64_t before, after; } Visit;
typedef struct {
    Config cfg;
    uint8_t *pixels, active[MAX_H];
    uint16_t master[256], candidates[MAX_H][SLOTS];
    int nm, nc[MAX_H];
    Palette roots, palettes[MAX_H];
    Order orders[MAX_H];
    FILE *diagnostic; int phase, seed;
    uint64_t evaluations;
    int64_t trace[65]; int nt;
    Visit *visits; size_t nv, cv;
} Image;
typedef struct {
    Image *im; int y, t;
    const Palette *old, *following, *retained;
    Order following_order, retained_order;
    int32_t comp[3][16][MAX_T];
    int next_ids[MAX_W]; int64_t next_ideal;
    Entry *cache; size_t capacity, used;
} Search;
typedef struct { int nearest[MAX_T], second_id[MAX_T];
    int32_t first[MAX_T], second[MAX_T]; } Neighbors;
typedef struct { int64_t base, table[SLOTS][65]; int count[SLOTS]; Order changed, initial; } Cost;

static void fail(const char *s) { fprintf(stderr, "enc2 core: %s\n", s); exit(1); }
static void *allocate(size_t n) { void *p = calloc(1, n); if (!p) fail("allocation"); return p; }
static uint32_t read32(FILE *f) {
    uint8_t b[4]; if (fread(b, 1, 4, f) != 4) fail("truncated input");
    return (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24;
}
static int signed32(FILE *f) { uint32_t v = read32(f); return v <= INT32_MAX ? (int)v : (int)((int64_t)v - INT64_C(4294967296)); }
static void write32(FILE *f, uint32_t v) {
    uint8_t b[4] = {v, v>>8, v>>16, v>>24};
    if (fwrite(b, 1, 4, f) != 4) fail("output");
}
static void write64(FILE *f, uint64_t v) { write32(f, (uint32_t)v); write32(f, (uint32_t)(v>>32)); }
static uint64_t now_ns(void) { struct timespec ts; if (!timespec_get(&ts, TIME_UTC)) fail("clock"); return (uint64_t)ts.tv_sec*1000000000 + ts.tv_nsec; }
static int channel(uint16_t q, int c) { return (q >> (8 - 4*c)) & 15; }
static int distance(const uint8_t *x, uint16_t q) {
    int r = x[0]-17*channel(q, 0), g=x[1]-17*channel(q, 1), b=x[2]-17*channel(q, 2);
    return r*r+g*g+b*b;
}
static int different(uint16_t a, uint16_t b) {
    return (channel(a,0)!=channel(b,0)) + (channel(a,1)!=channel(b,1)) + (channel(a,2)!=channel(b,2));
}
static int out_count(const Palette *a, const Palette *b) {
    int components=0, slots=0;
    for (int i=0; i<SLOTS; i++) { int n=different(a->q[i],b->q[i]); components+=n; slots+=(n!=0); }
    return components + (slots ? slots-1 : 0);
}
static int same(const Palette *a, const Palette *b) { return memcmp(a,b,sizeof(*a))==0; }
static int order_cmp(const Order *a, const Order *b) {
    int n=a->n<b->n?a->n:b->n;
    for (int i=0;i<n;i++) if(a->slot[i]!=b->slot[i]) return a->slot[i]<b->slot[i]?-1:1;
    return (a->n>b->n)-(a->n<b->n);
}
static int palette_cmp(const Palette *a,const Palette *b) {
    for(int i=0;i<SLOTS;i++) if(a->q[i]!=b->q[i]) return a->q[i]<b->q[i]?-1:1;
    return 0;
}
static int key_less(Search *s,const Palette *a,Value av,const Palette *b,Value bv) {
    if(av.score!=bv.score) return av.score<bv.score;
    int ao=s->old?out_count(s->old,a):0, bo=s->old?out_count(s->old,b):0;
    if(ao!=bo) return ao<bo;
    int pc=palette_cmp(a,b); return pc?pc<0:order_cmp(&av.order,&bv.order)<0;
}
static int cut(const Config *c,int j) {
    int64_t numerator=((int64_t)c->delay+j*OUT_NS-HBLANK_NS-c->offset)*c->w;
    int64_t x=numerator/ACTIVE_NS;
    if(numerator>0 && numerator%ACTIVE_NS) x++; /* ceil, including negative offsets */
    return x<0?0:x>c->w?c->w:(int)x;
}
static int nearest(const uint8_t *x,const Palette *p,int *squared) {
    int best=INT_MAX,id=0;
    for(int i=0;i<SLOTS;i++) { int d=distance(x,p->q[i]); if(d<best) {best=d;id=i;} }
    if(squared) *squared=best;
    return id;
}

/* For each changed slot, accumulate only pixels assigned to that slot.
 * Monotone completion positions build the same prefix-SSE table as S1. */
static void cost_init(Cost *cost,const uint8_t *pixels,const Palette *old,const Palette *p,
                      const int *ids,const Config *cfg) {
    memset(cost,0,sizeof(*cost));
    int positions[SLOTS][MAX_W], sizes[SLOTS]={0};
    for(int x=0;x<cfg->w;x++) {
        int i=ids[x]; positions[i][sizes[i]++]=x;
        cost->base+=distance(pixels+3*x,p->q[i]);
    }
    for(int i=0;i<SLOTS;i++) {
        int n=different(old->q[i],p->q[i]); cost->count[i]=n;
        if(!n) continue;
        cost->changed.slot[cost->changed.n++]=i;
        int rank=0;
        for(int k=0;k<3;k++) {
            int c=component_order[k], a=17*channel(old->q[i],c), b=17*channel(p->q[i],c);
            if(a==b) continue;
            rank++;
            int pos=0; int64_t sum=0;
            for(int preceding=0;preceding<65;preceding++) {
                int boundary=cfg->cuts[preceding+rank];
                while(pos<sizes[i] && positions[i][pos]<boundary) {
                    int v=pixels[3*positions[i][pos++]+c];
                    sum+=(int64_t)(v-a)*(v-a)-(int64_t)(v-b)*(v-b);
                }
                cost->table[i][preceding]+=sum;
            }
        }
    }
    cost->initial=cost->changed;
    /* Sort by (first use x, slot); unused slots use width. */
    for(int i=1;i<cost->initial.n;i++) {
        uint8_t slot=cost->initial.slot[i];
        int first=sizes[slot]?positions[slot][0]:cfg->w,j=i;
        while(j>0) {
            int other=cost->initial.slot[j-1], of=sizes[other]?positions[other][0]:cfg->w;
            if(of<first || (of==first && other<slot)) break;
            cost->initial.slot[j]=other;j--;
        }
        cost->initial.slot[j]=slot;
    }
}
static int64_t cost_score(const Cost *c,const Order *o) {
    int preceding=0; int64_t score=c->base;
    for(int k=0;k<o->n;k++) {
        int i=o->slot[k]; score+=c->table[i][preceding]; preceding+=c->count[i]+1;
    }
    return score;
}
static int64_t improve(const Cost *c,Order *o,const Config *cfg) {
    int64_t score=cost_score(c,o);
    for(int pass=0;pass<cfg->passes;pass++) {
        Order best=*o; int64_t bs=score;
        int preceding[SLOTS]={0};
        for(int k=1;k<o->n;k++) preceding[k]=preceding[k-1]+c->count[o->slot[k-1]]+1;
        for(int i=0;i<o->n;i++) for(int j=i+1;j<o->n;j++) {
            int a=o->slot[i],b=o->slot[j],shift=c->count[b]-c->count[a];
            int64_t v=score+c->table[b][preceding[i]]-c->table[a][preceding[i]];
            v+=c->table[a][preceding[j]+shift]-c->table[b][preceding[j]];
            for(int k=i+1;k<j;k++) {
                int slot=o->slot[k];
                v+=c->table[slot][preceding[k]+shift]-c->table[slot][preceding[k]];
            }
            if(v<=bs) {
                Order trial=*o;trial.slot[i]=b;trial.slot[j]=a;
                if(v<bs || order_cmp(&trial,&best)<0) {bs=v;best=trial;}
            }
        }
        if(bs>=score) break;
        score=bs;*o=best;
    }
    return score;
}
static int64_t optimize(const uint8_t *pixels,const Palette *old,const Palette *p,
                         const int *ids,const Config *cfg,const Order *retained,Order *result) {
    Cost cost; cost_init(&cost,pixels,old,p,ids,cfg);
    Order start=cost.initial;
    int64_t is=cost_score(&cost,&start), cs=cost_score(&cost,&cost.changed);
    if(cs<is || (cs==is && order_cmp(&cost.changed,&start)<0)) start=cost.changed;
    int64_t best=improve(&cost,&start,cfg); *result=start;
    if(retained) {
        Order current={0}; int present[SLOTS]={0};
        for(int i=0;i<retained->n;i++) {int slot=retained->slot[i];if(cost.count[slot]) {current.slot[current.n++]=slot;present[slot]=1;}}
        for(int i=0;i<cost.changed.n;i++) {int slot=cost.changed.slot[i];if(!present[slot]) current.slot[current.n++]=slot;}
        Order trial=current; int64_t original=cost_score(&cost,&current), v=improve(&cost,&trial,cfg);
        if(v<best || (v==best && order_cmp(&trial,result)<0)) {best=v;*result=trial;}
        if(original<=best) {best=original;*result=current;}
    }
    return best;
}
static uint64_t hash_palette(const Palette *p) {
    uint64_t h=UINT64_C(14695981039346656037);
    for(int i=0;i<SLOTS;i++) {h^=p->q[i];h*=UINT64_C(1099511628211);}
    return h?h:1;
}
static Entry *cache_entry(Search *s,const Palette *p,uint64_t hash) {
    size_t i=hash&(s->capacity-1);
    while(s->cache[i].hash && !(s->cache[i].hash==hash && same(&s->cache[i].p,p))) i=(i+1)&(s->capacity-1);
    return s->cache+i;
}
static void cache_grow(Search *s) {
    Entry *previous=s->cache; size_t n=s->capacity;
    s->capacity*=2;s->cache=allocate(s->capacity*sizeof(Entry));
    for(size_t i=0;i<n;i++) if(previous[i].hash) *cache_entry(s,&previous[i].p,previous[i].hash)=previous[i];
    free(previous);
}
static Search *search_new(Image *im,int y,const Palette *old,const Palette *following,
                          Order following_order,const Palette *retained,Order retained_order) {
    Search *s=allocate(sizeof(*s));s->im=im;s->y=y;s->old=old;s->following=following;
    s->following_order=following_order;s->retained=retained;s->retained_order=retained_order;
    int nr=im->cfg.h-y;if(nr>im->cfg.lookahead+1) nr=im->cfg.lookahead+1;
    s->t=nr*im->cfg.w;s->capacity=1024;s->cache=allocate(s->capacity*sizeof(Entry));
    const uint8_t *x=im->pixels+3*y*im->cfg.w;
    for(int c=0;c<3;c++) for(int q=0;q<16;q++) for(int t=0;t<s->t;t++) {int d=x[3*t+c]-q*17;s->comp[c][q][t]=d*d;}
    if(following) for(int i=0;i<im->cfg.w;i++) {
        int d;s->next_ids[i]=nearest(x+3*(im->cfg.w+i),following,&d);s->next_ideal+=d;
    }
    return s;
}
static void search_free(Search *s) {free(s->cache);free(s);}
static Value evaluate(Search *s,const Palette *p,const int32_t *minima,const int *indices) {
    uint64_t h=hash_palette(p);Entry *entry=cache_entry(s,p,h);
    if(entry->hash) return entry->v;
    s->im->evaluations++;
    Value value={0}; Config *cfg=&s->im->cfg;
    if((s->old && out_count(s->old,p)>cfg->budget) || (s->following && out_count(p,s->following)>cfg->budget)) goto save;
    int ids[MAX_W]; int64_t f=0,ideal=0;
    for(int t=0;t<s->t;t++) {
        int d=INT_MAX,id=0;
        if(minima) {d=minima[t];if(t<cfg->w) id=indices[t];}
        else for(int i=0;i<SLOTS;i++) {
            uint16_t q=p->q[i];int v=s->comp[0][channel(q,0)][t]+s->comp[1][channel(q,1)][t]+s->comp[2][channel(q,2)][t];
            if(v<d) {d=v;id=i;}
        }
        f+=(int64_t)d*weights[t/cfg->w];
        if(t<cfg->w) {ideal+=d;ids[t]=id;}
    }
    int64_t actual=ideal;
    if(s->old) {
        const Order *retained=s->retained && same(p,s->retained)?&s->retained_order:NULL;
        actual=optimize(s->im->pixels+3*s->y*cfg->w,s->old,p,ids,cfg,retained,&value.order);
    }
    value.score=f + SPILL_WEIGHT*(actual-ideal);
    if(s->following) {
        int64_t next=optimize(s->im->pixels+3*(s->y+1)*cfg->w,p,s->following,s->next_ids,cfg,&s->following_order,&value.next);
        value.score+=SPILL_WEIGHT*(next-s->next_ideal);
    }
    value.valid=1;
save:
    if((s->used+1)*10>s->capacity*7) cache_grow(s);
    entry=cache_entry(s,p,h);entry->hash=h;entry->p=*p;entry->v=value;s->used++;
    return value;
}
static void neighbors(Search *s,const Palette *p,Neighbors *n) {
    for(int t=0;t<s->t;t++) {
        int a=INT_MAX,b=INT_MAX,ai=0,bi=0;
        for(int i=0;i<SLOTS;i++) {
            uint16_t q=p->q[i];int d=s->comp[0][channel(q,0)][t]+s->comp[1][channel(q,1)][t]+s->comp[2][channel(q,2)][t];
            if(d<a) {b=a;bi=ai;a=d;ai=i;} else if(d<b) {b=d;bi=i;}
        }
        n->nearest[t]=ai;n->first[t]=a;n->second[t]=b;n->second_id[t]=bi;
    }
}
static Value evaluate_slot(Search *s,const Palette *p,int slot,uint16_t q,const Neighbors *n,Palette *trial) {
    *trial=*p;trial->q[slot]=q;
    /* Invalid palettes and cached palettes do not need another pixel scan. */
    uint64_t h=hash_palette(trial);Entry *e=cache_entry(s,trial,h);
    if(e->hash) return e->v;
    if((s->old && out_count(s->old,trial)>s->im->cfg.budget) ||
       (s->following && out_count(trial,s->following)>s->im->cfg.budget)) return evaluate(s,trial,NULL,NULL);
    int32_t minima[MAX_T];int indices[MAX_W];
    const int32_t *r=s->comp[0][channel(q,0)],*g=s->comp[1][channel(q,1)],*b=s->comp[2][channel(q,2)];
    for(int t=0;t<s->t;t++) {
        int without=n->nearest[t]==slot?n->second[t]:n->first[t];
        int other=n->nearest[t]==slot?n->second_id[t]:n->nearest[t];
        int candidate=r[t]+g[t]+b[t];
        minima[t]=candidate<without?candidate:without;
        if(t<s->im->cfg.w) indices[t]=(candidate<without || (candidate==without && slot<other))?slot:other;
    }
    return evaluate(s,trial,minima,indices);
}
typedef struct {uint16_t q;int64_t cost;} Proposal;
static int proposal_cmp(const void *a,const void *b) {
    const Proposal *x=a,*y=b;
    if(x->cost!=y->cost) return x->cost<y->cost?-1:1;
    return (x->q>y->q)-(x->q<y->q);
}
static int proposals(uint16_t current,const uint16_t *candidates,int nc,Proposal *out,int64_t n,const int64_t *sums) {
    uint8_t seen[COLORS]={0};
    for(int i=0;i<nc;i++) for(int mask=1;mask<8;mask++) {
        uint16_t q=0;
        for(int c=0;c<3;c++) q|=(uint16_t)(channel((mask&(1<<c))?candidates[i]:current,c)<<(8-4*c));
        if(q!=current) seen[q]=1;
    }
    int count=0;
    for(int q=0;q<COLORS;q++) if(seen[q]) {
        int64_t cost=0;
        if(sums) for(int c=0;c<3;c++) {int e=channel(q,c)*17;cost+=n*e*e-2*e*sums[c];}
        out[count++]=(Proposal){(uint16_t)q,cost};
    }
    if(sums) qsort(out,(size_t)count,sizeof(*out),proposal_cmp);
    return count;
}
static uint16_t centroid(int64_t n,const int64_t *sums) {
    uint16_t result=0;
    for(int c=0;c<3;c++) {
        int best=0;int64_t value=INT64_MAX;
        for(int q=0;q<16;q++) {int e=q*17;int64_t v=n*e*e-2*e*sums[c];if(v<value) {value=v;best=q;}}
        result|=(uint16_t)(best<<(8-4*c));
    }
    return result;
}
/* Optional host-only JSONL; never participates in candidate selection. */
static void trace_state(Search *s,const char *stage,int iteration,int slot,
                        const Palette *before,const Palette *after,int total,int valid,int up) {
    FILE *f=s->im->diagnostic;if(!f) return;
    Value a=evaluate(s,before,NULL,NULL),b=evaluate(s,after,NULL,NULL);
    Config *cfg=&s->im->cfg;
    int64_t weighted=0,ideal=0;int ids[MAX_W];
    for(int t=0;t<s->t;t++) {
        int d,id=nearest(s->im->pixels+3*(s->y*cfg->w+t),after,&d);
        weighted+=(int64_t)d*weights[t/cfg->w];
        if(t<cfg->w) {ideal+=d;ids[t]=id;}
    }
    int64_t actual=ideal;int k=0;
    if(s->old) {
        Cost cost;cost_init(&cost,s->im->pixels+3*s->y*cfg->w,s->old,after,ids,cfg);
        actual=cost_score(&cost,&b.order);
        int boundary[SLOTS][3]={{0}},j=0;
        for(int pos=0;pos<b.order.n;pos++) {
            int i=b.order.slot[pos];if(pos) j++;
            for(int c=0;c<3;c++) {int ch=component_order[c];
                if(channel(s->old->q[i],ch)!=channel(after->q[i],ch)) boundary[i][ch]=cut(cfg,++j);
            }
        }
        for(int x=0;x<cfg->w;x++) {
            int old=0;for(int c=0;c<3;c++) old|=x<boundary[ids[x]][c];
            k+=old;
        }
    }
    fprintf(f,"{\"phase\":%d,\"seed\":%d,\"y\":%d,\"stage\":\"%s\",\"iteration\":%d,\"slot\":%d,"
              "\"before\":%lld,\"after\":%lld,\"F\":%lld,\"delta\":%lld,\"next_delta_weighted\":%lld,"
              "\"OUT\":%d,\"next_OUT\":%d,\"K_plan\":%d,\"total\":%d,\"valid\":%d,\"out_up\":%d,\"palette\":[",
            s->im->phase,s->im->seed,s->y,stage,iteration,slot,(long long)a.score,(long long)b.score,
            (long long)weighted,(long long)(actual-ideal),(long long)(b.score-weighted-SPILL_WEIGHT*(actual-ideal)),
            s->old?out_count(s->old,after):0,s->following?out_count(after,s->following):0,k,total,valid,up);
    for(int i=0;i<SLOTS;i++) fprintf(f,"%s%u",i?",":"",after->q[i]);
    fputs("],\"order\":[",f);
    for(int i=0;i<b.order.n;i++) fprintf(f,"%s%d",i?",":"",b.order.slot[i]);
    fputs("]}\n",f);
    if(ferror(f)) fail("diagnostic output");
}
static Palette lloyd(Search *s,Palette p,const int *movable,int nm) {
    Config *cfg=&s->im->cfg;
    for(int iteration=0;iteration<cfg->iters;iteration++) {
        int changed=0;
        for(int k=0;k<nm;k++) {
            int slot=movable[k];int64_t n=0,sums[3]={0};
            const uint8_t *pixels=s->im->pixels+3*s->y*cfg->w;
            for(int t=0;t<s->t;t++) {
                int id=nearest(pixels+3*t,&p,NULL);
                if(id==slot) {int w=weights[t/cfg->w];n+=w;for(int c=0;c<3;c++) sums[c]+=(int64_t)pixels[3*t+c]*w;}
            }
            if(!n) continue;
            uint16_t candidates[257];memcpy(candidates,s->im->master,(size_t)s->im->nm*sizeof(uint16_t));
            candidates[s->im->nm]=centroid(n,sums);
            Proposal prop[COLORS];int np=proposals(p.q[slot],candidates,s->im->nm+1,prop,n,sums);
            Palette best=p;Value current=evaluate(s,&p,NULL,NULL),bv=current;
            Neighbors nb;neighbors(s,&p,&nb);
            for(int i=0;i<np;i++) {
                Palette trial;Value v=evaluate_slot(s,&p,slot,prop[i].q,&nb,&trial);
                if(v.valid && v.score<current.score && key_less(s,&trial,v,&best,bv)) {best=trial;bv=v;}
            }
            if(!same(&best,&p)) {trace_state(s,"lloyd",iteration,slot,&p,&best,np,0,0);p=best;changed=1;}
        }
        if(!changed) {trace_state(s,"lloyd-stationary",iteration,-1,&p,&p,0,0,0);break;}
    }
    return p;
}
static int search(Search *s,const Palette *seed,int initial,Palette *result) {
    Palette p=*seed;Value current=evaluate(s,&p,NULL,NULL);
    if(!current.valid) return 0;
    trace_state(s,"seed",0,-1,&p,&p,0,0,0);
    if(initial) {int all[SLOTS];for(int i=0;i<SLOTS;i++) all[i]=i;*result=lloyd(s,p,all,SLOTS);return 1;}
    for(int move=0;move<s->im->cfg.moves;move++) {
        Palette best=p;Value bv=evaluate(s,&p,NULL,NULL);current=bv;
        Neighbors nb;neighbors(s,&p,&nb);
        int total=0,valid=0,up=0;
        for(int slot=0;slot<SLOTS;slot++) {
            Proposal prop[COLORS];int np=proposals(p.q[slot],s->im->candidates[s->y],s->im->nc[s->y],prop,0,NULL);
            for(int i=0;i<np;i++) {
                Palette trial;Value v=evaluate_slot(s,&p,slot,prop[i].q,&nb,&trial);
                if(s->im->diagnostic) {
                    total++;valid+=v.valid;
                    up+=v.valid && out_count(s->old,&trial)>out_count(s->old,&p);
                }
                if(v.valid && v.score<current.score && key_less(s,&trial,v,&best,bv)) {best=trial;bv=v;}
            }
        }
        trace_state(s,same(&best,&p)?"exchange-stationary":"exchange",move,-1,&p,&best,total,valid,up);
        if(same(&best,&p)) break;
        p=best;
    }
    /* C2 exhausts tree exchanges before C4/C5 refine the final changed set.
     * Also refine a retained seed when no tree exchange improves it. */
    int movable[SLOTS],nm=0;
    for(int i=0;i<SLOTS;i++) if(p.q[i]!=s->old->q[i]) movable[nm++]=i;
    *result=lloyd(s,p,movable,nm);
    trace_state(s,"search-final",0,-1,&p,result,0,0,0);
    return 1;
}
static int64_t objective(Image *im) {
    int64_t total=0;Config *cfg=&im->cfg;
    for(int y=0;y<cfg->h;y++) {
        int nr=cfg->h-y;if(nr>cfg->lookahead+1) nr=cfg->lookahead+1;
        int ids[MAX_W];int64_t ideal=0;
        for(int row=0;row<nr;row++) for(int x=0;x<cfg->w;x++) {
            int d,id=nearest(im->pixels+3*((y+row)*cfg->w+x),&im->palettes[y],&d);
            total+=(int64_t)d*weights[row];if(!row) {ids[x]=id;ideal+=d;}
        }
        if(y) {
            Cost cost;cost_init(&cost,im->pixels+3*y*cfg->w,&im->palettes[y-1],&im->palettes[y],ids,cfg);
            total+=SPILL_WEIGHT*(cost_score(&cost,&im->orders[y])-ideal);
        }
    }
    return total;
}
static void add_visit(Image *im,int y,int64_t before,int64_t after) {
    if(im->nv==im->cv) {
        im->cv=im->cv?im->cv*2:128;Visit *p=realloc(im->visits,im->cv*sizeof(Visit));if(!p) fail("visits");im->visits=p;
    }
    im->visits[im->nv++]=(Visit){y,before,after};
}
static void write_palette(FILE *f,const Palette *p) {for(int i=0;i<SLOTS;i++) write32(f,p->q[i]);}
static void write_order(FILE *f,const Order *o) {write32(f,o->n);for(int i=0;i<o->n;i++) write32(f,o->slot[i]);}
static Palette read_palette(FILE *f) {
    Palette p;
    for(int i=0;i<SLOTS;i++) {uint32_t q=read32(f);if(q>=COLORS) fail("palette code");p.q[i]=(uint16_t)q;}
    return p;
}
static Order read_order(FILE *f,const Palette *old,const Palette *p) {
    Order o={0};uint32_t n=read32(f);if(n>SLOTS) fail("order length");o.n=n;int seen[SLOTS]={0};
    for(int i=0;i<o.n;i++) {uint32_t slot=read32(f);if(slot>=SLOTS || seen[slot]++) fail("order slot");o.slot[i]=slot;}
    for(int i=0;i<SLOTS;i++) if(seen[i]!=(old && old->q[i]!=p->q[i])) fail("changed slots");
    return o;
}
static void plan(Image *im,FILE *out) {
    Config *cfg=&im->cfg;uint64_t start=now_ns();Order empty={0};
    Search *s=search_new(im,0,NULL,NULL,empty,NULL,empty);
    search(s,&im->roots,1,&im->palettes[0]);search_free(s);
    for(int y=1;y<cfg->h;y++) {
        im->palettes[y]=im->palettes[y-1];
        if(im->active[y]) {
            s=search_new(im,y,&im->palettes[y-1],NULL,empty,NULL,empty);
            search(s,&im->palettes[y-1],0,&im->palettes[y]);im->orders[y]=evaluate(s,&im->palettes[y],NULL,NULL).order;
            trace_state(s,"forward",0,-1,&im->palettes[y-1],&im->palettes[y],0,0,0);search_free(s);
        }
        if(y%25==0) fprintf(stderr,"enc2 B%d forward %d/%d\n",cfg->budget,y,cfg->h-1);
    }
    uint64_t forward=now_ns()-start;start=now_ns();im->trace[im->nt++]=objective(im);
    for(int trip=0;trip<cfg->trips;trip++) {
        int changes=0;
        for(int direction=0;direction<2;direction++) for(int step=0;step<cfg->h;step++) {
            int y=direction?step:cfg->h-1-step;if(y && !im->active[y]) continue;
            const Palette *old=y?&im->palettes[y-1]:NULL,*following=y+1<cfg->h?&im->palettes[y+1]:NULL;
            Order next=following?im->orders[y+1]:empty;
            Palette current=im->palettes[y];im->phase=1+2*trip+direction;
            s=search_new(im,y,old,following,next,&current,im->orders[y]);
            Value value=evaluate(s,&current,NULL,NULL);int64_t incumbent=value.score;
            int ids[MAX_W];
            if(y) {
                for(int x=0;x<cfg->w;x++) ids[x]=nearest(im->pixels+3*(y*cfg->w+x),&current,NULL);
                Cost cost;cost_init(&cost,im->pixels+3*y*cfg->w,old,&current,ids,cfg);
                incumbent+=SPILL_WEIGHT*(cost_score(&cost,&im->orders[y])-cost_score(&cost,&value.order));
            }
            if(following) {
                Cost cost;cost_init(&cost,im->pixels+3*(y+1)*cfg->w,&current,following,s->next_ids,cfg);
                incumbent+=SPILL_WEIGHT*(cost_score(&cost,&next)-cost_score(&cost,&value.next));
            }
            Palette best=current;Value bv=value;
            const Palette *seeds[2]={y?old:&im->roots,&current};
            for(int i=0;i<2;i++) {
                im->seed=i;Palette trial;if(!search(s,seeds[i],y==0,&trial)) continue;
                Value v=evaluate(s,&trial,NULL,NULL);
                if(key_less(s,&trial,v,&best,bv)) {best=trial;bv=v;}
            }
            if(bv.score<incumbent) {
                im->palettes[y]=best;im->orders[y]=bv.order;if(following) im->orders[y+1]=bv.next;
                changes++;add_visit(im,y,incumbent,bv.score);
            }
            if(im->diagnostic) fprintf(im->diagnostic,
                "{\"phase\":%d,\"y\":%d,\"stage\":\"visit-decision\",\"J_before\":%lld,\"J_after\":%lld,\"accepted\":%d}\n",
                im->phase,y,(long long)incumbent,
                (long long)(bv.score<incumbent?bv.score:incumbent),bv.score<incumbent);
            trace_state(s,"visit",0,-1,&current,&im->palettes[y],0,0,0);
            search_free(s);
            if(y%25==0) fprintf(stderr,"enc2 B%d trip %d direction %d row %d\n",cfg->budget,trip+1,direction,y);
        }
        im->trace[im->nt++]=objective(im);if(!changes) break;
    }
    uint64_t sweep=now_ns()-start;
    for(int y=0;y<cfg->h;y++) write_palette(out,&im->palettes[y]);
    for(int y=0;y<cfg->h;y++) write_order(out,&im->orders[y]);
    write32(out,im->nt);for(int i=0;i<im->nt;i++) write64(out,(uint64_t)im->trace[i]);
    write32(out,(uint32_t)im->nv);for(size_t i=0;i<im->nv;i++) {write32(out,im->visits[i].y);write64(out,(uint64_t)im->visits[i].before);write64(out,(uint64_t)im->visits[i].after);}
    write64(out,im->evaluations);write64(out,forward);write64(out,sweep);
}
static void quantize(Image *im,FILE *out) {
    Config *cfg=&im->cfg;int error[MAX_W+2][3]={0},nxt[MAX_W+2][3];
    for(int y=0;y<cfg->h;y++) {
        int boundaries[SLOTS][3]={{0}};int j=0;
        const Palette *p=&im->palettes[y],*old=y?&im->palettes[y-1]:p;
        if(y && cfg->timed) for(int pos=0;pos<im->orders[y].n;pos++) {
            int slot=im->orders[y].slot[pos];if(pos) j++;
            for(int k=0;k<3;k++) {int c=component_order[k];if(channel(old->q[slot],c)!=channel(p->q[slot],c)) boundaries[slot][c]=cut(cfg,++j);}
        }
        memset(nxt,0,sizeof(nxt));
        for(int x=0;x<cfg->w;x++) {
            int rgb[3];for(int c=0;c<3;c++) {
                int v=im->pixels[3*(y*cfg->w+x)+c]+(cfg->dither?error[x+1][c]:0);rgb[c]=v<0?0:v>255?255:v;
            }
            int best=INT_MAX,id=0,visible[SLOTS][3];
            for(int i=0;i<SLOTS;i++) {
                int d=0;
                for(int c=0;c<3;c++) {visible[i][c]=17*channel(x<boundaries[i][c]?old->q[i]:p->q[i],c);int delta=rgb[c]-visible[i][c];d+=delta*delta;}
                if(d<best) {best=d;id=i;}
            }
            if(fputc(id,out)==EOF) fail("indices output");
            if(cfg->dither) for(int c=0;c<3;c++) {
                int e=rgb[c]-visible[id][c];
                error[x+2][c]+=e*7/16; /* C11 signed division truncates toward zero. */
                nxt[x][c]+=e*3/16;nxt[x+1][c]+=e*5/16;nxt[x+2][c]+=e/16;
            }
        }
        memcpy(error,nxt,sizeof(error));
    }
}
int main(int argc,char **argv) {
    if(argc!=3 && argc!=4) fail("usage: osri-enc2-core INPUT OUTPUT [TRACE_JSONL]");
    FILE *in=fopen(argv[1],"rb");if(!in) fail("open input");
    if(read32(in)!=UINT32_C(0x43324345) || read32(in)!=1) fail("protocol version");
    uint32_t mode=read32(in);if(mode>1) fail("mode");
    Image *im=allocate(sizeof(*im));Config *cfg=&im->cfg;
    if(argc==4) {im->diagnostic=fopen(argv[3],"w");if(!im->diagnostic) fail("open diagnostic");}
    cfg->h=signed32(in);cfg->w=signed32(in);cfg->budget=signed32(in);
    cfg->lookahead=signed32(in);cfg->iters=signed32(in);cfg->trips=signed32(in);cfg->moves=signed32(in);cfg->passes=signed32(in);
    cfg->delay=signed32(in);cfg->offset=signed32(in);cfg->timed=signed32(in);cfg->dither=signed32(in);
    if(cfg->h<1 || cfg->h>MAX_H || cfg->w<1 || cfg->w>MAX_W || cfg->budget<0 || cfg->budget>MAX_OUT ||
       cfg->lookahead<0 || cfg->lookahead>3 || cfg->iters<0 || cfg->trips<0 || cfg->trips>64 || cfg->moves<0 || cfg->passes<0 ||
       cfg->delay < -100000000 || cfg->delay>100000000 || cfg->offset < -100000000 || cfg->offset>100000000) fail("configuration");
    for(int j=0;j<68;j++) cfg->cuts[j]=cut(cfg,j);
    size_t np=(size_t)cfg->h*cfg->w*3;im->pixels=allocate(np);if(fread(im->pixels,1,np,in)!=np) fail("pixels");
    if(!mode) {
        uint32_t nm=read32(in);if(!nm || nm>256) fail("master length");im->nm=nm;
        for(int i=0;i<im->nm;i++) {uint32_t q=read32(in);if(q>=COLORS) fail("master code");im->master[i]=q;}
        im->roots=read_palette(in);
        if(fread(im->active,1,cfg->h,in)!=(size_t)cfg->h) fail("active rows");
        for(int y=0;y<cfg->h;y++) {
            uint32_t n=read32(in);if(n>SLOTS) fail("candidates length");im->nc[y]=n;
            if((!y || im->active[y]) && !n) fail("missing candidates");
            for(int i=0;i<im->nc[y];i++) {uint32_t q=read32(in);if(q>=COLORS) fail("candidate code");im->candidates[y][i]=q;}
        }
    } else {
        for(int y=0;y<cfg->h;y++) im->palettes[y]=read_palette(in);
        for(int y=0;y<cfg->h;y++) im->orders[y]=read_order(in,y?&im->palettes[y-1]:NULL,&im->palettes[y]);
    }
    if(fgetc(in)!=EOF) fail("trailing input");
    fclose(in);
    FILE *out=fopen(argv[2],"wb");if(!out) fail("open output");
    write32(out,UINT32_C(0x52324345));write32(out,1);write32(out,mode);
    if(mode) quantize(im,out);else plan(im,out);
    if(fclose(out)) fail("close output");
    if(im->diagnostic && fclose(im->diagnostic)) fail("close diagnostic");
    free(im->pixels);free(im->visits);free(im);return 0;
}
