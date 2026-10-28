#pragma once
// Deterministic synthetic test signals.

#include <cmath>
#include <cstdint>
#include <vector>

namespace sig
{
constexpr double kPi = 3.14159265358979323846;

inline std::vector<float> silence (double sr, double seconds)
{
    return std::vector<float> ((size_t) std::llround (sr * seconds), 0.0f);
}

// Adds a harmonic tone (partial h has amplitude amp / h^rolloffPower) between t0 and t1 with 10 ms fades.
inline void addTone (std::vector<float>& buf, double sr, double f0, double t0, double t1, double amp,
                     int numPartials = 1, double rolloffPower = 1.0)
{
    const size_t i0 = (size_t) std::llround (t0 * sr);
    const size_t i1 = std::min (buf.size(), (size_t) std::llround (t1 * sr));
    const double fade = 0.010 * sr;

    for (size_t i = i0; i < i1; ++i)
    {
        const double t = double (i) / sr;
        double v = 0.0;
        for (int h = 1; h <= numPartials; ++h)
        {
            const double f = f0 * h;
            if (f >= sr * 0.5)
                break;
            v += amp / std::pow ((double) h, rolloffPower) * std::sin (2.0 * kPi * f * t);
        }

        double env = 1.0;
        env = std::min (env, double (i - i0) / fade);
        env = std::min (env, double (i1 - 1 - i) / fade);
        buf[i] += (float) (v * std::max (0.0, std::min (1.0, env)));
    }
}

// Deterministic white noise (xorshift), uniform in [-amp, amp].
inline std::vector<float> whiteNoise (double sr, double seconds, double amp, uint32_t seed = 12345u)
{
    std::vector<float> out ((size_t) std::llround (sr * seconds));
    uint32_t x = seed;
    for (auto& s : out)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s = (float) (amp * ((double) x / 2147483648.0 - 1.0));
    }
    return out;
}
} // namespace sig
