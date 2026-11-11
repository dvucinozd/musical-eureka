#include "app_web.h"
#include "app_storage.h"
#include "app_wifi.h"
#include "audio_controls.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_NEKTAR_P07_OTA
#include "app_ota.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#ifdef CONFIG_NEKTAR_P06B_BLUETOOTH
#include "bt_output.h"
#endif
extern const char index_html[] asm("_binary_index_html_start");

typedef struct {
    const char *name;
    audio_command_kind_t kind;
    int min, max;
    bool track;
} command_spec_t;

static const command_spec_t commands[] = {
    {"press", AUDIO_CMD_PRESS, 0, 127, false}, {"release", AUDIO_CMD_RELEASE, 0, 127, false},
    {"hold", AUDIO_CMD_HOLD, 0, 0, false}, {"panic", AUDIO_CMD_PANIC, 0, 0, false},
    {"preset", AUDIO_CMD_PRESET, 0, SYNTH_PRESET_COUNT - 1, false},
    {"gain", AUDIO_CMD_GAIN, 0, SYNTH_MAX_GAIN_PERMILLE, false},
    {"wave", AUDIO_CMD_WAVE, 0, SYNTH_WAVE_COUNT - 1, false},
    {"cutoff", AUDIO_CMD_CUTOFF, 20, 18000, false}, {"resonance", AUDIO_CMD_RESONANCE, 5, 120, false},
    {"filter", AUDIO_CMD_FILTER, 0, 2, false},
    {"attack", AUDIO_CMD_ATTACK, 0, 4000, false}, {"decay", AUDIO_CMD_DECAY, 0, 4000, false},
    {"sustain", AUDIO_CMD_SUSTAIN, 0, 100, false}, {"env_release", AUDIO_CMD_ENV_RELEASE, 0, 4000, false},
    {"delay", AUDIO_CMD_DELAY, 0, 1, false}, {"chorus", AUDIO_CMD_CHORUS, 0, 1, false},
    {"lfo", AUDIO_CMD_LFO, 0, 1, false}, {"arp", AUDIO_CMD_ARP, 0, SYNTH_ARP_MODE_COUNT - 1, false},
    {"arp_rate", AUDIO_CMD_ARP_RATE, 0, SYNTH_ARP_RATE_COUNT - 1, false},
    {"arp_octaves", AUDIO_CMD_ARP_OCTAVES, 1, 4, false}, {"arp_latch", AUDIO_CMD_ARP_LATCH, 0, 1, false},
    {"bpm", AUDIO_CMD_BPM, 40, 300, false}, {"bars", AUDIO_CMD_LOOP_BARS, 1, 8, false},
    {"trigger", AUDIO_CMD_LOOP_TRIGGER, 0, 0, true}, {"stop", AUDIO_CMD_LOOP_STOP, 0, 0, true},
    {"clear", AUDIO_CMD_LOOP_CLEAR, 0, 0, true}, {"mute", AUDIO_CMD_LOOP_MUTE, 0, 1, true},
    {"volume", AUDIO_CMD_LOOP_VOLUME, 0, 100, true},
    {"track_preset", AUDIO_CMD_LOOP_PRESET, 0, SYNTH_PRESET_COUNT - 1, true},
};

static esp_err_t respond(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, body);
}

static atomic_bool s_commands_open;

void app_web_open_commands(void)
{
    atomic_store(&s_commands_open, true);
}

static esp_err_t close_with(httpd_req_t *req, const char *status, const char *body)
{
    (void)respond(req, status, body);
    return ESP_FAIL; /* Unread body bytes cannot be used for the next request. */
}

static esp_err_t startup_busy(httpd_req_t *req)
{
    return close_with(req, "503 Service Unavailable", "Startup checks in progress");
}

/* Require the AP host for every request and a same-origin browser mutation. */
static bool authorized(httpd_req_t *req)
{
    char host[32];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        (strcmp(host, NEKTAR_AP_IP) && strcmp(host, NEKTAR_AP_IP ":80"))) return false;
    if (req->method != HTTP_POST || !httpd_req_get_hdr_value_len(req, "Origin")) return true;
    char origin[48];
    return httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK &&
           (!strcmp(origin, "http://" NEKTAR_AP_IP) || !strcmp(origin, "http://" NEKTAR_AP_IP ":80"));
}

static bool number(const char *query, const char *key, int min, int max, int *out)
{
    char text[12], *end;
    if (httpd_query_key_value(query, key, text, sizeof(text)) != ESP_OK ||
        text[0] < '0' || text[0] > '9') return false;
    errno = 0;
    long v = strtol(text, &end, 10);
    if (errno || *end || v < min || v > max) return false;
    *out = v;
    return true;
}

