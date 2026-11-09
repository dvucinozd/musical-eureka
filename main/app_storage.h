#pragma once

#include "preset_codec.h"

void app_storage_boot(void);
void *app_storage_upload_buffer(size_t size);
void app_storage_free_buffer(void *data);
esp_err_t app_storage_upload(const void *data, size_t size);
esp_err_t app_storage_unload(void);
esp_err_t app_storage_reload(void);
esp_err_t app_storage_save_preset(void);
esp_err_t app_storage_restore_preset(void);

typedef struct {
    uint64_t generation;
    unsigned programs;
    bool stored;
} app_bank_status_t;

app_bank_status_t app_storage_status(void);
bool app_storage_program(unsigned index, unsigned *bank, unsigned *program, char name[21]);
esp_err_t app_storage_select(uint64_t generation, unsigned index);
