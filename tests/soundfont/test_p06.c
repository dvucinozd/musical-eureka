#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "audio_sampler.h"
#ifdef NEKTAR_P06_TELEMETRY
#include "audio_perf_internal.h"
#endif

enum { EVENT_BLOCKS = 8, STEADY_BLOCKS = 128, SETUP_BLOCKS = 1 };
static synth_t synth;
static synth_looper_t looper;
static bool realtime;
static unsigned allocations, blocks;
static FILE *pcm_output;

bool audio_storage_held(void)
{
    return !realtime;
}

static void *allocate(void *context, sf_mem_kind kind, size_t size)
{
    (void)context;
    (void)kind;
    assert(!realtime);
    void *ptr = malloc(size);
    if (ptr) ++allocations;
    return ptr;
}

static void release(void *context, sf_mem_kind kind, void *ptr)
{
    (void)context;
    (void)kind;
    assert(!realtime && allocations);
    --allocations;
    free(ptr);
}

static sf_bank *load_bank(const char *path)
{
    uint8_t data[16384];
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t size = fread(data, 1, sizeof(data), file);
    assert(feof(file) && !ferror(file));
    assert(fclose(file) == 0);
    sf2_info_t info;
    const sf_allocator allocator = {.alloc = allocate, .free = release};
    sf_bank *bank = sf_bank_prepare(data, size, false, &allocator, &info);
    assert(bank);
    return bank;
}

static void attach(sf_bank *bank)
{
    uint64_t generation = audio_storage_stage_bank(bank);
    assert(generation);
    const audio_restore_t restore = {.sound = {.engine = SOUND_SF2, .bank_generation = generation}};
    audio_sampler_begin_block();
    assert(audio_sampler_resume(&synth, &restore));
    sf_lc_ack ack;
    assert(audio_storage_bank_ack(&ack) && ack.result == SF_LC_OK && !ack.retired);
}

static void chord(unsigned notes)
{
    for (unsigned i = 0; i < notes; ++i) {
        const midi_event_t event = {.type = MIDI_EVENT_NOTE_ON, .note = {60 + i, 100}};
        assert(synth_sound_event_from(&synth, SYNTH_SOURCE_USB, synth_live_epoch(&synth), &event));
    }
}

static void prepare_tracks(unsigned notes)
{
    for (unsigned track = 0; track < LOOPER_TRACK_COUNT; ++track) {
        looper_track_trigger(&looper, track, &synth);
        looper.cursor_samples = 0;
        for (unsigned i = 0; i < notes; ++i)
            looper_record_note(&looper, 48 + i, 100, true, &synth);
        looper.cursor_samples = 160 * AUDIO_FRAMES;
        for (unsigned i = 0; i < notes; ++i)
            looper_record_note(&looper, 48 + i, 0, false, &synth);
        looper_track_stop(&looper, track);
        assert(looper.tracks[track].event_count == notes * 2);
    }
    assert(looper_sampler_tracks(&looper) == LOOPER_TRACK_COUNT);
    for (unsigned track = 0; track < LOOPER_TRACK_COUNT; ++track)
        looper_track_trigger(&looper, track, &synth);
}

static void begin_block(void)
{
    realtime = true;
#ifdef NEKTAR_P06_TELEMETRY
    /* Synthetic timestamps test accounting only. They are not measurements. */
    audio_perf_begin_block((int64_t)blocks * 2667, 9, &synth.live_sound.current);
#endif
    audio_sampler_begin_block();
}

static void render_block(sf_bank *bank, unsigned live, unsigned track, bool save)
{
    float pcm[AUDIO_FRAMES];
    synth_render(&synth, pcm, AUDIO_FRAMES);
    audio_sampler_end_block();
    assert(sf_part_get_stats(bank, 0).voice_active == live);
    for (unsigned i = 1; i < SOUND_PART_COUNT; ++i)
        assert(sf_part_get_stats(bank, i).voice_active == track);
#ifdef NEKTAR_P06_TELEMETRY
    if (audio_perf_record(1000, 1200, (int64_t)blocks * 2667 + 2200, 9, 0, false,
                          &synth.live_sound.current))
        audio_perf_publish(1234);
#endif
    ++blocks;
    realtime = false;
    if (save) {
        bool sounding = false;
        for (unsigned i = 0; i < AUDIO_FRAMES; ++i) {
            assert(isfinite(pcm[i]));
            sounding |= pcm[i] != 0.0f;
        }
        assert(sounding);
        assert(fwrite(pcm, sizeof(*pcm), AUDIO_FRAMES, pcm_output) == AUDIO_FRAMES);
    }
}