static bool hold_mask(const char *query, uint32_t mask[4])
{
    char text[33];
    if (httpd_query_key_value(query, "m", text, sizeof(text)) != ESP_OK || strlen(text) != 32) return false;
    for (unsigned i = 0; i < 32; ++i) {
        char c = text[i];
        unsigned n;
        if (c >= '0' && c <= '9') n = c - '0';
        else if (c >= 'a' && c <= 'f') n = c - 'a' + 10;
        else return false;
        mask[i / 8] = (mask[i / 8] << 4) | n;
    }
    return true;
}

static esp_err_t control_handler(httpd_req_t *req)
{
    if (!atomic_load(&s_commands_open)) return startup_busy(req);
    if (!authorized(req)) return respond(req, "403 Forbidden", "Invalid host or origin");
    char query[96], op[20];
    if (req->content_len || httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "op", op, sizeof(op)) != ESP_OK)
        return respond(req, "400 Bad Request", "Invalid command");

    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        const command_spec_t *spec = &commands[i];
        if (strcmp(op, spec->name)) continue;
        audio_command_t cmd = {.kind = spec->kind};
        int index = 0;
        if ((spec->track && !number(query, "i", 0, LOOPER_TRACK_COUNT - 1, &index)) ||
            (spec->min != spec->max && !number(query, "v", spec->min, spec->max, &cmd.value)) ||
            (cmd.kind == AUDIO_CMD_HOLD && !hold_mask(query, cmd.mask)))
            return respond(req, "400 Bad Request", "Invalid value");
        if (cmd.kind == AUDIO_CMD_LOOP_BARS && cmd.value != 1 && cmd.value != 2 &&
            cmd.value != 4 && cmd.value != 8)
            return respond(req, "400 Bad Request", "Invalid loop length");
        cmd.index = index;
        if (cmd.kind == AUDIO_CMD_PRESS || cmd.kind == AUDIO_CMD_RELEASE) cmd.index = cmd.value;
        if (!audio_submit_control(cmd)) return respond(req, "503 Service Unavailable", "Audio busy");
        return respond(req, "202 Accepted", "Queued");
    }
    return respond(req, "400 Bad Request", "Unknown command");
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!authorized(req)) return respond(req, "403 Forbidden", "Invalid host");
    audio_control_status_t s;
    if (!audio_get_controls(&s)) return respond(req, "503 Service Unavailable", "Audio unavailable");
    const synth_config_t *c = &s.config;
    char json[1400];
    int n = snprintf(json, sizeof(json),
        "{\"state\":%d,\"preset\":%u,\"gain\":%u,\"wave\":%u,\"cutoff\":%.0f,"
        "\"resonance\":%.0f,\"filter\":%u,\"attack\":%u,\"decay\":%u,\"sustain\":%.0f,"
        "\"env_release\":%u,\"delay\":%u,\"chorus\":%u,\"lfo\":%u,\"arp\":%u,"
        "\"arp_rate\":%u,\"arp_octaves\":%u,\"arp_latch\":%u,\"bpm\":%u,\"bars\":%u,\"tracks\":[",
        audio_get_status().state, (unsigned)s.preset, (unsigned)c->master_gain_permille,
        (unsigned)c->waveform, c->base_cutoff_hz, c->resonance * 10, (unsigned)c->filter_type,
        (unsigned)c->attack_ms, (unsigned)c->decay_ms, c->sustain_ratio * 100,
        (unsigned)c->release_ms, (unsigned)c->delay_enable, (unsigned)c->chorus_enable,
        (unsigned)c->lfo_enable, (unsigned)c->arp_mode, (unsigned)c->arp_rate,
        (unsigned)c->arp_octaves, (unsigned)c->arp_latch, (unsigned)s.looper.bpm, (unsigned)s.looper.bars);
    if (n < 0 || n >= sizeof(json)) return ESP_FAIL;
    size_t used = n;
    for (unsigned i = 0; i < LOOPER_TRACK_COUNT; ++i) {
        const looper_track_status_t *t = &s.looper.tracks[i];
        n = snprintf(json + used, sizeof(json) - used,
            "%s{\"state\":%u,\"events\":%u,\"mute\":%u,\"volume\":%u,\"preset\":%u,\"sf\":%u}",
            i ? "," : "", (unsigned)t->state, (unsigned)t->event_count, (unsigned)t->muted,
            (unsigned)t->volume_pct, (unsigned)t->preset, (unsigned)t->sf);
        if (n < 0 || (size_t)n >= sizeof(json) - used) return ESP_FAIL;
        used += n;
    }
    n = snprintf(json + used, sizeof(json) - used,
        "],\"sound\":{\"sf\":%u,\"generation\":\"%" PRIu64 "\",\"bank\":%u,\"program\":%u,\"missing\":%u},\"ota\":%s}",
        s.sound.engine == SOUND_SF2, s.sound.bank_generation, (unsigned)s.sound.bank,
        (unsigned)s.sound.program, (unsigned)s.sound.sound_missing,
#ifdef CONFIG_NEKTAR_P07_OTA
        app_ota_enabled() ? "true" : "false"
#else
        "false"
#endif
    );
    if (n < 0 || (size_t)n >= sizeof(json) - used) return ESP_FAIL;
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t bank_handler(httpd_req_t *req)
{
    if (!authorized(req)) return respond(req, "403 Forbidden", "Invalid host");
    const app_bank_status_t status = app_storage_status();
    char query[48];
    int offset = 0;
    if (httpd_req_get_url_query_len(req) &&
        (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
         !number(query, "offset", 0, status.programs, &offset)))
        return respond(req, "400 Bad Request", "Invalid catalog offset");
    char json[1536];
    int n = snprintf(json, sizeof(json), "{\"generation\":\"%" PRIu64 "\",\"total\":%u,\"stored\":%u,\"programs\":[",
                     status.generation, status.programs, (unsigned)status.stored);
    if (n < 0 || n >= sizeof(json)) return ESP_FAIL;
    size_t used = n;
    for (unsigned i = offset; i < status.programs && i < (unsigned)offset + 16u; ++i) {
        unsigned bank, program;
        char name[21];
        if (!app_storage_program(i, &bank, &program, name)) return ESP_FAIL;
        for (unsigned j = 0; name[j]; ++j) {
            const unsigned char c = name[j];
            if (c == '"') name[j] = '\'';
            else if (c == '\\') name[j] = '/';
            else if (c < 32 || c > 126) name[j] = '?';
        }
        n = snprintf(json + used, sizeof(json) - used,
            "%s{\"index\":%u,\"bank\":%u,\"program\":%u,\"name\":\"%s\"}",
            i == (unsigned)offset ? "" : ",", i, bank, program, name);
        if (n < 0 || (size_t)n >= sizeof(json) - used) return ESP_FAIL;
        used += n;
    }
    if (sizeof(json) - used < 3) return ESP_FAIL;
    memcpy(json + used, "]}", 3);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t storage_result(httpd_req_t *req, esp_err_t err)
{
    switch (err) {
    case ESP_OK: return respond(req, "200 OK", "Done");
    case ESP_ERR_NO_MEM: return respond(req, "409 Conflict", "SoundFont memory budget exceeded. Unload the active bank if present.");
    case ESP_ERR_INVALID_STATE: return respond(req, "409 Conflict", "Audio busy or bank changed. Stop playback and release notes. Bank changes also require clearing or changing SoundFont tracks.");
    case ESP_ERR_NOT_FOUND:
    case ESP_ERR_NVS_NOT_FOUND: return respond(req, "404 Not Found", "No usable bank or saved preset");
    case ESP_ERR_NOT_SUPPORTED:
    case ESP_ERR_INVALID_ARG: return respond(req, "422 Unprocessable Content", "Invalid or unsupported SoundFont or preset");
    case ESP_ERR_TIMEOUT: return respond(req, "503 Service Unavailable", "Audio acknowledgement timed out. Refresh status before retrying.");
    default: return respond(req, "500 Internal Server Error", "Storage or audio resume failed. Refresh status before retrying.");
    }
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    const size_t size = req->content_len;
    if (size < 12 || size > BANK_PAYLOAD_MAX)
        return close_with(req, "413 Content Too Large", "SF2 size must be between 12 bytes and 3 MiB");
    char type[32];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcmp(type, "application/octet-stream"))
        return close_with(req, "415 Unsupported Media Type", "Send the raw SF2 as application/octet-stream");
    uint8_t *data = app_storage_upload_buffer(size);
    if (!data) return close_with(req, "409 Conflict", "Upload buffer unavailable. Check flash layout or unload the active bank.");
    const int64_t deadline = esp_timer_get_time() + INT64_C(45000000);
    size_t used = 0;
    while (used < size && esp_timer_get_time() < deadline) {
        size_t chunk = size - used < 4096 ? size - used : 4096;
        int received = httpd_req_recv(req, (char *)data + used, chunk);
        if (received <= 0) break;
        used += received;
    }
    esp_err_t err = used == size ? app_storage_upload(data, size) : ESP_ERR_INVALID_SIZE;
    app_storage_free_buffer(data);
    if (used != size) return close_with(req, "408 Request Timeout", "Incomplete upload");
    return storage_result(req, err);
}

