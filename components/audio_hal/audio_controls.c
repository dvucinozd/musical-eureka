#include "audio_controls_internal.h"
#include "audio_sampler.h"
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "synth_web_keys.h"

enum { CONTROL_DEPTH = 32, CONTROL_BUDGET = 8 };
static StaticQueue_t s_queue_state, s_status_state;
static uint8_t s_queue_data[CONTROL_DEPTH * sizeof(audio_command_t)];
static uint8_t s_status_data[sizeof(audio_control_status_t)];
static QueueHandle_t s_queue, s_status;
static _Atomic uint32_t s_epoch;
static _Atomic uint32_t s_storage_epoch;
static synth_web_keys_t s_keys;

esp_err_t audio_controls_init(void)
{
    s_queue = xQueueCreateStatic(CONTROL_DEPTH, sizeof(audio_command_t), s_queue_data, &s_queue_state);
    s_status = xQueueCreateStatic(1, sizeof(audio_control_status_t), s_status_data, &s_status_state);
    return s_queue && s_status ? ESP_OK : ESP_ERR_NO_MEM;
}

bool audio_submit_control(audio_command_t command)
{
    command.storage_epoch = atomic_load(&s_storage_epoch);
    command.epoch = atomic_load(&s_epoch);
    if (!audio_enable_live_control()) return false;
    return xQueueSend(s_queue, &command, 0) == pdTRUE;
}

bool audio_get_controls(audio_control_status_t *status)
{
    return s_status && xQueuePeek(s_status, status, 0) == pdTRUE;
}

static void apply(synth_t *s, const audio_command_t *c, int64_t now)
{
    const synth_config_t *cfg = &s->config;
    switch (c->kind) {
    case AUDIO_CMD_PRESS:
    case AUDIO_CMD_RELEASE:
        if (c->epoch == synth_live_epoch(s))
            synth_web_key(&s_keys, s, c->index, c->kind == AUDIO_CMD_PRESS, now);
        break;
    case AUDIO_CMD_HOLD: synth_web_keys_hold_mask(&s_keys, s, c->mask, c->epoch, now); break;
    case AUDIO_CMD_PANIC: synth_panic(s); break;
    case AUDIO_CMD_PRESET:
        if (s->live_sound.current.engine == SOUND_SF2) {
            const sound_ref_t sound = {.synth_preset = c->value};
            (void)audio_sampler_select(s, &sound);
        } else synth_set_preset(s, c->value);
        break;
    case AUDIO_CMD_SOUND: (void)audio_sampler_select(s, &c->sound); break;
    case AUDIO_CMD_GAIN: synth_set_master_gain(s, c->value); break;
    case AUDIO_CMD_WAVE: synth_set_waveform(s, c->value); break;
    case AUDIO_CMD_CUTOFF: synth_set_cutoff(s, c->value); break;
    case AUDIO_CMD_RESONANCE: synth_set_resonance(s, c->value / 10.0f); break;
    case AUDIO_CMD_FILTER: synth_set_filter_type(s, c->value); break;
    case AUDIO_CMD_ATTACK: synth_set_adsr(s, c->value, cfg->decay_ms, cfg->sustain_ratio, cfg->release_ms); break;
    case AUDIO_CMD_DECAY: synth_set_adsr(s, cfg->attack_ms, c->value, cfg->sustain_ratio, cfg->release_ms); break;
    case AUDIO_CMD_SUSTAIN: synth_set_adsr(s, cfg->attack_ms, cfg->decay_ms, c->value / 100.0f, cfg->release_ms); break;
    case AUDIO_CMD_ENV_RELEASE: synth_set_adsr(s, cfg->attack_ms, cfg->decay_ms, cfg->sustain_ratio, c->value); break;
    case AUDIO_CMD_DELAY: synth_set_delay(s, c->value, 280, 0.45f, 0.3f); break;
    case AUDIO_CMD_CHORUS: synth_set_chorus(s, c->value, 0.35f); break;
    case AUDIO_CMD_LFO: synth_set_lfo(s, c->value, 5.0f, 0.5f); break;
    case AUDIO_CMD_ARP: synth_set_arp_mode(s, c->value); break;
    case AUDIO_CMD_ARP_RATE: synth_set_arp_rate(s, c->value); break;
    case AUDIO_CMD_ARP_OCTAVES: synth_set_arp_octaves(s, c->value); break;
    case AUDIO_CMD_ARP_LATCH: synth_set_arp_latch(s, c->value); break;
    case AUDIO_CMD_BPM: synth_set_arp_bpm(s, c->value); break;
    case AUDIO_CMD_LOOP_BARS: looper_set_bars(s->looper, c->value); break;
    case AUDIO_CMD_LOOP_TRIGGER: looper_track_trigger(s->looper, c->index, s); break;
    case AUDIO_CMD_LOOP_STOP: looper_track_stop(s->looper, c->index); break;
    case AUDIO_CMD_LOOP_CLEAR: looper_track_clear(s->looper, c->index); break;
    case AUDIO_CMD_LOOP_MUTE: looper_track_set_mute(s->looper, c->index, c->value); break;
    case AUDIO_CMD_LOOP_VOLUME: looper_track_set_volume(s->looper, c->index, c->value); break;
    case AUDIO_CMD_LOOP_PRESET: looper_track_set_preset(s->looper, c->index, c->value); break;
    }
}

bool audio_controls_drain(synth_t *s, int64_t now)
{
    audio_command_t command;
    bool applied = false;
    for (unsigned n = 0; n < CONTROL_BUDGET && xQueueReceive(s_queue, &command, 0) == pdTRUE; ++n) {
        if (command.storage_epoch != atomic_load(&s_storage_epoch)) continue;
        apply(s, &command, now);
        applied = true;
    }
    synth_web_keys_expire(&s_keys, s, now);
    atomic_store(&s_epoch, synth_live_epoch(s));
    return applied;
}

void audio_controls_publish(const synth_t *s)
{
    audio_control_status_t out = {
        .config = s->config, .preset = s->current_preset, .sound = s->live_sound.current,
    };
    looper_get_status(s->looper, &out.looper);
    xQueueOverwrite(s_status, &out);
}

bool audio_controls_pending(void)
{
    return uxQueueMessagesWaiting(s_queue) != 0;
}

void audio_controls_discard(synth_t *s)
{
    atomic_fetch_add(&s_storage_epoch, 1);
    xQueueReset(s_queue);
    synth_web_keys_expire(&s_keys, s, 0);
    atomic_store(&s_epoch, synth_live_epoch(s));
}
