#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_sampler.h"
#include "audio_storage_gate.h"
#include "preset_codec.h"

static bool parked;
static unsigned allocations;
static synth_t synth;
static synth_looper_t looper;

bool audio_storage_held(void)
{
    return parked;
}

static void *allocate(void *context, sf_mem_kind kind, size_t size)
{
    (void)context;
    (void)kind;
    assert(parked);
    void *ptr = malloc(size);
    if (ptr) ++allocations;
    return ptr;
}

static void release(void *context, sf_mem_kind kind, void *data)
{
    (void)context;
    (void)kind;
    assert(allocations > 0);
    --allocations;
    free(data);
}

static sf_bank *prepare(const char *path)
{
    static uint8_t bytes[16384];
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t size = fread(bytes, 1, sizeof(bytes), file);
    assert(feof(file) && !ferror(file));
    fclose(file);
    sf2_info_t info;
    const sf_allocator memory = {.alloc = allocate, .free = release};
    sf_bank *bank = sf_bank_prepare(bytes, size, false, &memory, &info);
    assert(bank);
    return bank;
}

static uint64_t attach(sf_bank *bank, sf_bank *retired)
{
    assert(!audio_storage_stage_bank(bank));
    parked = true;
    uint64_t gen = audio_storage_stage_bank(bank);
    assert(gen);
    sf_lc_ack ack;
    assert(!audio_storage_bank_ack(&ack)); /* Candidate stays worker-owned by the mailbox. */
    audio_restore_t restore = {.sound = {.engine = SOUND_SF2, .bank_generation = gen}};
    audio_sampler_begin_block();
    assert(audio_sampler_resume(&synth, &restore));
    assert(audio_storage_bank_ack(&ack) && ack.result == SF_LC_OK && ack.generation == gen);
    assert(ack.retired == retired);
    sf_bank_destroy(ack.retired);
    parked = false;
    return gen;
}

static void midi(bool down)
{
    midi_event_t event = {
        .type = down ? MIDI_EVENT_NOTE_ON : MIDI_EVENT_NOTE_OFF,
        .note = {60, down ? 100 : 0},
    };
    assert(synth_sound_event_from(&synth, SYNTH_SOURCE_USB, synth_live_epoch(&synth), &event));
}

static double render(void)
{
    float pcm[128];
    synth_render(&synth, pcm, 128);
    double energy = 0;
    for (unsigned i = 0; i < 128; ++i) {
        assert(isfinite(pcm[i]));
        energy += pcm[i] * pcm[i];
    }
    audio_sampler_end_block();
    return energy;
}

static void test_preset(void)
{
    startup_preset_t preset = {
        .restore = {.sound = synth.live_sound.current, .config = synth.config,
                    .bars = 4, .restore_config = true},
    };
    preset.bank_sha[0] = 0xa5;
    preset.restore.config.master_gain_permille = 19;
    uint8_t bytes[PRESET_BYTES], roundtrip[PRESET_BYTES];
    startup_preset_t restored;
    assert(preset_encode(&preset, bytes));
    assert(preset_decode(bytes, sizeof(bytes), &restored));
    assert(restored.restore.config.master_gain_permille == 19);
    assert(restored.restore.bars == 4 && restored.restore.sound.engine == SOUND_SF2);
    assert(!restored.restore.sound.bank_generation && restored.bank_sha[0] == 0xa5);
    assert(preset_encode(&restored, roundtrip) && !memcmp(bytes, roundtrip, sizeof(bytes)));
    assert(!preset_decode(bytes, sizeof(bytes) - 1, &restored));
    bytes[3] = '2';
    assert(!preset_decode(bytes, sizeof(bytes), &restored));
    bytes[3] = '1';
    bytes[9] = 1; /* A high byte must not be lost by narrowing the preset index. */
    assert(!preset_decode(bytes, sizeof(bytes), &restored));
    bytes[9] = 0;
    assert(preset_decode(bytes, sizeof(bytes), &restored));
    restored.restore.sound.bank_generation = synth.live_sound.current.bank_generation;
    assert(audio_sampler_resume(&synth, &restored.restore));
    assert(synth.config.master_gain_permille == 19 && looper.bars == 4);
}

