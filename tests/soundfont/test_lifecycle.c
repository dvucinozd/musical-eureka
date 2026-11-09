#include "soundfont_lifecycle.h"
#include "soundfont_engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned allocations,rt;
static void *a(void *c,sf_mem_kind k,size_t n){(void)c;(void)k;assert(!rt);void *p=malloc(n);if(p)allocations++;return p;}
static void f(void *c,sf_mem_kind k,void *p){(void)c;(void)k;assert(!rt);if(p){assert(allocations);allocations--;free(p);}}
static sf_lc_ack take(sf_lifecycle *l,sf_lc_result expected){sf_lc_ack r;assert(sf_lc_take_ack(l,&r));assert(r.result==expected);assert(!sf_lc_take_ack(l,&r));return r;}
static void boundary(sf_lifecycle *l,bool refs,bool voices,bool fault,bool *missing){rt=1;sf_lc_audio_boundary(l,refs,voices,fault,missing);rt=0;}
static void stop_bank(void *ctx,void *bank){(void)ctx;for(unsigned p=0;p<5;p++)sf_part_panic_rt(bank,p);}
static void fake_cases(void){sf_lifecycle l;sf_lc_init(&l,NULL,NULL);int bank1,bank2;bool missing;
 assert(sf_lc_begin(&l,1,1,false)==SF_LC_OK);assert(sf_lc_cancel(&l,2)==SF_LC_STALE);
 assert(sf_lc_publish(&l,&bank1,true)==SF_LC_OK);boundary(&l,false,false,false,&missing);assert(!missing&&l.active==&bank1);
 assert(sf_lc_begin(&l,2,2,false)==SF_LC_BUSY);assert(sf_lc_cancel(&l,1)==SF_LC_OK);assert(take(&l,SF_LC_OK).retired==NULL);
 for(unsigned stage=0;stage<2;stage++){
  assert(sf_lc_begin(&l,2+stage,2+stage,false)==SF_LC_OK);
  if(stage)assert(sf_lc_publish(&l,&bank2,true)==SF_LC_OK);
  assert(sf_lc_cancel(&l,2+stage)==SF_LC_CANCELLED);assert(sf_lc_cancel(&l,2+stage)==SF_LC_CANCELLED);
  if(!stage)assert(sf_lc_publish(&l,&bank2,true)==SF_LC_CANCELLED);
  boundary(&l,false,false,false,&missing);assert(take(&l,SF_LC_CANCELLED).retired==&bank2);assert(l.active==&bank1);
 }
 for(unsigned reason=0;reason<3;reason++){
  assert(sf_lc_begin(&l,4+reason,4+reason,false)==SF_LC_OK);assert(sf_lc_publish(&l,&bank2,true)==SF_LC_OK);
  boundary(&l,reason==0,reason==1,reason==2,&missing);
  assert(take(&l,reason==2?SF_LC_AUDIO_FAULT:SF_LC_BANK_IN_USE).retired==&bank2);assert(l.active==&bank1);
 }
 assert(sf_lc_begin(&l,7,7,false)==SF_LC_OK);assert(sf_lc_publish(&l,&bank2,false)==SF_LC_PREPARE_FAILED);assert(take(&l,SF_LC_PREPARE_FAILED).retired==&bank2);
 assert(sf_lc_begin(&l,8,99,true)==SF_LC_OK);assert(sf_lc_publish(&l,NULL,true)==SF_LC_OK);boundary(&l,true,true,false,&missing);assert(take(&l,SF_LC_STALE).retired==NULL);assert(l.active==&bank1);
 assert(sf_lc_begin(&l,9,1,true)==SF_LC_OK);assert(sf_lc_publish(&l,NULL,true)==SF_LC_OK);
 // A delayed consumer never permits overwrite or frees the active bank.
 for(int i=0;i<100;i++)assert(sf_lc_begin(&l,10,10,false)==SF_LC_BUSY);
 boundary(&l,true,true,false,&missing);assert(missing&&!l.active);assert(take(&l,SF_LC_OK).retired==&bank1);
 assert(sf_lc_begin(&l,9,10,false)==SF_LC_STALE);
 assert(sf_lc_begin(&l,10,10,false)==SF_LC_OK);assert(sf_lc_publish(&l,&bank2,true)==SF_LC_OK);l.epoch=UINT32_MAX;boundary(&l,false,false,false,NULL);assert(take(&l,SF_LC_LIMIT).retired==&bank2);assert(!l.active&&l.epoch==UINT32_MAX);
}
int main(void){fake_cases();FILE *file=fopen("tests/soundfont/fixtures/mono.sf2","rb");assert(file);fseek(file,0,SEEK_END);size_t n=ftell(file);rewind(file);void *data=malloc(n);assert(fread(data,1,n,file)==n);fclose(file);
 sf_allocator mem={NULL,a,f};sf_lifecycle l;sf_lc_init(&l,stop_bank,NULL);
 for(unsigned i=0;i<100;i++){sf2_info_t info;sf_bank *b=sf_bank_prepare(data,n,false,&mem,&info);assert(b);
  assert(sf_lc_begin(&l,i*2+1,i+1,false)==SF_LC_OK);assert(sf_lc_publish(&l,b,true)==SF_LC_OK);boundary(&l,false,false,false,NULL);assert(take(&l,SF_LC_OK).retired==NULL);
  float out[128];rt=1;for(unsigned p=0;p<5;p++)assert(sf_lc_audio_bind(&l,p,i+1));assert(sf_lc_audio_unbind(&l,0));assert(!l.part_refs[0]);assert(sf_lc_audio_bind(&l,0,i+1));assert(sf_part_note_on(l.active,0,60,127));assert(sf_part_render(l.active,0,out,128));rt=0;
  assert(sf_lc_begin(&l,i*2+2,i+1,true)==SF_LC_OK);assert(sf_lc_publish(&l,NULL,true)==SF_LC_OK);boundary(&l,true,true,false,NULL);sf_bank_destroy(take(&l,SF_LC_OK).retired);assert(!allocations);assert(l.missing_parts==31);for(unsigned p=0;p<5;p++)assert(!l.part_refs[p]);
 }free(data);puts("PASS SF03 host lifecycle: stale/busy/cancel/prepfail/BANK_IN_USE/fault/detach, delayed ACK, 100 real bank cycles, RT alloc/free=0");return 0;
}
