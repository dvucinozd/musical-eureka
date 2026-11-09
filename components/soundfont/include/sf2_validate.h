#ifndef NEKTAR_SF2_VALIDATE_H
#define NEKTAR_SF2_VALIDATE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef enum { SF2_OK, SF2_INVALID, SF2_UNSUPPORTED, SF2_LIMIT, SF2_NEEDS_APPROXIMATE } sf2_result_t;
/* warnings bits: 1 sm24 ignored, 2 SF sends, 4 modulators (2/4 need
   allow_approximate), 8 non-zero terminal modulator record (ignored). */
typedef struct {
    uint32_t presets, regions, samples, sample_bytes, decoded_bytes;
    uint32_t metadata_peak_bytes, warnings, max_layers;
    size_t error_offset;
    const char *reason;
} sf2_info_t;
/* No allocation, endian casts, file I/O, or engine calls. Immutable input must
   remain leased by its owner until validated data is loaded. */
sf2_result_t sf2_validate(const void *data, size_t size, bool allow_approximate, sf2_info_t *info);
#endif
