#include "bt_output.h"

#include <string.h>

#include "esp_a2dp_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

enum { QUEUE_DEPTH = 16, SCAN_SECONDS = 12, CONNECT_SECONDS = 20, CLOSE_SECONDS = 5 };

typedef enum {
    CMD_SCAN, CMD_CONNECT, CMD_DISCONNECT, EV_INIT, EV_FOUND, EV_SCAN_END, EV_CONNECTED,
    EV_DISCONNECTED, EV_FORMAT, EV_CHECKED, EV_START_ACK, EV_AUDIO, EV_CONFIRM, EV_PIN, EV_AUTH,
} message_kind_t;

typedef struct {
    message_kind_t kind;
    bt_output_device_t device;
    unsigned value;
} message_t;

static bt_pcm_t s_pcm;
static QueueHandle_t s_messages, s_status;
static StaticQueue_t s_message_queue, s_status_queue;
static uint8_t s_message_data[QUEUE_DEPTH * sizeof(message_t)], s_status_data[sizeof(bt_output_status_t)];
static _Atomic unsigned s_format, s_route, s_start_failed;
static atomic_bool s_requested, s_lost_event;

static unsigned active_format(void)
{
    if (atomic_load_explicit(&s_lost_event, memory_order_relaxed)) return 0;
    return atomic_load_explicit(&s_format, memory_order_relaxed);
}

/* Pinned Bluedroid config calls these two OSI symbols from btc_config.c.
 * Keep Classic link keys in the existing in-memory config for this boot.
 * The SDK and the instrument's NVS path remain separate. */
struct config_t;
struct config_t *__wrap_config_new(const char *name)
{
    (void)name;
    return NULL;
}

bool __wrap_config_save(const struct config_t *config, const char *name)
{
    (void)config;
    (void)name;
    return true;
}

static void post(const message_t *msg)
{
    if (xQueueSend(s_messages, msg, 0) != pdTRUE && msg->kind != EV_FOUND) {
        atomic_store(&s_format, 0);
        atomic_store(&s_lost_event, true);
    }
}

static void discovery_result(const esp_bt_gap_cb_param_t *param)
{
    const uint8_t *name = NULL, *eir = NULL;
    size_t length = 0;
    uint32_t cod = 0;
    for (int i = 0; i < param->disc_res.num_prop; ++i) {
        const esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
        if (p->type == ESP_BT_GAP_DEV_PROP_COD && p->len == sizeof(cod)) memcpy(&cod, p->val, sizeof(cod));
        if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->len > 0) {
            name = p->val;
            length = p->len;
        }
        if (p->type == ESP_BT_GAP_DEV_PROP_EIR && p->len >= ESP_BT_GAP_EIR_DATA_LEN) eir = p->val;
    }
    if (!(esp_bt_gap_get_cod_srvc(cod) & ESP_BT_COD_SRVC_RENDERING) &&
        esp_bt_gap_get_cod_major_dev(cod) != ESP_BT_COD_MAJOR_DEV_AV) return;
    if (!name && eir) {
        uint8_t size = 0;
        name = esp_bt_gap_resolve_eir_data((uint8_t *)eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &size);
        if (!name) name = esp_bt_gap_resolve_eir_data((uint8_t *)eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &size);
        length = size;
    }
    message_t msg = {.kind = EV_FOUND};
    memcpy(msg.device.address, param->disc_res.bda, BT_ADDRESS_BYTES);
    for (size_t i = 0; name && i < length && i < BT_NAME_BYTES - 1 && name[i]; ++i) {
        const uint8_t c = name[i];
        msg.device.name[i] = (c >= 32 && c <= 126 && c != '"' && c != '\\') ? (char)c : '?';
    }
    post(&msg);
}

static void gap_event(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    message_t msg = {0};
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        discovery_result(param);
        return;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state != ESP_BT_GAP_DISCOVERY_STOPPED) return;
        msg.kind = EV_SCAN_END;
        break;
    case ESP_BT_GAP_CFM_REQ_EVT:
        msg.kind = EV_CONFIRM;
        memcpy(msg.device.address, param->cfm_req.bda, BT_ADDRESS_BYTES);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT:
        msg.kind = EV_PIN;
        memcpy(msg.device.address, param->pin_req.bda, BT_ADDRESS_BYTES);
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        msg.kind = EV_AUTH;
        msg.value = param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS;
        memcpy(msg.device.address, param->auth_cmpl.bda, BT_ADDRESS_BYTES);
        break;
    default:
        return;
    }
    post(&msg);
}

