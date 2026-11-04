// Standalone DSP tests for HitCore.h — no JUCE needed.
//   g++ -std=c++17 -O2 -I Source tests/core_test.cpp -o core_test && ./core_test
#include "HitCore.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (! (cond)) {                                   \
            ++failures;                                   \
            std::printf ("  FAIL: ");                     \
            std::printf (__VA_ARGS__);                    \
            std::printf ("\n");                           \
        }                                                 \
    } while (0)

constexpr double kPi = 3.14159265358979323846;

static std::shared_ptr<hit::SampleData> makeSine (double sr, double seconds, double freq, double amp)
{
    auto s = std::make_shared<hit::SampleData>();
    s->sampleRate = sr;
    const int n = (int) (sr * seconds);
    s->left.resize ((size_t) n);
    for (int i = 0; i < n; ++i) s->left[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * freq * i / sr));
    s->right = s->left;
    return s;
}

static std::vector<float> renderHit (const hit::SampleData& s, double hostSr, double x, double y,
                                     double levelDb, double seconds, float vel = 1.0f)
{
    hit::Engine e;
    e.prepare (hostSr);
    e.setSample (&s);
    e.setParams (x, y, levelDb);
    e.snapParams();
    e.noteOn (vel);
    const int n = (int) (hostSr * seconds);
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    e.render (L.data(), R.data(), n);
    return L;
}

static double rmsDb (const std::vector<float>& v, int start, int len)
{
    double acc = 0.0;
    for (int i = start; i < start + len && i < (int) v.size(); ++i) acc += (double) v[(size_t) i] * v[(size_t) i];
    return 10.0 * std::log10 (acc / len + 1.0e-30);
}

static double goertzelDb (const std::vector<float>& v, int start, int len, double freq, double sr)
{
    const double w = 2.0 * kPi * freq / sr, c = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < len; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (len - 1));
        s0 = v[(size_t) (start + i)] * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 10.0 * std::log10 (p + 1.0e-30);
}

