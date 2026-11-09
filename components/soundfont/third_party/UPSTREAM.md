# TinySoundFont pin and NEKTAR SF02 patch

Upstream: https://github.com/schellingb/TinySoundFont

Commit: `853a0a171759f1ddba0de1442133a75912bbeffa`

Unmodified `tsf.h` SHA-256:
`70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197`.
The adjacent MIT `LICENSE` applies to both headers. The original header is
retained byte-for-byte; do not edit it. `tools/soundfont/patch_tsf.py` verifies
its hash and generates `tsf_nektar.h`, which is the only header compiled by the
private engine implementation.

Controlled changes:

1. Every allocation/reallocation call explicitly identifies samples, metadata,
   instance, voices or channels. Placement never depends on allocation size.
   The wrapper records the originating allocator and tag in an aligned header.
   Failed realloc leaves the original object alive; successful realloc copies
   and frees through the same originating callback.
2. RIFF child sizes account for odd padding using subtraction before addition.
   Each consumed leaf chunk skips its pad; typed LISTs must have even size.
   Header reads require the full field. The private loader is invoked only
   after full immutable-memory preflight; raw stream APIs are not exposed.
3. Memory-stream callbacks use their actual declared `void *` signatures,
   avoiding incompatible function-pointer calls diagnosed by Clang UBSan.
4. Negative coarse offsets use multiplication, avoiding signed left-shift UB.
5. Explicit loop state represents a one-sample loop (including loop at zero).
   Bounded `fmod` wrapping handles a pitch step crossing many loop periods.
   Sustain release clears the loop state explicitly.
6. Exclusive groups release previous note groups; they cannot release an
   earlier layer of the same newly admitted note.
7. MIDI velocity is rounded back to its exact integer value after normalized
   float input; velocity 127 remains exactly 1.0 for gain calculation.
8. Region allocation is capped at 256 per preset and the preflight's total
   flattened region budget. The upstream counting pass ignores some global
   range restrictions; conservative overcounts are rejected before allocation
   rather than exceeding the declared memory peak.
9. Preset allocation failure nulls the freed preset array. Failure injection
   covers every prepare/copy/preallocation allocation and cleanup path.

The wrapper uses `TSF_RENDER_EFFECTSAMPLEBLOCK=1`. This makes tested envelopes
and modulation independent of caller segment lengths 1/64/128, but increases
dynamic filter/LFO/envelope work. Host timing is not ESP32-P4 timing evidence.
P4 32-voice load qualification must measure this exact setting before adoption.

The caller's typed allocator must return storage aligned to at least
`_Alignof(max_align_t)` for every allocation kind. The wrapper prefixes an
`allocation_header` union with this alignment and the upstream renderer stores
`double` values. ESP-IDF's default `heap_caps_malloc` alignment is not this
contract; capability adapters use `heap_caps_aligned_alloc` with the explicit
alignment. The P4 toolchain reports max_align_t alignment16. Adapter accounting
includes its own header and alignment padding; this requirement does not change
the frozen SF02 compiled source.

SF02 has host parser/engine tests and a separately built isolated P4 benchmark.
A disabled SF03 worker adapter is prepared separately. There is no normal
synth/audio/HTTP routing integration, listening qualification, or device
32-voice timing PASS implied by this document.

2026-10-29: generated region/envelope field offsets now use standard offsetof divided by the field unit size. This replaces upstream null-pointer subtraction diagnosed by strict Clang. The original upstream header remains unchanged; V2 freeze is historical and new long fuzz qualification is required.
