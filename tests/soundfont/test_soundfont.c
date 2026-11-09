#include "soundfont_engine.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static unsigned calls,frees,live,fail_at,rt;
static void *allocate(void *ctx,sf_mem_kind kind,size_t n){(void)ctx;assert(kind<SF_MEM_COUNT);assert(!rt);calls++;if(calls==fail_at)return NULL;void *p=malloc(n);if(p)live++;return p;}
static void release(void *ctx,sf_mem_kind kind,void *p){(void)ctx;(void)kind;assert(!rt);if(p){assert(live);live--;frees++;free(p);}}
static unsigned char *readfile(const char *name,size_t *n){FILE *f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);unsigned char *b=malloc(*n);assert(fread(b,1,*n,f)==*n);fclose(f);return b;}
int main(int argc,char **argv){assert(argc==2);size_t n;unsigned char *data=readfile(argv[1],&n);sf2_info_t i;sf_allocator a={NULL,allocate,release};
    assert(sf2_validate(data,n,false,&i)==SF2_OK);
    for(size_t j=0;j<n;j++)assert(sf2_validate(data,j,false,&i)!=SF2_OK);
    sf_bank *b=sf_bank_prepare(data,n,false,&a,&i);assert(b);unsigned total=calls;sf_bank_destroy(b);assert(!live);
    for(unsigned j=1;j<=total;j++){calls=0;fail_at=j;b=sf_bank_prepare(data,n,false,&a,&i);assert(!b);assert(i.reason && strcmp(i.reason,"ok"));assert(!live);}fail_at=0;calls=0;
    for(unsigned cycle=0;cycle<100;cycle++){b=sf_bank_prepare(data,n,false,&a,&i);assert(b);rt=1;
        for(unsigned p=0;p<5;p++){
            assert(sf_part_set_program(b,p,0,0));assert(!sf_part_set_program(b,p,0,127));
            assert(sf_part_note_on(b,p,60,100));sf_part_stats st=sf_part_get_stats(b,p);unsigned layers=st.voice_active;assert(layers>=1&&layers<=4);
            unsigned cap=p?4:16;unsigned admitted=1;for(;admitted<cap/layers;admitted++)assert(sf_part_note_on(b,p,61+admitted%20,100));assert(sf_part_get_stats(b,p).voice_active<=cap);
            if(!strstr(argv[1],"exclusive"))assert(!sf_part_note_on(b,p,80,100));
            float out[128];for(int k=0;k<100;k++){assert(sf_part_render(b,p,out,128));for(int q=0;q<128;q++)assert(isfinite(out[q]));}
            sf_part_control(b,p,64,127);sf_part_note_off(b,p,60);sf_part_pitch(b,p,16383);sf_part_control(b,p,64,0);sf_part_control(b,p,121,0);
            sf_part_panic_rt(b,p);assert(!sf_part_get_stats(b,p).voice_active);sf_part_reset_controllers_rt(b,p);assert(sf_part_render(b,p,out,128));for(int q=0;q<128;q++)assert(out[q]==0);
        }rt=0;sf_bank_destroy(b);assert(!live);
    }
    printf("PASS fixture=%s prepare_allocations=%u failure_points=%u cycles=100 RT_alloc_free=0\n",argv[1],total,total);free(data);return 0;
}
