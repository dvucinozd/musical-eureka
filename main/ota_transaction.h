#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *context;
    bool (*begin)(void *, size_t);
    bool (*write)(void *, const uint8_t *, size_t);
    void (*abort)(void *);
    bool (*finish)(void *); /* Consumes the handle on success and failure. */
    bool (*select)(void *);
} ota_transaction_t;

/* Caller holds the audio maintenance lease and has validated the whole image. */
bool ota_transaction_install(const ota_transaction_t *tx, const uint8_t *image, size_t size);
