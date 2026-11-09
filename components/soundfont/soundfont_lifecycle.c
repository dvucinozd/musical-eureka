#include "soundfont_lifecycle.h"
#include <string.h>
void sf_lc_init(sf_lifecycle *l,void (*stop)(void *,void *),void *ctx){memset(l,0,sizeof(*l));l->retire_rt=stop;l->context=ctx;atomic_init(&l->state,SF_LC_EMPTY);}
bool sf_lc_audio_bind(sf_lifecycle *l,unsigned p,uint64_t gen){if(p>=5||!l->active||gen!=l->active_generation)return false;l->part_refs[p]=l->active;l->missing_parts&=~(1U<<p);return true;}
bool sf_lc_audio_unbind(sf_lifecycle *l,unsigned p){if(p>=5)return false;l->part_refs[p]=NULL;l->missing_parts&=~(1U<<p);return true;}
sf_lc_result sf_lc_begin(sf_lifecycle *l,uint64_t id,uint64_t gen,bool detach){
 if(atomic_load_explicit(&l->state,memory_order_acquire)!=SF_LC_EMPTY)return SF_LC_BUSY;
 if(!id||id<=l->last_request||(!detach&&(!gen||gen<=l->last_generation)))return SF_LC_STALE;
 if(!detach)l->last_generation=gen;
 l->request=l->last_request=id;l->generation=gen;l->detach=detach;l->candidate=NULL;
 atomic_store_explicit(&l->state,SF_LC_PREPARING,memory_order_release);return SF_LC_OK;
}
sf_lc_result sf_lc_cancel(sf_lifecycle *l,uint64_t id){
 unsigned s=atomic_load_explicit(&l->state,memory_order_acquire);
 if(!id||id!=l->request)return SF_LC_STALE;
 if(s==SF_LC_EMPTY||s==SF_LC_ACK_READY)return l->ack.result;
 for(;;){unsigned next;
  if(s==SF_LC_PREPARING)next=SF_LC_PREP_CANCELLED;
  else if(s==SF_LC_PENDING)next=SF_LC_CANCEL_PENDING;
  else if(s==SF_LC_PREP_CANCELLED||s==SF_LC_CANCEL_PENDING)return SF_LC_CANCELLED;
  else return SF_LC_BUSY; // claimed or committed: cancellation has no undo
  if(atomic_compare_exchange_weak_explicit(&l->state,&s,next,memory_order_acq_rel,memory_order_acquire))return SF_LC_CANCELLED;
 }
}
static void ack(sf_lifecycle *l,void *retired,sf_lc_result result){
 l->ack=(sf_lc_ack){l->request,l->active_generation,retired,result,l->epoch};
 atomic_store_explicit(&l->state,SF_LC_ACK_READY,memory_order_release);
}
sf_lc_result sf_lc_publish(sf_lifecycle *l,void *candidate,bool prepared){
 unsigned s=atomic_load_explicit(&l->state,memory_order_acquire);
 if(s!=SF_LC_PREPARING&&s!=SF_LC_PREP_CANCELLED)return SF_LC_BUSY;
 l->candidate=candidate;
 if(s==SF_LC_PREP_CANCELLED){ack(l,candidate,SF_LC_CANCELLED);return SF_LC_CANCELLED;}
 if(!prepared||(!l->detach&&!candidate)||(l->detach&&candidate)){ack(l,candidate,SF_LC_PREPARE_FAILED);return SF_LC_PREPARE_FAILED;}
 unsigned expected=SF_LC_PREPARING;
 if(!atomic_compare_exchange_strong_explicit(&l->state,&expected,SF_LC_PENDING,memory_order_release,memory_order_acquire)){
  ack(l,candidate,SF_LC_CANCELLED);return SF_LC_CANCELLED;
 }
 return SF_LC_OK;
}
void sf_lc_audio_boundary(sf_lifecycle *l,bool refs,bool voices,bool fault,bool *missing){
 if(missing)*missing=false;
 unsigned s=atomic_load_explicit(&l->state,memory_order_acquire);
 if(s==SF_LC_CANCEL_PENDING){unsigned expected=s;
  if(atomic_compare_exchange_strong_explicit(&l->state,&expected,SF_LC_CLAIMED,memory_order_acq_rel,memory_order_acquire))ack(l,l->candidate,SF_LC_CANCELLED);
  return;
 }
 if(s!=SF_LC_PENDING)return;
 unsigned expected=SF_LC_PENDING;
 if(!atomic_compare_exchange_strong_explicit(&l->state,&expected,SF_LC_CLAIMED,memory_order_acq_rel,memory_order_acquire))return;
 if(fault){ack(l,l->candidate,SF_LC_AUDIO_FAULT);return;}
 for(unsigned p=0;p<5;p++)refs|=l->part_refs[p]!=NULL;
 if(!l->detach&&(refs||voices)){ack(l,l->candidate,SF_LC_BANK_IN_USE);return;}
 if(l->detach&&l->generation!=l->active_generation){ack(l,l->candidate,SF_LC_STALE);return;}
 if(l->epoch==UINT32_MAX){ack(l,l->candidate,SF_LC_LIMIT);return;}
 void *old=l->active;
 if(old&&l->retire_rt)l->retire_rt(l->context,old);
 for(unsigned p=0;p<5;p++)if(l->part_refs[p]){l->part_refs[p]=NULL;l->missing_parts|=1U<<p;}
 l->active=l->detach?NULL:l->candidate;l->active_generation=l->detach?0:l->generation;l->epoch++;
 if(missing)*missing=l->detach&&old;
 ack(l,old,SF_LC_OK);
}
bool sf_lc_take_ack(sf_lifecycle *l,sf_lc_ack *out){
 if(!out||atomic_load_explicit(&l->state,memory_order_acquire)!=SF_LC_ACK_READY)return false;
 *out=l->ack;atomic_store_explicit(&l->state,SF_LC_EMPTY,memory_order_release);return true;
}
