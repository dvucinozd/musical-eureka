#include "sf2_validate.h"
#include <string.h>
#include <limits.h>
#define MIB (1024U*1024U)
typedef struct { const uint8_t *p; uint32_t bytes, count; } table;
typedef struct { table ph,pb,pm,pg,in,ib,im,ig,sh,sm,sm24; unsigned lists,version,unsupported; } hydra;
typedef struct { int64_t start,end,ls,le; int lo,hi,vl,vh,root,mode; } zone;
typedef struct { uint8_t lo,hi,vl,vh; } rect;
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | ((uint32_t)u16(p+2)<<16); }
static int32_t s16(const uint8_t *p) { uint16_t x=u16(p); return x < 32768 ? x : (int32_t)x-65536; }
static sf2_result_t fail(sf2_info_t *i, sf2_result_t r, const char *why, size_t off) { i->reason=why;i->error_offset=off;return r; }
#define BAD(why) return fail(info,SF2_INVALID,why,0)
#define LIMIT(why) return fail(info,SF2_LIMIT,why,0)
static bool set_table(table *t,const uint8_t *p,uint32_t n,uint32_t record) {
    if(t->p || !n || n%record) return false;
    *t=(table){p,n,n/record}; return true;
}
static bool collect(hydra *h,const uint8_t *p,uint32_t n,int kind) {
    if(kind==3 && !memcmp(p,"ifil",4)) {
        if(h->version || n!=4)return false;
        if(u16(p+8)!=2 || u16(p+10)>4)h->unsupported=1;
        h->version=1;
    }
    if(kind==1) {
#define T(id,field,rec) if(!memcmp(p,id,4)) return set_table(&h->field,p+8,n,rec)
        T("phdr",ph,38); T("pbag",pb,4); T("pmod",pm,10); T("pgen",pg,4);
        T("inst",in,22); T("ibag",ib,4); T("imod",im,10); T("igen",ig,4); T("shdr",sh,46);
#undef T
        return false; /* pdta schema is fixed, unlike extensible INFO. */
    }
    if(kind==2) {
        if(!memcmp(p,"smpl",4)) return set_table(&h->sm,p+8,n,2);
        if(!memcmp(p,"sm24",4)) return set_table(&h->sm24,p+8,n,1);
        return false;
    }
    return true;
}
static bool walk(hydra *h,const uint8_t *base,size_t start,size_t end,int kind) {
    size_t pos=start;
    while(pos<end) {
        if(end-pos<8) return false;
        uint32_t n=u32(base+pos+4);size_t room=end-pos-8;
        if(n>room || (n&1U)>room-n) return false;
        if(!memcmp(base+pos,"LIST",4)) {
            if(kind || n<4) return false;
            int k=!memcmp(base+pos+8,"pdta",4)?1:!memcmp(base+pos+8,"sdta",4)?2:3;
            if(k==3 && memcmp(base+pos+8,"INFO",4))return false;
            if(h->lists&(1U<<k))return false;
            h->lists|=1U<<k;
            if(!walk(h,base,pos+12,pos+8+n,k)) return false;
        } else if(!kind || !collect(h,base+pos,n,kind)) return false;
        pos+=8+(size_t)n+(n&1U);
    }
    return pos==end;
}
static bool index_table(table t,unsigned stride,unsigned off,uint32_t end) {
    uint32_t last=0;
    for(uint32_t j=0;j<t.count;j++) { uint32_t x=u16(t.p+j*stride+off);if(x<last||x>=end)return false;last=x; }
    return last==end-1;
}
static bool zero_record(const uint8_t *p,unsigned n) { while(n--) if(*p++)return false; return true; }
static zone initial(void) { return (zone){0,0,0,0,0,127,0,127,-1,0}; }
/* Preset amounts are offsets added to the instrument value. TSF clamps the
   merged sum for these generators, so any int16 preset offset is safe. Tuning
   (51/52/56) and key scaling (31/32/39/40) keep their bounded windows. */
