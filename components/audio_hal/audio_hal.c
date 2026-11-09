#include "audio_hal.h"

#include <math.h>
#include <stdatomic.h>

#include "board_audio.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "midi_ingress.h"
#include "midi_parser.h"
#include "midi_queue.h"
#include "synth.h"
#include "synth_looper.h"
#include "usb_midi_host.h"

enum {
    DMA_BLOCKS = 6,
    PROBE_LEAD_BLOCKS = 12,
    PROBE_HOLD_BLOCKS = 75,
    PROBE_RELEASE_BLOCKS = 30,
    PROBE_END = PROBE_LEAD_BLOCKS + PROBE_HOLD_BLOCKS + PROBE_RELEASE_BLOCKS + DMA_BLOCKS,
};

static const char *TAG = "S31_AUDIO";
static const board_audio_pins_t *s_pins;
static i2s_chan_handle_t s_tx;
static synth_t s_synth;
static synth_looper_t s_looper;
static float s_mono[AUDIO_FRAMES];
static int16_t s_pcm[AUDIO_FRAMES][2];
static StaticTask_t s_task;
static StackType_t s_stack[4096];
static _Atomic audio_state_t s_state;
static _Atomic uint32_t s_blocks, s_prepare_max_us, s_prepare_deadlines;
static _Atomic uint32_t s_dma_overflows, s_probes_completed;
static _Atomic uint32_t s_midi_applied, s_midi_rejected, s_midi_panics, s_midi_dispatch_max;
static atomic_bool s_live_started;
static _Atomic esp_err_t s_fault;

_Static_assert(SYNTH_SAMPLE_RATE == AUDIO_RATE, "Synth and I2S rates must match");
_Static_assert(LOOPER_TRACK_COUNT == 4, "The instrument retains four MIDI tracks");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "DMA callback requires lock-free counters");

static esp_err_t set_amplifier(bool enabled)
{
    return gpio_set_level(s_pins->pa_enable, enabled == s_pins->pa_active_high);
}

static void latch_fault(esp_err_t fault)
{
    (void)set_amplifier(false);
    atomic_store(&s_fault, fault);
    atomic_store(&s_state, AUDIO_FAULT);
}

static bool tx_overflow(i2s_chan_handle_t chan, i2s_event_data_t *event, void *context)
{
    (void)chan;
    (void)event;
    (void)context;
    atomic_fetch_add_explicit(&s_dma_overflows, 1, memory_order_relaxed);
    return false;
}

/* Exercise the project MIDI parser and the synth MIDI entry point. */
static void probe_note(bool down)
{
    const uint8_t packet[] = {down ? 0x09 : 0x08, down ? 0x90 : 0x80, 60, down ? 64 : 0};
    midi_event_t event;
    if (midi_parser_parse_usb_packet(packet, 0, esp_timer_get_time(), &event)) {
        synth_handle_midi_event(&s_synth, &event);
    }
}