static unsigned codec_format(const esp_a2d_mcc_t *codec)
{
    if (codec->type != ESP_A2D_MCT_SBC) return 0;
    const esp_a2d_cie_sbc_t *sbc = &codec->cie.sbc_info;
    unsigned rate = 0, channels = 0;
    if (sbc->samp_freq == ESP_A2D_SBC_CIE_SF_44K) rate = 44100;
    if (sbc->samp_freq == ESP_A2D_SBC_CIE_SF_48K) rate = 48000;
    if (sbc->ch_mode == ESP_A2D_SBC_CIE_CH_MODE_MONO) channels = 1;
    if (sbc->ch_mode == ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL ||
        sbc->ch_mode == ESP_A2D_SBC_CIE_CH_MODE_STEREO ||
        sbc->ch_mode == ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO) channels = 2;
    return rate && channels ? (rate << 2) | channels : 0;
}

static void a2dp_event(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    message_t msg = {0};
    switch (event) {
    case ESP_A2D_PROF_STATE_EVT:
        msg.kind = EV_INIT;
        msg.value = param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS;
        break;
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) msg.kind = EV_CONNECTED;
        else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            msg.kind = EV_DISCONNECTED;
            atomic_store(&s_format, 0);
        } else return;
        memcpy(msg.device.address, param->conn_stat.remote_bda, BT_ADDRESS_BYTES);
        break;
    case ESP_A2D_AUDIO_CFG_EVT: {
        msg.kind = EV_FORMAT;
        msg.value = codec_format(&param->audio_cfg.mcc);
        const unsigned previous = atomic_load(&s_format);
        if (previous && previous != msg.value) atomic_store(&s_format, 0);
        memcpy(msg.device.address, param->audio_cfg.remote_bda, BT_ADDRESS_BYTES);
        break;
    }
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) msg.kind = EV_CHECKED;
        else if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START) msg.kind = EV_START_ACK;
        else return;
        msg.value = param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS;
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        msg.kind = EV_AUDIO;
        msg.value = param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED;
        if (!msg.value) atomic_store(&s_format, 0);
        memcpy(msg.device.address, param->audio_stat.remote_bda, BT_ADDRESS_BYTES);
        break;
    default:
        return;
    }
    post(&msg);
}

static int32_t a2dp_pcm(uint8_t *data, int32_t len)
{
    if (!data || len <= 0) return 0;
    unsigned format = active_format();
    bt_pcm_read(&s_pcm, data, (size_t)len, format >> 2, format & 3);
    /* A disconnect can arrive while the callback is copying a batch. */
    if (format != active_format()) memset(data, 0, (size_t)len);
    return len;
}

typedef struct {
    bt_output_status_t view;
    unsigned format;
    int64_t deadline, retry_at;
    bool connected, awaiting_ack;
} session_t;

static int device_index(const bt_output_status_t *view, const uint8_t *addr)
{
    for (unsigned i = 0; i < view->count; ++i)
        if (!memcmp(view->devices[i].address, addr, BT_ADDRESS_BYTES)) return i;
    return -1;
}

static void close_session(session_t *s, bt_output_error_t error, int64_t now)
{
    atomic_store(&s_format, 0);
    s->view.error = error;
    if (esp_a2d_source_disconnect(s->view.peer) == ESP_OK) {
        s->view.state = BT_CLOSING;
        s->deadline = now + CLOSE_SECONDS * INT64_C(1000000);
    } else {
        s->view.state = BT_FAILED;
    }
}

