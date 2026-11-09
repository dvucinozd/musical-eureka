#pragma once
/* Precondition: value and bounds are finite, low < 0 < high.
 * Preserves signed zero; caller retains its existing NaN/Inf fault policy. */
static inline float synth_finite_clamp(float value, float low, float high)
{
    return value > high ? high : value < low ? low : value;
}