static void audio_task(void *context)
{
    (void)context;
    unsigned probe_block = 0;
    midi_ingress_state_t midi = {0};
    esp_err_t err = i2s_channel_enable(s_tx);
    while (err == ESP_OK) {
        int64_t start = esp_timer_get_time();
        audio_state_t expected = AUDIO_MUTED;
        if (usb_midi_host_active_session() &&
            atomic_compare_exchange_strong(&s_state, &expected, AUDIO_LIVE)) {
            atomic_store(&s_live_started, true);
            err = set_amplifier(true);
            if (err != ESP_OK) break;
        }
        const audio_state_t mode = atomic_load(&s_state);
        midi_ingress_drain(&midi, &s_synth, mode == AUDIO_LIVE);
        atomic_store(&s_midi_applied, midi.applied);
        atomic_store(&s_midi_rejected, midi.rejected);
        atomic_store(&s_midi_panics, midi.panics);
        atomic_store(&s_midi_dispatch_max, midi.receive_to_dispatch_max_us);
        if (mode == AUDIO_LIVE && !usb_midi_host_active_session()) {
            err = set_amplifier(false);
            if (err != ESP_OK) break;
            atomic_store(&s_state, AUDIO_MUTED);
        }

        const bool probing = mode == AUDIO_PROBE;
        if (probing) {
            if (probe_block == 0) {
                err = set_amplifier(true);
                if (err != ESP_OK) break;
            } else if (probe_block == PROBE_LEAD_BLOCKS) {
                probe_note(true);
            } else if (probe_block == PROBE_LEAD_BLOCKS + PROBE_HOLD_BLOCKS) {
                probe_note(false);
            }
        }

        synth_render(&s_synth, s_mono, AUDIO_FRAMES);
        for (unsigned i = 0; i < AUDIO_FRAMES; ++i) {
            float sample = s_mono[i];
            if (!isfinite(sample)) {
                err = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            s_pcm[i][0] = (int16_t)(fmaxf(-1.0f, fminf(1.0f, sample)) * 32767.0f);
            s_pcm[i][1] = 0;
        }
        if (err != ESP_OK) break;

        uint32_t prepare_us = (uint32_t)(esp_timer_get_time() - start);
        if (prepare_us > atomic_load(&s_prepare_max_us)) {
            atomic_store(&s_prepare_max_us, prepare_us);
        }
        if (prepare_us >= 2667) {
            atomic_fetch_add(&s_prepare_deadlines, 1);
        }

        size_t written = 0;
        err = i2s_channel_write(s_tx, s_pcm, sizeof(s_pcm), &written, 20);
        if (err != ESP_OK) break;
        if (written != sizeof(s_pcm)) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        unsigned blocks = atomic_fetch_add(&s_blocks, 1) + 1;
        if (blocks == DMA_BLOCKS) {
            atomic_store(&s_state, AUDIO_MUTED);
        }
        if (probing && ++probe_block == PROBE_END) {
            err = set_amplifier(false);
            if (err != ESP_OK) break;
            probe_block = 0;
            atomic_fetch_add(&s_probes_completed, 1);
            atomic_store(&s_state, AUDIO_MUTED);
        }
    }
    latch_fault(err);
    (void)i2s_channel_disable(s_tx);
    vTaskDelete(NULL);
}

esp_err_t audio_start(void)
{
    if (atomic_load(&s_state) != AUDIO_OFF) return ESP_ERR_INVALID_STATE;
    s_pins = board_audio_pins();
    esp_err_t err = set_amplifier(false);
    if (err == ESP_OK) err = gpio_set_direction(s_pins->pa_enable, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        latch_fault(err);
        return err;
    }

    i2c_master_bus_handle_t bus = NULL;
    const audio_codec_ctrl_if_t *ctrl = NULL;
    const audio_codec_if_t *codec = NULL;
    bool tx_enabled = false;
    err = midi_queue_init();
    if (err != ESP_OK) goto fail;
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = 0,
        .sda_io_num = s_pins->sda,
        .scl_io_num = s_pins->scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&i2c_config, &bus);
    if (err != ESP_OK) goto fail;
    err = i2c_master_probe(bus, s_pins->codec_address_7bit, 100);
    if (err != ESP_OK) goto fail;
    ESP_LOGI(TAG, "ES8311 ACK address_7bit=0x%02x", s_pins->codec_address_7bit);

    audio_codec_i2c_cfg_t ctrl_config = {
        .port = 0,
        .addr = s_pins->codec_address_7bit << 1,
        .bus_handle = bus,
        .clock_speed_hz = 100000,
    };
    ctrl = audio_codec_new_i2c_ctrl(&ctrl_config);
    if (!ctrl) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = DMA_BLOCKS;
    channel.dma_frame_num = AUDIO_FRAMES;
    channel.auto_clear_after_cb = true;
    err = i2s_new_channel(&channel, &s_tx, NULL);
    if (err != ESP_OK) goto fail;
    i2s_std_config_t format = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = s_pins->mclk, .bclk = s_pins->bclk, .ws = s_pins->ws,
            .dout = s_pins->dout, .din = I2S_GPIO_UNUSED,
        },
    };
    format.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    err = i2s_channel_init_std_mode(s_tx, &format);
    if (err != ESP_OK) goto fail;
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) goto fail;
    tx_enabled = true;

    /* Configure ES8311 with MCLK present. GPIO57 is owned by this HAL. */
    es8311_codec_cfg_t codec_config = {
        .ctrl_if = ctrl,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .use_mclk = true,
        .mclk_div = 256,
    };
    codec = es8311_codec_new(&codec_config);
    if (!codec) {
        err = ESP_FAIL;
        goto fail;
    }
    esp_codec_dev_sample_info_t samples = {
        .sample_rate = AUDIO_RATE, .bits_per_sample = 16, .channel = 1,
    };
    int sdp = 0;
    if (codec->set_fs(codec, &samples) || codec->enable(codec, true) ||
        codec->set_vol(codec, -12.0f) || codec->mute(codec, false) ||
        codec->get_reg(codec, 0x09, &sdp) || (sdp & 0x1f) != 0x0c) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }
    ESP_LOGI(TAG, "ES8311 SDP=0x%02x rate=%u frames=%u slot=left", sdp, AUDIO_RATE, AUDIO_FRAMES);

    err = i2s_channel_disable(s_tx);
    if (err != ESP_OK) goto fail;
    tx_enabled = false;
    i2s_event_callbacks_t callbacks = {.on_send_q_ovf = tx_overflow};
    err = i2s_channel_register_event_callback(s_tx, &callbacks, NULL);
    if (err != ESP_OK) goto fail;

    synth_init(&s_synth, NULL);
    looper_init(&s_looper, s_synth.arp.bpm);
    s_synth.looper = &s_looper;
    synth_set_waveform(&s_synth, SYNTH_WAVE_SINE);
    synth_set_adsr(&s_synth, 10, 25, 0.6f, 50);
    synth_set_master_gain(&s_synth, 2);
    synth_set_arp_mode(&s_synth, SYNTH_ARP_OFF);
    synth_set_delay(&s_synth, false, 0, 0, 0);
    synth_set_chorus(&s_synth, false, 0);
    synth_set_lfo(&s_synth, false, 0, 0);

    atomic_store(&s_state, AUDIO_PRIMING);
    if (!xTaskCreateStaticPinnedToCore(audio_task, "audio", sizeof(s_stack), NULL,
                                       20, s_stack, &s_task, 1)) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    return ESP_OK;