static void test_storage_ack(void)
{
    _Atomic storage_phase_t gate = STORAGE_REQUESTED;
    assert(!storage_ack(&gate));
    assert(storage_cancel(&gate, STORAGE_REQUESTED));
    assert(atomic_load(&gate) == STORAGE_IDLE);
    atomic_store(&gate, STORAGE_CLAIMED);
    assert(storage_cancel(&gate, STORAGE_CLAIMED));
    assert(!storage_ack(&gate) && atomic_load(&gate) == STORAGE_CANCELLED);
    atomic_store(&gate, STORAGE_CLAIMED);
    assert(storage_ack(&gate));
    assert(!storage_cancel(&gate, STORAGE_CLAIMED) && atomic_load(&gate) == STORAGE_HELD);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_storage_ack();
    synth_init(&synth, NULL);
    looper_init(&looper, 120);
    synth.looper = &looper;
    synth_set_arp_mode(&synth, SYNTH_ARP_OFF);
    synth_set_delay(&synth, false, 0, 0, 0);
    synth_set_chorus(&synth, false, 0);
    assert(audio_sampler_init(&synth));

    parked = true;
    sf_bank *first = prepare(argv[1]);
    sf_bank *second = prepare(argv[1]);
    parked = false;
    uint64_t gen = attach(first, NULL);
    const unsigned prepared_allocations = allocations;
    audio_sampler_begin_block();
    midi(true);
    assert(render() > 0.0);
    assert(allocations == prepared_allocations);
    audio_sampler_begin_block();
    for (unsigned i = 0; i < 64; ++i) synth.sampler->pitch(synth.sampler->context, 0, 8192);
    midi(false);
    (void)render();
    assert(sf_part_get_stats(first, 0).voice_active == 0); /* Overflow releases voices. */

    /* All four tracks can bind and retain the chosen SoundFont identity. */
    for (unsigned i = 0; i < LOOPER_TRACK_COUNT; ++i) {
        looper_track_trigger(&looper, i, &synth);
        audio_sampler_begin_block();
        midi(true);
        (void)render();
        audio_sampler_begin_block();
        midi(false);
        (void)render();
        looper_track_stop(&looper, i);
        assert(looper.tracks[i].sound.bank_generation == gen);
    }
    assert(looper_sampler_tracks(&looper) == LOOPER_TRACK_COUNT);
    /* The lifecycle refuses a swap if the caller attempts to bypass the RT guard. */
    parked = true;
    assert(audio_storage_stage_bank(second));
    assert(!audio_sampler_resume(&synth, NULL));
    sf_lc_ack ack;
    assert(audio_storage_bank_ack(&ack) && ack.result == SF_LC_BANK_IN_USE && ack.retired == second);
    sf_bank_destroy(ack.retired);
    second = prepare(argv[1]);
    parked = false;
    for (unsigned i = 0; i < LOOPER_TRACK_COUNT; ++i) looper_track_set_preset(&looper, i, 1);
    assert(!looper_sampler_tracks(&looper));
    assert(attach(second, first) > gen);
    const sound_ref_t stale = {.engine = SOUND_SF2, .bank_generation = gen};
    assert(audio_sampler_select(&synth, &stale) == SOUND_STALE);
    test_preset();
    parked = true;
    assert(audio_storage_stage_bank(NULL));
    assert(audio_sampler_resume(&synth, NULL));
    assert(audio_storage_bank_ack(&ack) && ack.result == SF_LC_OK && ack.retired == second);
    sf_bank_destroy(ack.retired);
    parked = false;
    assert(synth.live_sound.current.engine == SOUND_SYNTH && allocations == 0);
    puts("PASS P05b audio adapter: PCM, event cap, four SF2 tracks, swap ACK, stale ID, preset schema, unload");
}
