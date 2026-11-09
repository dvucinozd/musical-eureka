#include "ota_transaction.h"

bool ota_transaction_install(const ota_transaction_t *tx, const uint8_t *image, size_t size)
{
    if (!size || !tx->begin(tx->context, size)) return false;
    for (size_t offset = 0; offset < size;) {
        const size_t chunk = size - offset < 1024 ? size - offset : 1024;
        if (!tx->write(tx->context, image + offset, chunk)) {
            tx->abort(tx->context);
            return false;
        }
        offset += chunk;
    }
    return tx->finish(tx->context) && tx->select(tx->context);
}