static void update_session(session_t *s, message_t *m, int64_t now)
{
    const bt_output_state_t state = s->view.state;
    const bool peer = !memcmp(s->view.peer, m->device.address, BT_ADDRESS_BYTES);
    switch (m->kind) {
    case CMD_SCAN:
        if (state != BT_IDLE) break;
        s->view.error = BT_ERROR_NONE;
        s->view.count = 0;
        if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 6, 0) == ESP_OK) {
            s->view.state = BT_SCANNING;
            s->deadline = now + SCAN_SECONDS * INT64_C(1000000);
        } else s->view.error = BT_ERROR_SCAN;
        break;
    case CMD_CONNECT:
        if (state != BT_IDLE || device_index(&s->view, m->device.address) < 0) break;
        memcpy(s->view.peer, m->device.address, BT_ADDRESS_BYTES);
        s->connected = s->awaiting_ack = false;
        s->format = 0;
        s->retry_at = now;
        s->view.error = BT_ERROR_NONE;
        if (esp_a2d_source_connect(s->view.peer) == ESP_OK) {
            s->view.state = BT_CONNECTING;
            s->deadline = now + CONNECT_SECONDS * INT64_C(1000000);
        } else s->view.error = BT_ERROR_CONNECT;
        break;
    case CMD_DISCONNECT:
        if (state == BT_CONNECTING || state == BT_STREAMING) close_session(s, BT_ERROR_NONE, now);
        break;
    case EV_INIT:
        if (state == BT_BOOTING) {
            s->view.state = m->value ? BT_IDLE : BT_FAILED;
            s->view.error = m->value ? BT_ERROR_NONE : BT_ERROR_START;
        }
        break;
    case EV_FOUND:
        if (state == BT_SCANNING) {
            int i = device_index(&s->view, m->device.address);
            if (i >= 0 && m->device.name[0]) s->view.devices[i] = m->device;
            if (i < 0 && s->view.count < BT_OUTPUT_DEVICES) s->view.devices[s->view.count++] = m->device;
        }
        break;
    case EV_SCAN_END:
        if (state == BT_SCANNING) s->view.state = BT_IDLE;
        break;
    case EV_CONNECTED:
        if (!peer) (void)esp_a2d_source_disconnect(m->device.address);
        else if (state == BT_CONNECTING) s->connected = true;
        break;
    case EV_DISCONNECTED:
        if (peer && (state == BT_CONNECTING || state == BT_STREAMING || state == BT_CLOSING)) {
            if (state == BT_STREAMING) s->view.error = BT_ERROR_LOST;
            if (state == BT_CONNECTING) s->view.error = BT_ERROR_CONNECT;
            s->view.state = BT_IDLE;
            memset(s->view.peer, 0, BT_ADDRESS_BYTES);
        }
        break;
    case EV_FORMAT:
        if (!peer) break;
        if (state == BT_CONNECTING && m->value) s->format = m->value;
        else if (state == BT_CONNECTING || (state == BT_STREAMING && m->value != s->format))
            close_session(s, BT_ERROR_CODEC, now);
        break;
    case EV_CHECKED:
        if (state == BT_CONNECTING && s->awaiting_ack) {
            if (!m->value) s->awaiting_ack = false;
            else if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START) != ESP_OK)
                close_session(s, BT_ERROR_CONNECT, now);
        }
        break;
    case EV_START_ACK:
        if (state == BT_CONNECTING && !m->value) close_session(s, BT_ERROR_CONNECT, now);
        break;
    case EV_AUDIO:
        if (!peer) break;
        if (state == BT_CONNECTING && m->value && s->connected && s->format) {
            s->view.state = BT_STREAMING;
            atomic_store(&s_format, s->format);
        } else if (state == BT_STREAMING && !m->value) close_session(s, BT_ERROR_LOST, now);
        break;
    case EV_CONFIRM:
        (void)esp_bt_gap_ssp_confirm_reply(m->device.address, peer && state == BT_CONNECTING);
        break;
    case EV_PIN: {
        esp_bt_pin_code_t pin = {0};
        (void)esp_bt_gap_pin_reply(m->device.address, false, 0, pin);
        if (peer && state == BT_CONNECTING) close_session(s, BT_ERROR_PAIRING, now);
        break;
    }
    case EV_AUTH:
        if (peer && state == BT_CONNECTING && !m->value) close_session(s, BT_ERROR_PAIRING, now);
        break;
    }
}

static void worker(void *arg)
{
    (void)arg;
    session_t s = {.deadline = esp_timer_get_time() + CLOSE_SECONDS * INT64_C(1000000)};
    for (;;) {
        message_t message;
        const bool got = xQueueReceive(s_messages, &message, pdMS_TO_TICKS(50)) == pdTRUE;
        const int64_t now = esp_timer_get_time();
        if (atomic_load(&s_lost_event) && s.view.error != BT_ERROR_QUEUE) {
            atomic_store(&s_format, 0);
            if (s.view.state == BT_SCANNING) (void)esp_bt_gap_cancel_discovery();
            if (s.view.state == BT_CONNECTING || s.view.state == BT_STREAMING)
                close_session(&s, BT_ERROR_QUEUE, now);
            s.view.state = BT_FAILED;
            s.view.error = BT_ERROR_QUEUE;
        }
        if (got) update_session(&s, &message, now);
        if (s.view.state == BT_CONNECTING) {
            if (now >= s.deadline) close_session(&s, BT_ERROR_TIMEOUT, now);
            else if (s.connected && s.format && !s.awaiting_ack && now >= s.retry_at) {
                s.awaiting_ack = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) == ESP_OK;
                s.retry_at = now + 250000;
            }
        } else if (now >= s.deadline) {
            if (s.view.state == BT_SCANNING) (void)esp_bt_gap_cancel_discovery();
            if (s.view.state == BT_BOOTING || s.view.state == BT_SCANNING || s.view.state == BT_CLOSING) {
                s.view.state = BT_FAILED;
                s.view.error = BT_ERROR_TIMEOUT;
            }
        }
        xQueueOverwrite(s_status, &s.view);
    }
}