static esp_err_t storage_handler(httpd_req_t *req)
{
    if (!atomic_load(&s_commands_open)) return startup_busy(req);
    if (!authorized(req)) return close_with(req, "403 Forbidden", "Invalid host or origin");
    char query[96], op[20];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "op", op, sizeof(op)) != ESP_OK)
        return close_with(req, "400 Bad Request", "Invalid storage command");
    if (!strcmp(op, "upload")) return upload_handler(req);
    if (req->content_len) return close_with(req, "400 Bad Request", "Unexpected body");
    if (!strcmp(op, "unload")) return storage_result(req, app_storage_unload());
    if (!strcmp(op, "reload")) return storage_result(req, app_storage_reload());
    if (!strcmp(op, "save")) return storage_result(req, app_storage_save_preset());
    if (!strcmp(op, "restore")) return storage_result(req, app_storage_restore_preset());
    if (!strcmp(op, "select")) {
        char token[21], *end;
        int index;
        if (httpd_query_key_value(query, "g", token, sizeof(token)) != ESP_OK ||
            !token[0] || strspn(token, "0123456789") != strlen(token) ||
            !number(query, "i", 0, 4095, &index))
            return respond(req, "400 Bad Request", "Invalid program identity");
        errno = 0;
        uint64_t generation = strtoull(token, &end, 10);
        if (errno || *end) return respond(req, "400 Bad Request", "Invalid generation");
        esp_err_t err = app_storage_select(generation, index);
        return err == ESP_OK ? respond(req, "202 Accepted", "Queued") : storage_result(req, err);
    }
    return respond(req, "400 Bad Request", "Unknown storage command");
}

