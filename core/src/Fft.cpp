#include "cutbench/Fft.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace cutbench
{
Fft::Fft (int size) : n (size)
{
    if (size < 2 || (size & (size - 1)) != 0)
        throw std::invalid_argument ("Fft size must be a power of two >= 2");

    cosTable.resize ((size_t) n / 2);
    sinTable.resize ((size_t) n / 2);

    const double twoPi = 6.283185307179586476925286766559;
    for (int k = 0; k < n / 2; ++k)
    {
        cosTable[(size_t) k] = (float) std::cos (twoPi * k / n);
        sinTable[(size_t) k] = (float) -std::sin (twoPi * k / n);
    }

    int bits = 0;
    while ((1 << bits) < n)
        ++bits;

    bitReverse.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        bitReverse[(size_t) i] = r;
    }
}

void Fft::forward (float* re, float* im) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const int j = bitReverse[(size_t) i];
        if (j > i)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;

        for (int i = 0; i < n; i += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const float wr = cosTable[(size_t) (k * step)];
                const float wi = sinTable[(size_t) (k * step)];
                const int a = i + k;
                const int b = a + half;
                const float tr = re[b] * wr - im[b] * wi;
                const float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}
} // namespace cutbench