fail:
    latch_fault(err);
    if (codec) (void)audio_codec_delete_codec_if(codec);
    if (tx_enabled) (void)i2s_channel_disable(s_tx);
    if (s_tx) (void)i2s_del_channel(s_tx);
    if (ctrl) (void)audio_codec_delete_ctrl_if(ctrl);
    if (bus) (void)i2c_del_master_bus(bus);
    return err;
}

bool audio_request_probe(void)
{
    /* MIDI may change the preset and volume. Preserve the fixed P02 probe. */
    if (atomic_load(&s_live_started) || usb_midi_host_active_session()) return false;
    audio_state_t expected = AUDIO_MUTED;
    return atomic_compare_exchange_strong(&s_state, &expected, AUDIO_PROBE);
}

audio_status_t audio_get_status(void)
{
    return (audio_status_t){
        .state = atomic_load(&s_state),
        .blocks = atomic_load(&s_blocks),
        .prepare_max_us = atomic_load(&s_prepare_max_us),
        .prepare_deadlines = atomic_load(&s_prepare_deadlines),
        .dma_overflows = atomic_load(&s_dma_overflows),
        .probes_completed = atomic_load(&s_probes_completed),
        .midi_applied = atomic_load(&s_midi_applied),
        .midi_rejected = atomic_load(&s_midi_rejected),
        .midi_queue_drops = midi_queue_dropped_count(),
        .midi_panics = atomic_load(&s_midi_panics),
        .midi_receive_to_dispatch_max_us = atomic_load(&s_midi_dispatch_max),
        .fault = atomic_load(&s_fault),
    };
}