#ifdef NEKTAR_P06_TELEMETRY
static void test_measurement_edges(void)
{
    audio_perf_window_t window;
    const sound_ref_t sound = {.engine = SOUND_SF2, .bank_generation = 1};
    assert(audio_perf_request());
    audio_perf_begin_block(100, UINT32_MAX, &sound);
    assert(!audio_perf_record(AUDIO_PERF_TARGET_US, 3000, 3200, 0, 16, true, &sound));
    audio_perf_begin_block(6100, 0, &sound);
    assert(!audio_perf_record(AUDIO_PERF_DEADLINE_US, 2000, 11000, 1, 0, false, &sound));
    audio_perf_abort(AUDIO_PERF_MAINTENANCE, 12000, 2);
    assert(audio_perf_take(&window));
    assert(window.result == AUDIO_PERF_MAINTENANCE && window.all.blocks == 2);
    assert(window.all.over_target == 1 && window.all.deadlines == 1 && window.invalid_trace == 2);
    assert(window.gap_max_us == 6000 && window.long_gaps == 1 && window.dma_overflows == 3);
    assert(window.write_max_us == 3000 && window.usb_events == 16 && window.web_blocks == 1);
    assert(!window.event16.blocks && !window.steady32.blocks);
    assert(audio_perf_request());
    audio_perf_abort(AUDIO_PERF_FAULT, 20000, 2);
    assert(audio_perf_take(&window) && window.result == AUDIO_PERF_FAULT && !window.all.blocks);
    assert(audio_perf_request());
    audio_perf_begin_block(21000, 2, &sound);
    const sound_ref_t other = {.engine = SOUND_SF2, .bank_generation = 1, .program = 1};
    assert(!audio_perf_record(1000, 100, 22100, 2, 0, false, &other));
    assert(audio_perf_take(&window) && window.result == AUDIO_PERF_SOUND_CHANGED && !window.all.blocks);
}
#endif

int main(int argc, char **argv)
{
    assert(argc == 4);
    unsigned layers = (unsigned)atoi(argv[2]);
    assert(layers == 1 || layers == 2);
    pcm_output = fopen(argv[3], "wb");
    assert(pcm_output);
    synth_init(&synth, NULL);
    looper_init(&looper, 120);
    synth.looper = &looper;
    synth_set_arp_mode(&synth, SYNTH_ARP_OFF);
    synth_set_master_gain(&synth, 4);
    synth_set_delay(&synth, false, 0, 0, 0);
    synth_set_chorus(&synth, false, 0);
    assert(audio_sampler_init(&synth));
    sf_bank *bank = load_bank(argv[1]);
    attach(bank);
    const unsigned active_allocations = allocations;

#ifdef NEKTAR_P06_TELEMETRY
    assert(audio_perf_request() && !audio_perf_request());
#endif
    for (unsigned i = 0; i < EVENT_BLOCKS; ++i) {
        synth_panic(&synth);
        begin_block();
        chord(16 / layers);
        render_block(bank, 16, 0, true);
    }
    synth_panic(&synth);
    audio_sampler_begin_block();
    prepare_tracks(4 / layers);
    begin_block();
    chord(16 / layers);
    render_block(bank, 16, 4, true);
    for (unsigned i = 0; i < STEADY_BLOCKS; ++i) {
        begin_block();
        render_block(bank, 16, 4, true);
    }
    assert(blocks == EVENT_BLOCKS + SETUP_BLOCKS + STEADY_BLOCKS);
    assert(allocations == active_allocations);
    synth_panic(&synth);
    looper_clear_all(&looper);
#ifdef NEKTAR_P06_TELEMETRY
    while (blocks < AUDIO_PERF_BLOCKS) {
        begin_block();
        render_block(bank, 0, 0, false);
    }
    audio_perf_window_t window;
    assert(audio_perf_take(&window) && !audio_perf_take(&window));
    assert(window.result == AUDIO_PERF_COMPLETE && window.id == 1 && window.all.blocks == AUDIO_PERF_BLOCKS);
    assert(window.event16.blocks == EVENT_BLOCKS && window.steady32.blocks == STEADY_BLOCKS);
    assert(!window.invalid_trace && !window.all.deadlines && !window.dma_overflows);
    assert(window.stack_high_water == 1234);
    test_measurement_edges();
#endif
    assert(audio_storage_stage_bank(NULL));
    audio_sampler_begin_block();
    assert(audio_sampler_resume(&synth, NULL));
    sf_lc_ack ack;
    assert(audio_storage_bank_ack(&ack) && ack.result == SF_LC_OK && ack.retired == bank);
    sf_bank_destroy(ack.retired);
    assert(!allocations && fclose(pcm_output) == 0);
    printf("PASS fixture=%s layers=%u event16=%u steady32=%u RT alloc/free=0\n",
           argv[1], layers, EVENT_BLOCKS, STEADY_BLOCKS);
    return 0;
}
