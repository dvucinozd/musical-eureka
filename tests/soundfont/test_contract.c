#include "soundfont_engine.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
static unsigned live,rt;static size_t bytes[SF_MEM_COUNT],peak[SF_MEM_COUNT];
typedef union { max_align_t align; struct {size_t n;sf_mem_kind k;} h; } hdr;
static void *a(void *c,sf_mem_kind k,size_t n){(void)c;assert(!rt);hdr *h=malloc(sizeof(*h)+n);assert(h);h->h.n=n;h->h.k=k;live++;bytes[k]+=n;if(bytes[k]>peak[k])peak[k]=bytes[k];return h+1;}
static void f(void *c,sf_mem_kind k,void *p){(void)c;assert(!rt);if(!p)return;hdr *h=(hdr*)p-1;assert(k==h->h.k);bytes[k]-=h->h.n;live--;free(h);}
static unsigned char *load(const char *label,size_t *n){char path[256];snprintf(path,sizeof(path),"tests/soundfont/fixtures/%s.sf2",label);FILE *file=fopen(path,"rb");assert(file);fseek(file,0,SEEK_END);*n=ftell(file);rewind(file);unsigned char *b=malloc(*n);assert(fread(b,1,*n,file)==*n);fclose(file);return b;}
static sf_bank *prepare(const char *label){size_t n;unsigned char *data=load(label,&n);sf2_info_t i;sf_allocator mem={NULL,a,f};sf_bank *b=sf_bank_prepare(data,n,false,&mem,&i);if(!b)fprintf(stderr,"%s: %s\n",label,i.reason);assert(b);free(data);return b;}
static void render(sf_bank *b,unsigned p,float *out,unsigned total,unsigned segment){for(unsigned j=0;j<total;){unsigned count=total-j<segment?total-j:segment;assert(sf_part_render(b,p,out+j,count));for(unsigned x=0;x<count;x++)assert(isfinite(out[j+x]));j+=count;}}
static void timing_pitch(void){
    float x[8192],y[8192],z[8192];sf_bank *b=prepare("mono");rt=1;
    sf_part_note_on(b,0,60,127);render(b,0,x,8192,128);sf_part_panic_rt(b,0);
    sf_part_note_on(b,0,60,127);render(b,0,y,8192,64);sf_part_panic_rt(b,0);
    sf_part_note_on(b,0,60,127);render(b,0,z,8192,1);
    for(unsigned j=0;j<8192;j++){assert(fabsf(x[j]-y[j])<1e-7f);assert(fabsf(x[j]-z[j])<1e-7f);}
    unsigned crosses=0;for(unsigned j=1025;j<8192;j++)if(x[j-1]<=0&&x[j]>0)crosses++;
    assert(crosses>=110&&crosses<=114); /* 48000/64=750Hz, 7167 frames */
    sf_part_panic_rt(b,0);sf_part_note_on(b,0,72,127);render(b,0,x,8192,128);unsigned high=0;for(unsigned j=1025;j<8192;j++)if(x[j-1]<=0&&x[j]>0)high++;
    assert(high>=222&&high<=226);rt=0;sf_bank_destroy(b);
}
static void sustain_and_layers(void){
    sf_bank *b=prepare("four_layers");float out[128];rt=1;
    assert(sf_part_note_on(b,1,60,127));assert(sf_part_get_stats(b,1).voice_active==4);
    assert(!sf_part_note_on(b,1,61,127));assert(sf_part_get_stats(b,1).voice_active==4);
    sf_part_note_off(b,1,60);assert(sf_part_note_on(b,1,62,127));assert(sf_part_get_stats(b,1).voice_active==4);
    sf_part_panic_rt(b,1);sf_part_note_on(b,1,60,127);sf_part_control(b,1,64,127);sf_part_note_off(b,1,60);
    for(unsigned j=0;j<100;j++)sf_part_render(b,1,out,128);assert(sf_part_get_stats(b,1).voice_active==4);
    sf_part_reset_controllers_rt(b,1);for(unsigned j=0;j<100;j++)sf_part_render(b,1,out,128);assert(!sf_part_get_stats(b,1).voice_active);
    assert(!sf_part_control(b,1,7,127));assert(!sf_part_control(b,1,20,127));assert(!sf_part_control(b,1,0,127));assert(!sf_part_control(b,1,6,127));
    rt=0;sf_bank_destroy(b);
    b=prepare("velocity_split");rt=1;for(unsigned v=1;v<128;v++){sf_part_panic_rt(b,0);assert(sf_part_note_on(b,0,60,v));assert(sf_part_get_stats(b,0).voice_active==1);}rt=0;sf_bank_destroy(b);
    b=prepare("stereo_pair");rt=1;assert(sf_part_note_on(b,0,60,127));assert(sf_part_get_stats(b,0).voice_active==2);
    for(int j=0;j<20;j++){sf_part_render(b,0,out,128);for(int k=0;k<128;k++)assert(fabsf(out[k])<1e-6f);}rt=0;sf_bank_destroy(b);
}
static void dynamic_timing(void){
    sf_bank *b=prepare("dynamic");float out[128];rt=1;
    for(unsigned p=0;p<5;p++)for(unsigned n=0;n<(p?4:16);n++)assert(sf_part_note_on(b,p,48+n,127));
    double total=0,worst=0;
    for(unsigned j=0;j<300;j++){clock_t begin=clock();for(unsigned p=0;p<5;p++)render(b,p,out,128,128);double elapsed=(double)(clock()-begin)*1000000/CLOCKS_PER_SEC;total+=elapsed;if(elapsed>worst)worst=elapsed;}
    for(unsigned p=0;p<5;p++)assert(sf_part_get_stats(b,p).voice_active==(p?4:16));
    rt=0;sf_bank_destroy(b);printf("HOST ONLY dynamic32 voices128frames CPU us avg=%.1f max=%.1f; P4 timing unmeasured\n",total/300,worst);
}
static void invalids(void){size_t n;unsigned char *data=load("mono",&n),*copy=malloc(n);sf2_info_t i;
    const char *tags[]={"phdr","pbag","pmod","pgen","inst","ibag","imod","igen","shdr","smpl"};
    for(unsigned t=0;t<10;t++)for(size_t p=12;p+8<n;p++)if(!memcmp(data+p,tags[t],4)){
        memcpy(copy,data,n);memset(copy+p+4,255,4);assert(sf2_validate(copy,n,true,&i)!=SF2_OK);break;
    }
    for(size_t p=12;p+8<n;p++){
        if(!memcmp(data+p,"shdr",4)){memcpy(copy,data,n);memset(copy+p+8+20,255,4);assert(sf2_validate(copy,n,true,&i)!=SF2_OK);memcpy(copy,data,n);copy[p+8+45]=128;assert(sf2_validate(copy,n,true,&i)==SF2_UNSUPPORTED);}
        if(!memcmp(data+p,"igen",4)){memcpy(copy,data,n);copy[p+8+6]=255;copy[p+8+7]=255;assert(sf2_validate(copy,n,true,&i)!=SF2_OK);}
    }
    free(copy);free(data);data=load("five_layers",&n);assert(sf2_validate(data,n,false,&i)==SF2_LIMIT);free(data);
}
int main(void){invalids();timing_pitch();sustain_and_layers();dynamic_timing();
    const char *labels[]={"odd_info","rate22050","rate44100","one_shot","release_loop","offset_add","short_loop","max_regions","exclusive","keyscale","bank128","dynamic","compat_sentinels"};
    for(unsigned j=0;j<sizeof(labels)/sizeof(*labels);j++){
        sf_bank *b=prepare(labels[j]);unsigned bank,program;char name[21];assert(sf_bank_preset(b,0,&bank,&program,name));rt=1;
        assert(sf_part_set_program(b,0,bank,program));
        for(unsigned burst=0;burst<64;burst++){sf_part_note_on(b,0,burst%128,127);sf_part_pitch(b,0,burst*255);sf_part_control(b,0,64,127);}
        sf_part_panic_rt(b,0);assert(sf_part_note_on(b,0,127,127));
        if(!strcmp(labels[j],"exclusive"))assert(sf_part_get_stats(b,0).voice_active==2);
        float out[128];for(int k=0;k<100;k++){render(b,0,out,128,128);}
        if(!strcmp(labels[j],"short_loop"))assert(sf_part_get_stats(b,0).voice_active==1);
        sf_part_panic_rt(b,0);rt=0;sf_bank_destroy(b);assert(!live);
    }
    printf("PASS SF02 contracts pitch octave/segmentation1-64-128, invalid indices/bounds/counts, 64-event burst, 256 regions, short-loop/root0/key127, exclusive layers, keyscale\n");
    printf("typed peak samples=%zu metadata=%zu voices=%zu channels=%zu instance=%zu\n",peak[0],peak[1],peak[2],peak[3],peak[4]);return 0;
}
