// Standalone DSP tests for HitCore.h — no JUCE needed.
//   c++ -std=c++17 -O2 -I Source tests/core_test.cpp -o core_test && ./core_test
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

using hit::kPi;

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

static hit::Params P (double x, double y, double sat = 0.0)
{
    hit::Params p;
    p.x = x; p.y = y; p.sat = sat;
    return p;
}

static std::vector<float> renderHit (const hit::SampleData& s, double hostSr, const hit::Params& p, double seconds, float vel = 1.0f)
{
    hit::Engine e;
    e.prepare (hostSr);
    e.setSample (&s);
    e.setParams (p);
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
        s2 = s1; s1 = s0;
    }
    return 10.0 * std::log10 (s1 * s1 + s2 * s2 - c * s1 * s2 + 1.0e-30);
}

static int lastAudible (const std::vector<float>& v)
{
    int last = 0;
    for (int i = 0; i < (int) v.size(); ++i) if (std::abs (v[(size_t) i]) > 1.0e-3f) last = i;
    return last;
}

int main()
{
    const double sr = 48000.0;
    auto sine = makeSine (sr, 10.0, 1000.0, 0.5);

    std::printf ("1) decay: level at T60 is ~ -60 dB\n");
    for (double x : { 0.0, 0.25, 0.5, 0.75, 0.9 })
    {
        const double t60 = hit::effectiveT60 (x);
        const int tMs = (int) std::lround (t60 * 1000.0);
        auto out = renderHit (*sine, sr, P (x, 0.5), 3.0);
        const double d = rmsDb (out, 48, 48) - rmsDb (out, (int) (tMs * 48), 48);
        std::printf ("   x=%.2f  T60=%7.1f ms  drop=%5.1f dB\n", x, t60 * 1000.0, d);
        CHECK (std::abs (d - 60.0) < 4.0, "decay at x=%.2f off: %.1f dB", x, d);
    }

    std::printf ("2) open end (x=1): no decay over 2 s\n");
    {
        auto out = renderHit (*sine, sr, P (1.0, 0.5), 3.0);
        CHECK (std::abs (rmsDb (out, 48, 48) - rmsDb (out, 96000, 48)) < 0.5, "x=1 should not decay");
    }

    std::printf ("3) X sensitivity squeezes the decay span around the middle\n");
    {
        CHECK (std::abs (hit::decayPosition (0.0, 1.0) - 0.0) < 1e-9, "full span at sens 1");
        CHECK (std::abs (hit::decayPosition (0.0, 0.2) - 0.4) < 1e-9, "x=0 at 20%% -> 0.4");
        CHECK (std::abs (hit::decayPosition (0.5, 0.2) - 0.5) < 1e-9, "centre unchanged");
        hit::Params p = P (0.0, 0.5); p.xSens = 0.2;
        hit::Engine e; e.prepare (sr); e.setSample (sine.get()); e.setParams (p); e.noteOn (1.0f);
        std::printf ("   x=0 @20%%: T60 = %.0f ms (full span would be 20 ms)\n", e.lastHit().t60 * 1000.0);
        CHECK (std::abs (e.lastHit().t60 - hit::effectiveT60 (0.4)) < 1e-9, "engine uses squeezed decay");
    }

    std::printf ("4) pitch: +12 st halves the length, -12 doubles it; Y sens scales the range\n");
    {
        auto s = makeSine (sr, 0.5, 440.0, 0.5);
        const int base = lastAudible (renderHit (*s, sr, P (1.0, 0.5), 1.5));
        const int up   = lastAudible (renderHit (*s, sr, P (1.0, 1.0), 1.5));   // +12 at default sens
        const int down = lastAudible (renderHit (*s, sr, P (1.0, 0.0), 1.5));   // -12
        std::printf ("   frames: 0 st=%d  +12=%d  -12=%d\n", base, up, down);
        CHECK (std::abs (up * 2 - base) < 200, "+12 should halve");
        CHECK (std::abs (down - base * 2) < 400, "-12 should double");

        auto out = renderHit (*s, sr, P (1.0, 1.0), 0.2);
        const double f880 = goertzelDb (out, 480, 4800, 880.0, sr), f440 = goertzelDb (out, 480, 4800, 440.0, sr);
        CHECK (f880 - f440 > 30.0, "+12 st should move 440 Hz to 880 Hz");

        hit::Params p = P (1.0, 1.0); p.ySens = 2.0;
        CHECK (std::abs (hit::pitchSemitones (1.0, 2.0) - 2.0) < 1e-9, "edge = +ySens");
        const int two = lastAudible (renderHit (*s, sr, p, 1.5));
        const double expect = base / std::pow (2.0, 2.0 / 12.0);
        CHECK (std::abs (two - expect) < 200, "ySens 2: top edge = +2 st (got %d, expect %.0f)", two, expect);
    }

    std::printf ("5) output never exceeds full scale, any drive\n");
    {
        auto loud = makeSine (sr, 1.0, 220.0, 1.0);
        for (double y : { 0.0, 0.3, 0.6, 1.0 })
        {
            auto out = renderHit (*loud, sr, P (1.0, 0.5, y), 1.0);
            float pk = 0.0f;
            for (float v : out) { CHECK (std::isfinite (v), "non-finite"); pk = std::max (pk, std::abs (v)); }
            CHECK (pk <= 1.001f, "peak %.4f > 1 at drive %.1f", pk, y);
        }
    }

    std::printf ("6) drive 0 is transparent; drive adds harmonics\n");
    {
        auto quiet = makeSine (sr, 2.0, 1000.0, 0.1);
        const double d = rmsDb (renderHit (*quiet, sr, P (1.0, 0.5, 0.0), 1.5), 4800, 4800) - 10.0 * std::log10 (0.005);
        CHECK (std::abs (d) < 0.2, "drive 0 colours by %.3f dB", d);
        auto tone = makeSine (sr, 2.0, 1000.0, 0.8);
        auto a = renderHit (*tone, sr, P (1.0, 0.5, 0.0), 1.5), b = renderHit (*tone, sr, P (1.0, 0.5, 0.8), 1.5);
        const double h0 = goertzelDb (a, 9600, 24000, 3000, sr) - goertzelDb (a, 9600, 24000, 1000, sr);
        const double h1 = goertzelDb (b, 9600, 24000, 3000, sr) - goertzelDb (b, 9600, 24000, 1000, sr);
        std::printf ("   3rd harmonic: drive 0 = %.1f dB, drive 0.8 = %.1f dB\n", h0, h1);
        CHECK (h0 < -55.0 && h1 > -25.0, "harmonics");
    }

    std::printf ("7) motion off or depth 0: every hit identical\n");
    {
        hit::Params p = P (0.4, 0.6); p.motion = true; p.depth = 0.0;
        hit::Engine e; e.prepare (sr); e.setSample (sine.get()); e.setParams (p);
        std::vector<float> L (4800), R (4800);
        e.noteOn (1.0f); const auto a = e.lastHit();
        e.render (L.data(), R.data(), 4800);
        e.noteOn (1.0f); const auto b = e.lastHit();
        CHECK (a.padX == b.padX && a.padY == b.padY, "depth 0 should not move");
    }

    std::printf ("8) motion, time clock: spacing decides how different two hits are\n");
    {
        hit::Params p = P (0.5, 0.5); p.motion = true; p.depth = 0.2; p.pattern = hit::kCircle; p.cycleBeats = 4.0;
        auto distAfter = [&] (double seconds)
        {
            hit::Engine e; e.prepare (sr); e.setSample (sine.get()); e.setParams (p);
            e.setTransport (false, -1.0, 120.0);
            std::vector<float> L ((size_t) (sr * seconds) + 1), R (L.size());
            e.noteOn (1.0f); const auto a = e.lastHit();
            e.render (L.data(), R.data(), (int) (sr * seconds));
            e.noteOn (1.0f); const auto b = e.lastHit();
            return std::hypot (a.padX - b.padX, a.padY - b.padY);
        };
        const double close = distAfter (0.125), half = distAfter (1.0), full = distAfter (2.0); // 1 bar @120 = 2 s
        std::printf ("   1/16 apart: %.3f   half cycle: %.3f   full cycle: %.3f   (depth 0.2)\n", close, half, full);
        CHECK (close < 0.1, "close hits should be similar");
        CHECK (std::abs (half - 0.4) < 0.01, "half a cycle apart = 2x depth");
        CHECK (full < 0.01, "a full cycle apart = same spot");
    }

    std::printf ("9) motion, step clock: each hit advances 1/8 cycle regardless of timing\n");
    {
        hit::Params p = P (0.5, 0.5); p.motion = true; p.depth = 0.2; p.stepClock = true; p.pattern = hit::kCorners;
        hit::Engine e; e.prepare (sr); e.setSample (sine.get()); e.setParams (p);
        std::vector<hit::HitInfo> got;
        for (int k = 0; k < 9; ++k) { e.noteOn (1.0f); got.push_back (e.lastHit()); }
        CHECK (std::abs (got[0].padX - got[1].padX) < 1e-9 && std::abs (got[1].padX - got[2].padX) > 0.1, "corners: 2 hits per corner at 1/8 steps");
        CHECK (std::abs (got[0].padX - got[8].padX) < 1e-9 && std::abs (got[0].padY - got[8].padY) < 1e-9, "8 steps = one cycle");
    }

    std::printf ("10) transport lock: same song position -> same values every pass\n");
    {
        hit::Params p = P (0.5, 0.5); p.motion = true; p.depth = 0.25; p.pattern = hit::kFigure8; p.cycleBeats = 4.0;
        hit::Engine e; e.prepare (sr); e.setSample (sine.get()); e.setParams (p);
        e.setTransport (true, 3.0, 120.0);  e.noteOn (1.0f); const auto a = e.lastHit();
        e.setTransport (true, 11.0, 120.0); e.noteOn (1.0f); const auto b = e.lastHit();  // 2 bars later
        e.setTransport (true, 5.0, 120.0);  e.noteOn (1.0f); const auto c = e.lastHit();
        CHECK (std::abs (a.padX - b.padX) < 1e-9 && std::abs (a.padY - b.padY) < 1e-9, "beat 3 of each bar should match");
        CHECK (std::abs (a.padX - c.padX) + std::abs (a.padY - c.padY) > 0.05, "different beats should differ");
        hit::Params q = p; q.stepClock = true; e.setParams (q);
        e.setTransport (false, -1.0, 120.0); e.noteOn (1.0f); e.noteOn (1.0f);
        e.setTransport (true, 0.0, 120.0);  e.noteOn (1.0f); const auto s1 = e.lastHit();
        e.setTransport (false, -1.0, 120.0);
        e.setTransport (true, 0.0, 120.0);  e.noteOn (1.0f); const auto s2 = e.lastHit();
        CHECK (std::abs (s1.padX - s2.padX) < 1e-9, "step pattern restarts when the song starts");
    }

    std::printf ("11) sample-rate conversion, polyphony stress, velocity\n");
    {
        auto s = makeSine (44100.0, 1.0, 500.0, 0.5);
        const int last = lastAudible (renderHit (*s, 48000.0, P (1.0, 0.5), 1.5));
        CHECK (std::abs (last - 48000) < 200, "SRC length off (%d)", last);

        hit::Params p = P (0.7, 0.8, 0.5); p.motion = true; p.depth = 0.3; p.pattern = hit::kDrift;
        hit::Engine e; e.prepare (sr); e.setSample (sine.get()); e.setParams (p);
        std::vector<float> L (480), R (480);
        int maxActive = 0;
        for (int h = 0; h < 40; ++h)
        {
            e.noteOn (0.9f);
            std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
            e.render (L.data(), R.data(), 480);
            for (float v : L) CHECK (std::isfinite (v), "NaN");
            maxActive = std::max (maxActive, e.activeVoices());
        }
        CHECK (maxActive <= hit::Engine::kVoices, "voice cap");

        const double d = rmsDb (renderHit (*sine, sr, P (1.0, 0.5), 0.2, 1.0f), 4800, 4800)
                       - rmsDb (renderHit (*sine, sr, P (1.0, 0.5), 0.2, 0.5f), 4800, 4800);
        CHECK (std::abs (d - 6.02) < 0.3, "velocity 0.5 should be -6 dB, got %.2f", d);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
