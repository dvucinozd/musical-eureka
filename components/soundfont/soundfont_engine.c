#include "soundfont_engine.h"
#include <string.h>
#include <math.h>
#include <stddef.h>
typedef union {
    max_align_t align;
    struct { sf_allocator allocator; size_t size; sf_mem_kind kind; } h;
} allocation_header;
/* Only prepare allocates. TLS keeps independent worker preparations isolated.
   Header records original callback for cleanup and realloc failure unwinding. */
static _Thread_local const sf_allocator *alloc_context;
static _Thread_local unsigned region_budget;
static void *sf_alloc(sf_mem_kind kind, size_t size) {
    if (!alloc_context || !size || size > SIZE_MAX-sizeof(allocation_header)) return NULL;
    allocation_header *h=alloc_context->alloc(alloc_context->context,kind,sizeof(*h)+size);
    if(!h)return NULL;
    h->h.allocator=*alloc_context;h->h.size=size;h->h.kind=kind;return h+1;
}
static void sf_free(void *p) {
    if(!p)return;
    allocation_header *h=(allocation_header*)p-1;
    sf_allocator a=h->h.allocator;sf_mem_kind k=h->h.kind;a.free(a.context,k,h);
}
static void *sf_realloc(sf_mem_kind kind,void *p,size_t n) {
    if(!p)return sf_alloc(kind,n);
    if(!n){sf_free(p);return NULL;}
    allocation_header *h=(allocation_header*)p-1;
    void *q=sf_alloc(kind,n);if(!q)return NULL;
    memcpy(q,p,h->h.size<n?h->h.size:n);sf_free(p);return q;
}
#define SF_ALLOC sf_alloc
#define SF_REALLOC sf_realloc
#define TSF_FREE sf_free
#define TSF_MALLOC unused_malloc
#define TSF_REALLOC unused_realloc
#define TSF_NO_STDIO
#define TSF_STATIC
#define TSF_IMPLEMENTATION
/* Sample-exact envelopes/LFOs across 1/64/128-frame caller segmentation.
   Hardware timing remains an explicit qualification gate for this setting. */
