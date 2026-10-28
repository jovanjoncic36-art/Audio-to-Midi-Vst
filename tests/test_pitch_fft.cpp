#include "TestFramework.h"
#include "cutbench/Fft.h"
#include "cutbench/PitchMath.h"

#include <complex>
#include <random>
#include <stdexcept>

using namespace cutbench;

TEST (pitch_conversions)
{
    CHECK_NEAR (midiToHz (69), 440.0, 1.0e-9);
    CHECK_NEAR (midiToHz (60), 261.6255653, 1.0e-4);
    CHECK_NEAR (midiToHz (12), 16.3515978, 1.0e-4);   // C0
    CHECK_NEAR (midiToHz (108), 4186.0090448, 1.0e-3); // C8
    CHECK_NEAR (hzToMidi (440.0), 69.0, 1.0e-9);
    CHECK_NEAR (hzToMidi (261.6255653), 60.0, 1.0e-6);
    CHECK (hzToNearestMidi (442.0) == 69);
    CHECK (hzToNearestMidi (452.0) == 69); // +46 cents
    CHECK (hzToNearestMidi (455.0) == 70); // +56 cents
    CHECK_NEAR (centsFromNearestNote (440.0 * std::pow (2.0, 20.0 / 1200.0)), 20.0, 1.0e-6);

    // invalid input must not produce NaN or crash
    CHECK (hzToMidi (0.0) < -100.0);
    CHECK (hzToMidi (-5.0) < -100.0);
    CHECK (hzToMidi (std::nan ("")) < -100.0);

    // round trip over the whole displayed range
    for (int m = kMidiC0; m <= kMidiC8; ++m)
        CHECK (hzToNearestMidi (midiToHz (m)) == m);
}

TEST (pitch_note_names)
{
    CHECK (midiToName (60) == "C4");
    CHECK (midiToName (61) == "C#4");
    CHECK (midiToName (69) == "A4");
    CHECK (midiToName (12) == "C0");
    CHECK (midiToName (108) == "C8");
    CHECK (midiToName (21) == "A0");
    CHECK (midiToName (0) == "C-1");
    CHECK (midiToName (127) == "G9");
    CHECK (isBlackKey (61));
    CHECK (! isBlackKey (60));
    CHECK (! isBlackKey (64));
    CHECK (isBlackKey (70));
}

TEST (fft_matches_naive_dft)
{
    const int n = 64;
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> re (n), im (n, 0.0f), inputRe (n);
    for (int i = 0; i < n; ++i)
        inputRe[(size_t) i] = re[(size_t) i] = dist (rng);

    Fft fft (n);
    fft.forward (re.data(), im.data());

    for (int k = 0; k < n; ++k)
    {
        std::complex<double> sum (0.0, 0.0);
        for (int t = 0; t < n; ++t)
            sum += std::polar ((double) inputRe[(size_t) t], -2.0 * 3.14159265358979323846 * k * t / n);

        CHECK_NEAR (re[(size_t) k], sum.real(), 1.0e-4);
        CHECK_NEAR (im[(size_t) k], sum.imag(), 1.0e-4);
    }
}

TEST (fft_sine_peaks_at_expected_bin)
{
    const int n = 1024, bin = 37;
    std::vector<float> re (n), im (n, 0.0f);
    for (int i = 0; i < n; ++i)
        re[(size_t) i] = (float) std::sin (2.0 * 3.14159265358979323846 * bin * i / n);

    Fft fft (n);
    fft.forward (re.data(), im.data());

    int best = 0;
    float bestMag = 0.0f;
    for (int k = 0; k < n / 2; ++k)
    {
        const float m = std::hypot (re[(size_t) k], im[(size_t) k]);
        if (m > bestMag)
        {
            bestMag = m;
            best = k;
        }
    }

    CHECK (best == bin);
    CHECK_NEAR (bestMag, n / 2.0, 0.05);
}

TEST (fft_rejects_bad_sizes)
{
    bool threw = false;
    try { Fft f (1000); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK (threw);

    threw = false;
    try { Fft f (0); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK (threw);
}