esp_err_t bt_output_start(void)
{
    s_messages = xQueueCreateStatic(QUEUE_DEPTH, sizeof(message_t), s_message_data, &s_message_queue);
    s_status = xQueueCreateStatic(1, sizeof(bt_output_status_t), s_status_data, &s_status_queue);
    if (!s_messages || !s_status) goto fail;
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bluedroid_config_t host = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    uint8_t io = ESP_BT_IO_CAP_NONE;
    if (esp_bt_controller_init(&controller) != ESP_OK ||
        esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK ||
        esp_bluedroid_init_with_cfg(&host) != ESP_OK || esp_bluedroid_enable() != ESP_OK ||
        esp_bt_gap_register_callback(gap_event) != ESP_OK ||
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE) != ESP_OK ||
        esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof(io)) != ESP_OK ||
        esp_a2d_register_callback(a2dp_event) != ESP_OK ||
        esp_a2d_source_init() != ESP_OK ||
        esp_a2d_source_register_data_callback(a2dp_pcm) != ESP_OK ||
        xTaskCreatePinnedToCore(worker, "a2dp_ctl", 3072, NULL, 5, NULL, 0) != pdPASS) goto fail;
    return ESP_OK;
fail:
    atomic_store(&s_start_failed, 1);
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) (void)esp_bluedroid_disable();
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) (void)esp_bluedroid_deinit();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) (void)esp_bt_controller_disable();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) (void)esp_bt_controller_deinit();
    return ESP_FAIL;
}

bt_output_status_t bt_output_status(void)
{
    bt_output_status_t out = {0};
    if (s_status) (void)xQueuePeek(s_status, &out, 0);
    if (atomic_load(&s_start_failed)) {
        out.state = BT_FAILED;
        out.error = BT_ERROR_START;
    }
    const unsigned format = active_format();
    out.ready = format != 0;
    out.rate = format >> 2;
    out.channels = format & 3;
    out.requested = atomic_load(&s_requested);
    out.route = atomic_load(&s_route);
    out.dropped_blocks = atomic_load(&s_pcm.dropped_blocks);
    out.silent_frames = atomic_load(&s_pcm.silent_frames);
    return out;
}

static esp_err_t submit(const message_t *message)
{
    return s_messages && xQueueSend(s_messages, message, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t bt_output_scan(void)
{
    const bt_output_status_t view = bt_output_status();
    const message_t message = {.kind = CMD_SCAN};
    return view.state == BT_IDLE ? submit(&message) : ESP_ERR_INVALID_STATE;
}

esp_err_t bt_output_connect(const uint8_t address[BT_ADDRESS_BYTES])
{
    const bt_output_status_t view = bt_output_status();
    if (view.state != BT_IDLE) return ESP_ERR_INVALID_STATE;
    if (device_index(&view, address) < 0) return ESP_ERR_NOT_FOUND;
    message_t message = {.kind = CMD_CONNECT};
    memcpy(message.device.address, address, BT_ADDRESS_BYTES);
    return submit(&message);
}

esp_err_t bt_output_disconnect(void)
{
    const bt_output_status_t view = bt_output_status();
    const message_t message = {.kind = CMD_DISCONNECT};
    return view.state == BT_CONNECTING || view.state == BT_STREAMING
        ? submit(&message) : ESP_ERR_INVALID_STATE;
}

esp_err_t bt_output_select(bool wireless)
{
    if (wireless && !active_format()) return ESP_ERR_INVALID_STATE;
    atomic_store(&s_requested, wireless);
    return ESP_OK;
}

bt_route_sink_t bt_output_target(void)
{
    return bt_route_target(atomic_load_explicit(&s_requested, memory_order_relaxed), active_format() != 0);
}

void bt_output_route(bt_route_sink_t sink)
{
    bt_pcm_enable(&s_pcm, sink == BT_ROUTE_WIRELESS);
    atomic_store_explicit(&s_route, sink, memory_order_relaxed);
}

void bt_output_write(const int16_t pcm[BT_PCM_BLOCK][2])
{
    if (active_format()) (void)bt_pcm_push(&s_pcm, pcm);
}