int main()
{
    const double sr = 48000.0;
    auto sine = makeSine (sr, 10.0, 1000.0, 0.5);

    std::printf ("1) decay: level at t = T60 should be ~ -60 dB below the start\n");
    for (double x : { 0.0, 0.25, 0.5, 0.75, 0.9 })
    {
        const double t60 = 1.0 / (hit::decayRatePerSec (x) / hit::kLn1000);
        const int tMs = (int) std::lround (t60 * 1000.0);            // whole ms => whole 1 kHz cycles
        auto out = renderHit (*sine, sr, x, 0.0, 0.0, 3.0);
        const double d = rmsDb (out, (int) (0.001 * sr), 48) - rmsDb (out, (int) (tMs * 0.001 * sr), 48);
        std::printf ("   x=%.2f  T60=%7.1f ms  measured drop=%5.1f dB\n", x, t60 * 1000.0, d);
        CHECK (std::abs (d - 60.0) < 4.0, "decay at x=%.2f off: %.1f dB", x, d);
    }

    std::printf ("2) open end (x=1): no decay over 2 s\n");
    {
        auto out = renderHit (*sine, sr, 1.0, 0.0, 0.0, 3.0);
        const double d = rmsDb (out, 48, 48) - rmsDb (out, 2 * 48000, 48);
        std::printf ("   drop over 2 s = %.2f dB\n", d);
        CHECK (std::abs (d) < 0.5, "x=1 should not decay");
    }

    std::printf ("3) decay is monotonic in X\n");
    {
        double prev = 0;
        for (double x = 0.0; x <= 1.0001; x += 0.05)
        {
            const double t = std::min (hit::effectiveT60 (x), 1.0e6);
            CHECK (t >= prev, "T60 not monotonic at x=%.2f", x);
            prev = t;
        }
    }

    std::printf ("4) output never exceeds full scale for a full-scale input, any Y\n");
    {
        auto loud = makeSine (sr, 1.0, 220.0, 1.0);
        for (double y : { 0.0, 0.3, 0.6, 1.0 })
        {
            auto out = renderHit (*loud, sr, 1.0, y, 0.0, 1.0);
            float pk = 0.0f;
            for (float v : out) { CHECK (std::isfinite (v), "non-finite output"); pk = std::max (pk, std::abs (v)); }
            std::printf ("   y=%.1f  peak=%.4f\n", y, pk);
            CHECK (pk <= 1.001f, "peak %.4f > 1 at y=%.1f", pk, y);
        }
    }

    std::printf ("5) Y=0 is near-transparent for a quiet signal (level within 0.2 dB)\n");
    {
        auto quiet = makeSine (sr, 2.0, 1000.0, 0.1);
        auto out = renderHit (*quiet, sr, 1.0, 0.0, 0.0, 1.5);
        const double inDb = 10.0 * std::log10 (0.1 * 0.1 / 2.0);
        const double d = rmsDb (out, 4800, 4800) - inDb;
        std::printf ("   level error = %.3f dB\n", d);
        CHECK (std::abs (d) < 0.2, "Y=0 colours the signal by %.3f dB", d);
    }

    std::printf ("6) saturation adds odd harmonics as Y rises\n");
    {
        auto tone = makeSine (sr, 2.0, 1000.0, 0.8);
        double h3[2];
        int k = 0;
        for (double y : { 0.0, 0.8 })
        {
            auto out = renderHit (*tone, sr, 1.0, y, 0.0, 1.5);
            h3[k] = goertzelDb (out, 9600, 24000, 3000.0, sr) - goertzelDb (out, 9600, 24000, 1000.0, sr);
            std::printf ("   y=%.1f  3rd harmonic = %6.1f dB re fundamental\n", y, h3[k]);
            ++k;
        }
        CHECK (h3[0] < -55.0, "too much distortion at Y=0");
        CHECK (h3[1] > -25.0, "not enough distortion at Y=0.8");
    }

    std::printf ("7) sample-rate conversion: 44.1k file on 48k host lasts ~1 s\n");
    {
        auto s = makeSine (44100.0, 1.0, 500.0, 0.5);
        auto out = renderHit (*s, 48000.0, 1.0, 0.0, 0.0, 1.5);
        int last = 0;
        for (int i = 0; i < (int) out.size(); ++i) if (std::abs (out[(size_t) i]) > 1.0e-3f) last = i;
        std::printf ("   last audible frame = %d (expect ~48000)\n", last);
        CHECK (std::abs (last - 48000) < 200, "SRC length off");
    }

    std::printf ("8) polyphony stress: 40 rapid hits, finite output, voice cap respected\n");
    {
        hit::Engine e;
        e.prepare (sr);
        e.setSample (sine.get());
        e.setParams (0.7, 0.5, 0.0);
        e.snapParams();
        std::vector<float> L (4800), R (4800);
        int maxActive = 0;
        for (int h = 0; h < 40; ++h)
        {
            e.noteOn (0.9f);
            std::fill (L.begin(), L.end(), 0.0f);
            std::fill (R.begin(), R.end(), 0.0f);
            e.render (L.data(), R.data(), 480); // 10 ms between hits
            for (int i = 0; i < 480; ++i) CHECK (std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]), "NaN");
            maxActive = std::max (maxActive, e.activeVoices());
        }
        std::printf ("   max simultaneously active = %d\n", maxActive);
        CHECK (maxActive <= hit::Engine::kVoices, "voice cap broken");
    }

    std::printf ("9) velocity scales level\n");
    {
        auto a = renderHit (*sine, sr, 1.0, 0.0, 0.0, 0.2, 1.0f);
        auto b = renderHit (*sine, sr, 1.0, 0.0, 0.0, 0.2, 0.5f);
        const double d = rmsDb (a, 4800, 4800) - rmsDb (b, 4800, 4800);
        CHECK (std::abs (d - 6.02) < 0.3, "velocity 0.5 should be -6 dB, got %.2f", d);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