#define TSF_RENDER_EFFECTSAMPLEBLOCK 1
#include "third_party/tsf_nektar.h"
struct sf_bank { tsf *parts[5]; sf_part_stats stats[5]; };
static tsf *part(sf_bank *b,unsigned p){return b&&p<5?b->parts[p]:NULL;}
static unsigned active(tsf *f){unsigned n=0;for(int i=0;i<f->voiceNum;i++)n+=f->voices[i].playingPreset!=-1;return n;}
static void measure(sf_bank *b,unsigned p){sf_part_stats *s=&b->stats[p];s->voice_active=active(b->parts[p]);if(s->voice_active>s->voice_peak)s->voice_peak=s->voice_active;}
void sf_bank_destroy(sf_bank *b){if(!b)return;for(unsigned p=0;p<5;p++)if(b->parts[p])tsf_close(b->parts[p]);sf_free(b);}
static bool regions_safe(tsf *f,unsigned sample_count){
    for(int p=0;p<f->presetNum;p++){
        struct tsf_preset *pr=&f->presets[p];if(pr->regionNum<1||pr->regionNum>256)return false;
        for(int j=0;j<pr->regionNum;j++){
            struct tsf_region *r=&pr->regions[j];
            if(r->offset>=r->end||r->end>=sample_count||r->loop_start<r->offset||r->loop_end>=r->end||r->loop_start>r->loop_end||
               !isfinite(r->attenuation)||!isfinite(r->pan)||r->pan<-.5f||r->pan>.5f||!r->sample_rate)return false;
        }
    }return true;
}
sf_bank *sf_bank_prepare(const void *data,size_t n,bool approximate,const sf_allocator *a,sf2_info_t *info){
    if(sf2_validate(data,n,approximate,info)!=SF2_OK)return NULL;
    if(!a||!a->alloc||!a->free){info->reason="invalid capability allocator";return NULL;}
    const sf_allocator *old=alloc_context;alloc_context=a;
    sf_bank *b=sf_alloc(SF_MEM_INSTANCE,sizeof(*b));
    if(!b){info->reason="instance allocation failed";alloc_context=old;return NULL;}memset(b,0,sizeof(*b));
    region_budget=info->regions;
    b->parts[0]=tsf_load_memory(data,(int)n);
    info->reason="engine allocation failed";
    if(!b->parts[0])goto bad;
    if(!regions_safe(b->parts[0],info->sample_bytes/2)){info->reason="effective engine region validation failed";goto bad;}
    for(unsigned p=1;p<5;p++)if(!(b->parts[p]=tsf_copy(b->parts[0])))goto bad;
    for(unsigned p=0;p<5;p++){
        tsf *f=b->parts[p];
        if(!tsf_set_max_voices(f,p?4:16)||!tsf_channel_set_presetindex(f,0,0))goto bad;
        tsf_set_output(f,TSF_MONO,48000,-12.0f);
    }
    info->reason="ok";alloc_context=old;return b;
bad:sf_bank_destroy(b);alloc_context=old;return NULL;
}
unsigned sf_bank_preset_count(const sf_bank *b){return b?(unsigned)b->parts[0]->presetNum:0;}
bool sf_bank_preset(const sf_bank *b,unsigned p,unsigned *bank,unsigned *program,char name[21]){
    if(!b||p>=sf_bank_preset_count(b)||!bank||!program||!name)return false;
    const struct tsf_preset *r=&b->parts[0]->presets[p];*bank=r->bank;*program=r->preset;memcpy(name,r->presetName,20);name[20]=0;return true;
}
void sf_part_reset_controllers_rt(sf_bank *b,unsigned p){tsf *f=part(b,p);if(!f)return;
    tsf_channel_set_sustain(f,0,0);
    tsf_channel_midi_control(f,0,121,0);
    tsf_channel_set_pitchwheel(f,0,8192);
}
void sf_part_panic_rt(sf_bank *b,unsigned p){tsf *f=part(b,p);if(!f)return;for(int j=0;j<f->voiceNum;j++)tsf_voice_kill(&f->voices[j]);sf_part_reset_controllers_rt(b,p);measure(b,p);}
bool sf_part_set_program(sf_bank *b,unsigned p,unsigned bank,unsigned program){tsf *f=part(b,p);if(!f||bank>16383||program>127)return false;
    int index=tsf_get_presetindex(f,(int)bank,(int)program);if(index<0)return false;
    sf_part_panic_rt(b,p);return tsf_channel_set_presetindex(f,0,index)!=0;
}
bool sf_part_note_on(sf_bank *b,unsigned p,unsigned key,unsigned vel){
    tsf *f=part(b,p);if(!f||key>127||vel>127)return false;if(!vel){sf_part_note_off(b,p,key);return true;}
    struct tsf_preset *pr=&f->presets[f->channels->channels[0].presetIndex];unsigned need=0,free_count=0;
    for(int j=0;j<pr->regionNum;j++){struct tsf_region *r=&pr->regions[j];if(key>=r->lokey&&key<=r->hikey&&vel>=r->lovel&&vel<=r->hivel)need++;}
    for(int j=0;j<f->voiceNum;j++)free_count+=f->voices[j].playingPreset==-1;
    /* Reclaim complete release groups only; never steal a subset of a layer. */
    if(need>free_count)for(int j=0;j<f->voiceNum&&need>free_count;j++){
        struct tsf_voice *v=&f->voices[j];if(v->playingPreset==-1||v->ampenv.segment!=TSF_SEGMENT_RELEASE)continue;
        unsigned group=v->playIndex;bool releasable=true;
        for(int k=0;k<f->voiceNum;k++)if(f->voices[k].playingPreset!=-1&&f->voices[k].playIndex==group&&f->voices[k].ampenv.segment!=TSF_SEGMENT_RELEASE)releasable=false;
        if(releasable)for(int k=0;k<f->voiceNum;k++)if(f->voices[k].playingPreset!=-1&&f->voices[k].playIndex==group){tsf_voice_kill(&f->voices[k]);free_count++;}
    }
    if(!need||need>4||need>free_count){b->stats[p].note_rejected++;measure(b,p);return false;}
    /* Upstream must see identical MIDI velocity after its float conversion. */
    int ok=tsf_channel_note_on(f,0,key,(float)vel/127.0f);
    b->stats[p].sample_layers=need;measure(b,p);return ok!=0;
}
void sf_part_note_off(sf_bank *b,unsigned p,unsigned key){tsf *f=part(b,p);if(f&&key<128)tsf_channel_note_off(f,0,key);}
bool sf_part_control(sf_bank *b,unsigned p,unsigned cc,unsigned v){tsf *f=part(b,p);if(!f||cc>127||v>127)return false;
    if(cc==120){sf_part_panic_rt(b,p);return true;}if(cc==121){sf_part_reset_controllers_rt(b,p);return true;}
    /* Bank/program and global volume belong to ingress/master, not sampler.
       SF modulation support is intentionally limited; no hidden RPN tuning. */
    if(cc!=64&&cc!=123)return false;
    return tsf_channel_midi_control(f,0,cc,v)!=0;
}
bool sf_part_pitch(sf_bank *b,unsigned p,unsigned value){tsf *f=part(b,p);return f&&value<=16383&&tsf_channel_set_pitchwheel(f,0,value);}
bool sf_part_render(sf_bank *b,unsigned p,float *out,unsigned frames){tsf *f=part(b,p);if(!f||!out||frames>128)return false;if(frames)tsf_render_float(f,out,frames,0);measure(b,p);return true;}
sf_part_stats sf_part_get_stats(const sf_bank *b,unsigned p){return b&&p<5?b->stats[p]:(sf_part_stats){0};}