static bool merged_clamp(unsigned op) {
    return (op>=5&&op<=11)||op==13||op==17||(op>=21&&op<=30)||(op>=33&&op<=38)||op==48;
}
/* Return last zone link, -1 for global, -2 for malformed/unsupported zone. */
static int parse_zone(table gen,uint32_t a,uint32_t b,bool preset,zone *z,uint32_t *warnings,uint32_t *fuel) {
    uint32_t cost=b-a+1;
    if(cost>*fuel)return -3;
    *fuel-=cost;
    uint64_t seen=0;int link=-1;
    for(uint32_t j=a;j<b;j++) {
        const uint8_t *p=gen.p+j*4;unsigned op=u16(p);int val=s16(p+2);
        if(op>58 || (seen & (UINT64_C(1)<<op))) return -2;
        seen|=UINT64_C(1)<<op;
        if(op==41 || op==53) {
            if((preset && op!=41)||(!preset && op!=53)||j+1!=b) return -2;
            link=u16(p+2);continue;
        }
        if(op==43||op==44) {
            int lo=p[2],hi=p[3];if(lo>hi||hi>127)return -2;
            if(op==43){z->lo=lo;z->hi=hi;}else{z->vl=lo;z->vh=hi;}continue;
        }
        if(preset && merged_clamp(op))continue;
        /* Remaining preset scalar adjustments only accept the instrument numeric
           window. Others are unsupported, never classified as malformed. */
        int scalar_error=preset?-4:-2;
        switch(op) {
        case 0:case 1:case 2:case 3:case 4:case 12:case 45:case 50: {
            if(preset)return -2;
            int64_t v=val;
            if(op==4||op==12||op==45||op==50)v*=32768;
            if(op==0||op==4)z->start+=v;
            else if(op==1||op==12)z->end+=v;
            else if(op==2||op==45)z->ls+=v;
            else z->le+=v;
            break;
        }
        case 54:if(preset || (val!=0&&val!=1&&val!=3))return -2;z->mode=val;break;
        case 58:if(preset||val<0||val>127)return -2;z->root=val;break;
        case 48: if(val<0||val>1440)return scalar_error;break;
        case 17: if(val< -500||val>500)return scalar_error;break;
        case 51: if(val< -120||val>120)return scalar_error;break;
        case 52: if(val< -1200||val>1200)return scalar_error;break;
        case 56: if(val<0||val>1200)return scalar_error;break;
        case 57: if(preset||val<0||val>127)return -2;break;
        /* Bounded subset: combined key scaling remains below 2^31 frames at
           fixed 48 kHz, even after global/local and preset/instrument merge. */
        case 31:case 32:case 39:case 40:
            if(val< -1200||val>1200)return scalar_error;
            if(val< -40||val>40)return -4;
            break;
        case 5:case 6:case 7:case 10:case 11:if(val< -12000||val>12000)return scalar_error;break;
        case 8:if(val<1500||val>13500)return scalar_error;break;
        case 9:if(val<0||val>960)return scalar_error;break;
        case 13:if(val< -960||val>960)return scalar_error;break;
        case 22:case 24:if(val< -16000||val>4500)return scalar_error;break;
        case 21:case 23:case 25:case 27:case 33:case 35:
            if(val!=-32768&&(val< -12000||val>5000))return scalar_error;
            break;
        case 26:case 28:case 30:case 34:case 36:case 38:
            if(val!=-32768&&(val< -12000||val>8000))return scalar_error;
            break;
        case 29:if(val<0||val>1000)return scalar_error;break;
        case 37:if(val<0||val>1440)return scalar_error;break;
        case 46:case 47:return -4;
        case 15:case 16:*warnings|=2;break; /* unimplemented SF send amounts */
        default:return -2;
        }
    }
    return link;
}
sf2_result_t sf2_validate(const void *data,size_t size,bool approx,sf2_info_t *info) {
    if(!info)return SF2_INVALID;
    memset(info,0,sizeof(*info)); info->reason="invalid header";
    if(!data||size<12)return SF2_INVALID;
    if(size>12*MIB)LIMIT("file exceeds 12 MiB");
    const uint8_t *p=data;hydra h={0};
    uint32_t fuel=1000000; /* Includes repeatedly visited records and overlap work. */
    if(memcmp(p,"RIFF",4)||memcmp(p+8,"sfbk",4)||u32(p+4)!=(uint64_t)size-8)BAD("RIFF size or sfbk");
    if(!walk(&h,p,12,size,0))BAD("chunk boundary, padding, duplicate, or unsupported chunk");
    if(h.unsupported)return fail(info,SF2_UNSUPPORTED,"unsupported SF version",0);
    if(h.lists!=14||!h.version)BAD("missing INFO/ifil or duplicate LIST");
    if(!h.ph.p||!h.pb.p||!h.pm.p||!h.pg.p||!h.in.p||!h.ib.p||!h.im.p||!h.ig.p||!h.sh.p||!h.sm.p)BAD("missing mandatory table");
    if(h.ph.count<2||h.in.count<2||h.sh.count<2)BAD("missing records or sentinels");
    if(h.ph.count>257||h.in.count>4097||h.sh.count>4097||h.pb.count>8193||h.ib.count>8193||h.pg.count>65536||h.ig.count>65536||h.pm.count>16385||h.im.count>16385)LIMIT("hydra count limit");
    if(h.sm.bytes>10*MIB)LIMIT("PCM16 exceeds 10 MiB");
    if(h.sm.bytes>=4 && !memcmp(h.sm.p,"OggS",4))return fail(info,SF2_UNSUPPORTED,"SF3/Ogg samples",h.sm.p-p);
    if(h.sm24.p && h.sm24.bytes!=h.sm.count)BAD("sm24 size mismatch");
    if(h.sm24.p)info->warnings|=1;
    if(memcmp(h.ph.p+(h.ph.count-1)*38,"EOP",4)||memcmp(h.in.p+(h.in.count-1)*22,"EOI",4)||memcmp(h.sh.p+(h.sh.count-1)*46,"EOS",4))BAD("terminal name");
    /* SF2 2.04: the terminal modulator record is always ignored. Authoring tools
       often leave sfModTransOper non-zero there; record it, never reject it. */
    if(!zero_record(h.pm.p+(h.pm.count-1)*10,10)||!zero_record(h.im.p+(h.im.count-1)*10,10))info->warnings|=8;
    if(h.pm.count>1||h.im.count>1)info->warnings|=4;
    if(!index_table(h.ph,38,24,h.pb.count)||!index_table(h.in,22,20,h.ib.count)||!index_table(h.pb,4,0,h.pg.count)||!index_table(h.pb,4,2,h.pm.count)||!index_table(h.ib,4,0,h.ig.count)||!index_table(h.ib,4,2,h.im.count))BAD("nonmonotone or terminal indices");
    if(u16(h.ph.p+24)||u16(h.in.p+20)||u16(h.pb.p)||u16(h.pb.p+2)||u16(h.ib.p)||u16(h.ib.p+2))BAD("orphan leading records");
    for(uint32_t s=0;s+1<h.sh.count;s++) {
        const uint8_t *q=h.sh.p+s*46;uint32_t start=u32(q+20),end=u32(q+24),ls=u32(q+28),le=u32(q+32),rate=u32(q+36);unsigned type=u16(q+44),link=u16(q+42);
        if(type&0x8000)return fail(info,SF2_UNSUPPORTED,"ROM sample",q-p);
        if(type!=1&&type!=2&&type!=4&&type!=8)return fail(info,SF2_UNSUPPORTED,"sample type",q-p);
        if(start>=end||end>=h.sm.count||ls<start||le>end||ls>=le||rate<400||rate>192000||q[40]>127)BAD("sample bounds, loop, root, or rate");
        if(type!=1) {
            if(link>=h.sh.count-1||link==s)BAD("sample link");
            const uint8_t *mate=h.sh.p+link*46;unsigned mt=u16(mate+44);
            if(u16(mate+42)!=s||u32(mate+36)!=rate||((type==2&&mt!=4)||(type==4&&mt!=2)||(type==8&&mt!=8)))BAD("nonreciprocal stereo link");
        }
    }
    /* Validate all instrument zones, including unreferenced ones, before any
       upstream parser can index instrument/sample arrays. */
    for(uint32_t ins=0;ins+1<h.in.count;ins++) {
        uint32_t a=u16(h.in.p+ins*22+20),b=u16(h.in.p+(ins+1)*22+20);zone global=initial();
        if(a==b)BAD("empty instrument");
        for(uint32_t j=a;j<b;j++) {
            zone z=global;int id=parse_zone(h.ig,u16(h.ib.p+j*4),u16(h.ib.p+(j+1)*4),false,&z,&info->warnings,&fuel);
            if(id==-3)LIMIT("validator work budget");
            if(id==-4)return fail(info,SF2_UNSUPPORTED,"unsupported generator or key scaling",0);
            if(id==-2||id>=(int)h.sh.count-1||(id<0&&j!=a))BAD("instrument zone/link");
            if(id<0){global=z;continue;}
            const uint8_t *q=h.sh.p+(unsigned)id*46;
            int64_t st=(int64_t)u32(q+20)+z.start,en=(int64_t)u32(q+24)+z.end,ls=(int64_t)u32(q+28)+z.ls,le=(int64_t)u32(q+32)+z.le;
            if(st<0||st>=en||en+1>=(int64_t)h.sm.count||ls<st||le>en||ls>=le)BAD("effective signed sample offsets");
        }
    }
    for(uint32_t pr=0;pr+1<h.ph.count;pr++) {
        unsigned prog=u16(h.ph.p+pr*38+20),bank=u16(h.ph.p+pr*38+22);
        if(prog>127||bank>16383)BAD("bank/program range");
        for(uint32_t prev=0;prev<pr;prev++)if(u16(h.ph.p+prev*38+20)==prog&&u16(h.ph.p+prev*38+22)==bank)BAD("duplicate bank/program");
        uint32_t a=u16(h.ph.p+pr*38+24),b=u16(h.ph.p+(pr+1)*38+24),nr=0;zone global=initial();rect regions[256];
        if(a==b)BAD("empty preset");
        for(uint32_t j=a;j<b;j++) {
            zone pz=global;int id=parse_zone(h.pg,u16(h.pb.p+j*4),u16(h.pb.p+(j+1)*4),true,&pz,&info->warnings,&fuel);
            if(id==-3)LIMIT("validator work budget");
            if(id==-4)return fail(info,SF2_UNSUPPORTED,"unsupported generator or key scaling",0);
            if(id==-2||id>=(int)h.in.count-1||(id<0&&j!=a))BAD("preset zone/link");
            if(id<0){global=pz;continue;}
            uint32_t ia=u16(h.in.p+id*22+20),ib=u16(h.in.p+(id+1)*22+20);zone iglobal=initial();
            for(uint32_t k=ia;k<ib;k++) {
                zone iz=iglobal;int sample=parse_zone(h.ig,u16(h.ib.p+k*4),u16(h.ib.p+(k+1)*4),false,&iz,&info->warnings,&fuel);
                if(sample==-3)LIMIT("validator work budget");
                if(sample==-4)return fail(info,SF2_UNSUPPORTED,"unsupported generator or key scaling",0);
                if(sample<0){iglobal=iz;continue;}
                int lo=iz.lo>pz.lo?iz.lo:pz.lo,hi=iz.hi<pz.hi?iz.hi:pz.hi,vl=iz.vl>pz.vl?iz.vl:pz.vl,vh=iz.vh<pz.vh?iz.vh:pz.vh;
                if(lo>hi||vl>vh)continue;
                if(nr==256||info->regions==4096)LIMIT("flattened region limit");
                regions[nr++]=(rect){(uint8_t)lo,(uint8_t)hi,(uint8_t)vl,(uint8_t)vh};++info->regions;
            }
        }
        if(!nr)BAD("preset without playable regions");
        if(nr>fuel/128)LIMIT("validator work budget");
        fuel-=nr*128;
        for(unsigned key=0;key<128;key++) {
            int16_t diff[129]={0};
            for(uint32_t j=0;j<nr;j++)if(key>=regions[j].lo&&key<=regions[j].hi){diff[regions[j].vl]++;diff[regions[j].vh+1]--;}
            int count=0;for(unsigned vel=0;vel<128;vel++){count+=diff[vel];if(count>4)LIMIT("more than four layers");if((unsigned)count>info->max_layers)info->max_layers=(unsigned)count;}
        }
    }
    info->presets=h.ph.count-1;info->samples=h.sh.count-1;info->sample_bytes=h.sm.bytes;info->decoded_bytes=h.sm.bytes*2;
    /* Conservative preflight estimate; actual tagged allocator enforces peak. */
    info->metadata_peak_bytes=(h.ph.bytes+h.pb.bytes+h.pm.bytes+h.pg.bytes+h.in.bytes+h.ib.bytes+h.im.bytes+h.ig.bytes+h.sh.bytes)*2+info->regions*512+info->presets*64;
    /* SF02 memory loader holds the immutable source throughout prepare.
       Include it, allocator headers, five instances/pools and conservative
       metadata duplication. Streaming is not claimed by this host API. */
    if(info->decoded_bytes>20*MIB || size+info->decoded_bytes+info->metadata_peak_bytes+65536U>22*MIB)LIMIT("22 MiB staging peak including source blob");
    if((info->warnings&6)&&!approx)return fail(info,SF2_NEEDS_APPROXIMATE,"unimplemented modulators or SF sends require consent",0);
    info->reason="ok";return SF2_OK;
}