#ifdef CONFIG_NEKTAR_P07_OTA
static esp_err_t ota_handler(httpd_req_t *req)
{
    if (!atomic_load(&s_commands_open)) return startup_busy(req);
    if (!authorized(req)) return close_with(req, "403 Forbidden", "Invalid host or origin");
    if (!app_ota_enabled()) return close_with(req, "403 Forbidden", "Configure a private Wi-Fi password");
    const size_t size = req->content_len;
    if (size < OTA_FIRST_SEGMENT || size > OTA_IMAGE_LIMIT)
        return close_with(req, "413 Content Too Large", "Firmware must fit the 3 MiB OTA slot");
    char type[32], hex[2 * OTA_DIGEST_BYTES + 1];
    uint8_t expected[OTA_DIGEST_BYTES];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcmp(type, "application/octet-stream") ||
        httpd_req_get_hdr_value_str(req, "X-Firmware-SHA256", hex, sizeof(hex)) != ESP_OK ||
        !ota_digest_parse(hex, expected))
        return close_with(req, "400 Bad Request", "Raw firmware and a SHA-256 header are required");
    uint8_t *data = app_ota_buffer(size);
    if (!data) return close_with(req, "409 Conflict", "Firmware does not fit the PSRAM staging budget");
    const int64_t deadline = esp_timer_get_time() + INT64_C(45000000);
    size_t used = 0;
    while (used < size && esp_timer_get_time() < deadline) {
        const size_t chunk = size - used < 4096 ? size - used : 4096;
        const int received = httpd_req_recv(req, (char *)data + used, chunk);
        if (received <= 0) break;
        used += received;
    }
    const esp_err_t err = used == size ? app_ota_install(data, size, expected) : ESP_ERR_INVALID_SIZE;
    heap_caps_free(data);
    if (used != size) return close_with(req, "408 Request Timeout", "Incomplete firmware upload");
    if (err == ESP_ERR_INVALID_VERSION || err == ESP_ERR_INVALID_CRC)
        return respond(req, "422 Unprocessable Content", "Firmware target, profile, format or SHA-256 is invalid");
    if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_TIMEOUT)
        return respond(req, "409 Conflict", "Stop notes and loop playback, or resolve the boot slot state");
    if (err != ESP_OK) return respond(req, "500 Internal Server Error", "Firmware update failed");

    (void)respond(req, "200 OK", "Firmware staged. Restarting.");
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}
#endif

static esp_err_t index_handler(httpd_req_t *req)
{
    if (!authorized(req)) return respond(req, "403 Forbidden", "Invalid host");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, index_html);
}

