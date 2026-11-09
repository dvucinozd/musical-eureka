#ifndef NEKTAR_SF_LIFECYCLE_H
#define NEKTAR_SF_LIFECYCLE_H
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"RT mailbox requires lock-free 32-bit int");
_Static_assert(sizeof(unsigned)==4,"32-bit mailbox required");
typedef enum { SF_LC_OK, SF_LC_BUSY, SF_LC_STALE, SF_LC_CANCELLED,
 SF_LC_BANK_IN_USE, SF_LC_AUDIO_FAULT, SF_LC_PREPARE_FAILED, SF_LC_LIMIT } sf_lc_result;
typedef enum { SF_LC_EMPTY, SF_LC_PREPARING, SF_LC_PREP_CANCELLED,
 SF_LC_PENDING, SF_LC_CANCEL_PENDING, SF_LC_CLAIMED, SF_LC_ACK_READY } sf_lc_state;
typedef struct { uint64_t request,generation; void *retired; sf_lc_result result; uint32_t epoch; } sf_lc_ack;
typedef struct {
 _Atomic unsigned state;
 uint64_t request,last_request,generation,last_generation; // immutable between publication and ACK
 void *candidate; bool detach;
 sf_lc_ack ack;
 void *active; uint64_t active_generation; uint32_t epoch;
 void *part_refs[5]; unsigned missing_parts;
 void (*retire_rt)(void *context,void *bank); void *context;
} sf_lifecycle;
/* retire_rt stops old bank voices before ACK; bounded, no free/alloc/I/O. */
void sf_lc_init(sf_lifecycle *,void (*retire_rt)(void *,void *),void *context);
bool sf_lc_audio_bind(sf_lifecycle *,unsigned part,uint64_t generation);
/* Caller must stop this part before unbind (adapter wrapper does so). */
bool sf_lc_audio_unbind(sf_lifecycle *,unsigned part);
/* Single serialized worker/control owner. IDs strictly increase; 0 reserved.
   begin takes no bank ownership. publish always transfers candidate ownership
   after successful begin, including cancelled/failed prepare cleanup. */
sf_lc_result sf_lc_begin(sf_lifecycle *,uint64_t request,uint64_t generation,bool detach);
sf_lc_result sf_lc_cancel(sf_lifecycle *,uint64_t request);
sf_lc_result sf_lc_publish(sf_lifecycle *,void *candidate,bool prepared);
/* Single audio owner, one call per boundary. Never frees/allocates/waits.
   refs/voices describe current SF use. detach requires explicit caller request.
   sound_missing output is true only for a committed detach of an existing bank. */
void sf_lc_audio_boundary(sf_lifecycle *,bool track_refs,bool live_voices,bool fault,bool *sound_missing);
/* Worker-only. Returned retired pointer is now exclusively caller-owned.
   No new lifecycle request until this ACK is taken. */
bool sf_lc_take_ack(sf_lifecycle *,sf_lc_ack *);
#endif
