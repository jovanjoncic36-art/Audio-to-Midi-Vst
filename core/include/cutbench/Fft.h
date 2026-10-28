#pragma once
// Small, dependency-free radix-2 complex FFT (in place, forward only).
// Used by the analysis core so that it can be built and unit-tested without JUCE.

#include <vector>

namespace cutbench
{
class Fft
{
public:
    // size must be a power of two >= 2, otherwise std::invalid_argument is thrown.
    explicit Fft (int size);

    int size() const noexcept { return n; }

    // In-place forward transform of n complex samples stored as separate re/im arrays.
    void forward (float* re, float* im) const noexcept;

private:
    int n = 0;
    std::vector<float> cosTable, sinTable;
    std::vector<int> bitReverse;
};
} // namespace cutbench