static esp_err_t output_status_handler(httpd_req_t *req)
{
    if (!authorized(req)) return respond(req, "403 Forbidden", "Invalid host");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
#ifdef CONFIG_NEKTAR_P06B_BLUETOOTH
    bt_output_status_t s = bt_output_status();
    char json[1024];
    int n = snprintf(json, sizeof(json),
        "{\"available\":true,\"state\":%u,\"error\":%u,\"route\":%u,\"requested\":%u,\"ready\":%u,"
        "\"rate\":%u,\"channels\":%u,\"dropped\":%" PRIu32 ",\"silent\":%" PRIu32 ",\"devices\":[",
        (unsigned)s.state, (unsigned)s.error, (unsigned)s.route, (unsigned)s.requested,
        (unsigned)s.ready, s.rate, s.channels, s.dropped_blocks, s.silent_frames);
    if (n < 0 || n >= sizeof(json)) return ESP_FAIL;
    size_t used = n;
    for (unsigned i = 0; i < s.count; ++i) {
        const bt_output_device_t *device = &s.devices[i];
        const uint8_t *a = device->address;
        n = snprintf(json + used, sizeof(json) - used,
            "%s{\"address\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"name\":\"%s\"}",
            i ? "," : "", a[0], a[1], a[2], a[3], a[4], a[5], device->name);
        if (n < 0 || (size_t)n >= sizeof(json) - used) return ESP_FAIL;
        used += n;
    }
    if (sizeof(json) - used < 3) return ESP_FAIL;
    memcpy(json + used, "]}", 3);
    return httpd_resp_sendstr(req, json);
#else
    return httpd_resp_sendstr(req, "{\"available\":false}");
#endif
}

#ifdef CONFIG_NEKTAR_P06B_BLUETOOTH
static bool output_address(const char *query, uint8_t address[BT_ADDRESS_BYTES])
{
    /* Fixed hex string avoids URL-escaped separators and ambiguous formats. */
    char text[BT_ADDRESS_BYTES * 2 + 1];
    if (httpd_query_key_value(query, "address", text, sizeof(text)) != ESP_OK ||
        strlen(text) != BT_ADDRESS_BYTES * 2 ||
        strspn(text, "0123456789abcdefABCDEF") != BT_ADDRESS_BYTES * 2) return false;
    for (unsigned i = 0; i < BT_ADDRESS_BYTES; ++i) {
        const char byte[] = {text[2 * i], text[2 * i + 1], 0};
        address[i] = (uint8_t)strtoul(byte, NULL, 16);
    }
    return true;
}

static esp_err_t output_command_handler(httpd_req_t *req)
{
    if (!atomic_load(&s_commands_open)) return startup_busy(req);
    if (!authorized(req)) return close_with(req, "403 Forbidden", "Invalid host or origin");
    char query[64], op[16];
    if (req->content_len || httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "op", op, sizeof(op)) != ESP_OK)
        return close_with(req, "400 Bad Request", "Invalid output command");

    esp_err_t err;
    if (!strcmp(op, "scan")) err = bt_output_scan();
    else if (!strcmp(op, "disconnect")) err = bt_output_disconnect();
    else if (!strcmp(op, "local")) err = bt_output_select(false);
    else if (!strcmp(op, "bluetooth")) err = bt_output_select(true);
    else if (!strcmp(op, "connect")) {
        uint8_t address[BT_ADDRESS_BYTES];
        if (!output_address(query, address)) return respond(req, "400 Bad Request", "Invalid address");
        err = bt_output_connect(address);
    } else return respond(req, "400 Bad Request", "Unknown output command");
    if (err == ESP_OK) return respond(req, "202 Accepted", "Accepted");
    if (err == ESP_ERR_NOT_FOUND) return respond(req, "404 Not Found", "Speaker is not in scan results");
    if (err == ESP_ERR_TIMEOUT) return respond(req, "503 Service Unavailable", "Bluetooth command queue is full");
    return respond(req, "409 Conflict", "Bluetooth is busy or the speaker is not ready");
}
#endif

esp_err_t app_web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/control", .method = HTTP_POST, .handler = control_handler},
        {.uri = "/api/bank", .method = HTTP_GET, .handler = bank_handler},
        {.uri = "/api/storage", .method = HTTP_POST, .handler = storage_handler},
        {.uri = "/api/output", .method = HTTP_GET, .handler = output_status_handler},
#ifdef CONFIG_NEKTAR_P06B_BLUETOOTH
        {.uri = "/api/output", .method = HTTP_POST, .handler = output_command_handler},
#endif
#ifdef CONFIG_NEKTAR_P07_OTA
        {.uri = "/api/firmware", .method = HTTP_POST, .handler = ota_handler},
#endif
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            httpd_stop(server);
            return err;
        }
    }
    return ESP_OK;
}
