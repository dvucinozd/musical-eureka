#ifndef SYNTH_SAMPLER_H
#define SYNTH_SAMPLER_H
#include "synth_sound.h"
#include <stddef.h>

/* HOST EXPERIMENT ONLY. No application installs these ops. No bank pointer is
 * stored in synth; context belongs to the audio adapter that resolves part IDs.
 * All callbacks bounded and audio-owned, no allocation/free/I/O/log/wait.
 * validate must ensure explicit generation/bank/program exists and reserve its
 * lifetime through completion. complete releases the reservation (also fault).
 * bind + program cannot normally fail after successful validation; unexpected
 * failures are panicked/unbound and the sound becomes missing/silent.
 * render overwrites mono float dry output, at most128 frames, no extra gain.
 * note_on returns false on pool/layer admission failure; no pending note queue.
 */
typedef struct {
    void *context;
    sound_result_t (*validate)(void *, unsigned part, const sound_ref_t *);
    bool (*bind)(void *, unsigned part, uint64_t generation);
    bool (*program)(void *, unsigned part, unsigned bank, unsigned program);
    void (*unbind)(void *, unsigned part);
    bool (*note_on)(void *, unsigned part, unsigned key, unsigned velocity);
    void (*note_off)(void *, unsigned part, unsigned key);
    void (*control)(void *, unsigned part, unsigned cc, unsigned value);
    void (*pitch)(void *, unsigned part, unsigned value);
    void (*panic)(void *, unsigned part);
    bool (*render)(void *, unsigned part, float *, unsigned frames);
    unsigned (*voice_count)(void *, unsigned part);
    void (*complete)(void *, unsigned part, uint64_t request, bool fault);
} synth_sampler_ops_t;
#endif
