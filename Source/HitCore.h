// HitCore.h — JUCE-free DSP core for HitPad.
//
// Signal flow per voice:   sample  ->  saturator (Y)  ->  decay VCA (X)  ->  sum  ->  level
//
// Keeping this header free of JUCE lets it be unit-tested with a plain compiler
// (see tests/core_test.cpp) and reused later (multi-pad version, other formats).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hit
{
//==============================================================================
// Parameter mappings (shared by the DSP and the UI so they can never disagree)
//==============================================================================
constexpr double kMinT60    = 0.02;   // seconds to reach -60 dB at X = 0
constexpr double kMaxT60    = 2.0;    // seconds to reach -60 dB at X = kOpenFrom
constexpr double kOpenFrom  = 0.97;   // above this X the decay relaxes to "no decay"
constexpr double kMinDriveDb = -18.0; // Y = 0  (practically linear)
constexpr double kMaxDriveDb = 24.0;  // Y = 1  (heavy)
constexpr double kLn1000    = 6.907755278982137; // ln(10^3): -60 dB as a natural-log rate

inline double clamp01 (double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

/** Decay rate in nepers/second for knob position X (0..1). Gain(t) = exp(-rate * t). */
inline double decayRatePerSec (double x)
{
    x = clamp01 (x);
    const double xe  = std::min (x, kOpenFrom) / kOpenFrom;           // 0..1 across the usable range
    const double t60 = kMinT60 * std::pow (kMaxT60 / kMinT60, xe);    // log-spaced 20 ms .. 2 s
    const double open = x > kOpenFrom ? (x - kOpenFrom) / (1.0 - kOpenFrom) : 0.0;
    return (kLn1000 / t60) * (1.0 - open);                            // -> 0 at X = 1 (sample plays out fully)
}

/** Effective time to -60 dB in seconds (infinity when the decay is fully open). */
inline double effectiveT60 (double x)
{
    const double r = decayRatePerSec (x);
    return r <= 1.0e-9 ? INFINITY : kLn1000 / r;
}

/** Envelope gain at time t seconds after the hit, for display purposes. */
inline double envelopeAt (double tSeconds, double x) { return std::exp (-decayRatePerSec (x) * tSeconds); }

inline double driveDbFor (double y) { return kMinDriveDb + (kMaxDriveDb - kMinDriveDb) * clamp01 (y); }
inline double dbToGain (double db)  { return std::exp (db * 0.11512925464970229); }

//==============================================================================
/** Loaded sample. Stereo always (mono sources are duplicated), immutable once shared. */
struct SampleData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    std::string name;

    int    numFrames() const      { return (int) left.size(); }
    double durationSeconds() const { return sampleRate > 0 ? numFrames() / sampleRate : 0.0; }
};

//==============================================================================
/** tanh saturator using first-order antiderivative anti-aliasing (ADAA).
    Output is scaled by 1/tanh(drive) so a full-scale input peaks at ~1.0 at any
    drive setting — turning Y up gets denser, not louder.
    ADAA adds a half-sample delay; irrelevant for a one-shot player. */
class Saturator
{
public:
    void reset() { u1 = 0.0; F1 = 0.0; }

    inline float process (float x, double drive, double norm)
    {
        const double u  = (double) x * drive;
        const double F  = logCosh (u);
        const double du = u - u1;
        double y;
        if (std::abs (du) < 1.0e-6)  y = std::tanh (0.5 * (u + u1));
        else                         y = (F - F1) / du;
        u1 = u;
        F1 = F;
        return (float) (y * norm);
    }

private:
    static double logCosh (double x)
    {
        const double a = std::abs (x);
        return a - 0.6931471805599453 + std::log1p (std::exp (-2.0 * a)); // stable for large |x|
    }
    double u1 = 0.0, F1 = 0.0;
};

//==============================================================================
/** One-shot sample engine: 6 overlapping hits (+2 spare slots for 3 ms declick steals). */
class Engine
{
public:
    static constexpr int kVoices = 8;
    static constexpr int kMaxSounding = 6;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        smoothCoef = 1.0 - std::exp (-1.0 / (0.010 * sampleRate)); // 10 ms param smoothing
        stealStep  = 1.0f / (float) (0.003 * sampleRate);          // 3 ms declick
        killAll();
        snapParams();
    }

    /** Audio thread only. The pointer must outlive its use (the processor keeps it alive). */
    void setSample (const SampleData* s)
    {
        if (s == sample) return;
        killAll();
        sample = s;
    }

    /** x, sat in 0..1; level in dB. */
    void setParams (double x, double sat, double levelDb)
    {
        xTarget = clamp01 (x);
        yTarget = clamp01 (sat);
        lTarget = levelDb;
    }
    void snapParams() { xSmooth = xTarget; ySmooth = yTarget; lSmooth = lTarget; }

    void killAll()
    {
        for (auto& v : voices) v.active = false;
    }

    void noteOn (float velocity)
    {
        if (sample == nullptr || sample->numFrames() < 2) return;

        ++ageCounter;

        int sounding = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.releasing)
            {
                ++sounding;
                if (oldest == nullptr || v.age < oldest->age) oldest = &v;
            }
        if (sounding >= kMaxSounding && oldest != nullptr)
            oldest->releasing = true; // fades out over 3 ms

        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active) { slot = &v; break; }

        if (slot == nullptr) // every slot busy: hard-reuse the oldest
        {
            slot = &voices[0];
            for (auto& v : voices) if (v.age < slot->age) slot = &v;
        }

        slot->active    = true;
        slot->releasing = false;
        slot->relGain   = 1.0f;
        slot->pos       = 0.0;
        slot->inc       = sample->sampleRate / sampleRate;
        slot->env       = 1.0f;
        slot->vel       = std::clamp (velocity, 0.0f, 1.0f);
        slot->age       = ageCounter;
        slot->satL.reset();
        slot->satR.reset();
    }

    int activeVoices() const
    {
        int n = 0;
        for (auto& v : voices) if (v.active) ++n;
        return n;
    }

    /** Adds n frames into outL/outR. */
    void render (float* outL, float* outR, int n)
    {
        const int total = sample != nullptr ? sample->numFrames() : 0;
        const double sc = smoothCoef;

        for (int i = 0; i < n; ++i)
        {
            xSmooth += (xTarget - xSmooth) * sc;
            ySmooth += (yTarget - ySmooth) * sc;
            lSmooth += (lTarget - lSmooth) * sc;

            if (total < 2) continue;

            const float  decayCoef = (float) std::exp (-decayRatePerSec (xSmooth) / sampleRate);
            const double drive     = dbToGain (driveDbFor (ySmooth));
            const double norm      = 1.0 / std::tanh (drive);
            const float  level     = (float) dbToGain (lSmooth);

            float sumL = 0.0f, sumR = 0.0f;

            for (auto& v : voices)
            {
                if (! v.active) continue;

                const int   i0   = (int) v.pos;
                const int   i1   = std::min (i0 + 1, total - 1);
                const float frac = (float) (v.pos - (double) i0);

                float sl = sample->left [(size_t) i0] + (sample->left [(size_t) i1] - sample->left [(size_t) i0]) * frac;
                float sr = sample->right[(size_t) i0] + (sample->right[(size_t) i1] - sample->right[(size_t) i0]) * frac;

                sl = v.satL.process (sl, drive, norm);
                sr = v.satR.process (sr, drive, norm);

                const double framesLeft = (double) (total - 1) - v.pos;
                const float  endFade = framesLeft < kEndFadeFrames ? (float) (framesLeft / kEndFadeFrames) : 1.0f;

                const float amp = v.vel * v.env * endFade * v.relGain;
                sumL += sl * amp;
                sumR += sr * amp;

                v.env *= decayCoef;
                if (v.releasing)
                {
                    v.relGain -= stealStep;
                    if (v.relGain <= 0.0f) v.active = false;
                }
                v.pos += v.inc;
                if (v.pos >= (double) (total - 1) || v.env < 1.0e-4f) // -80 dB or end of sample
                    v.active = false;
            }

            outL[i] += sumL * level;
            outR[i] += sumR * level;
        }
    }

private:
    static constexpr double kEndFadeFrames = 64.0;

    struct Voice
    {
        bool     active = false, releasing = false;
        double   pos = 0.0, inc = 1.0;
        float    env = 1.0f, vel = 1.0f, relGain = 1.0f;
        uint64_t age = 0;
        Saturator satL, satR;
    };

    std::array<Voice, kVoices> voices {};
    const SampleData* sample = nullptr;

    double   sampleRate = 44100.0, smoothCoef = 0.01;
    float    stealStep = 0.01f;
    double   xTarget = 0.5, yTarget = 0.25, lTarget = 0.0;
    double   xSmooth = 0.5, ySmooth = 0.25, lSmooth = 0.0;
    uint64_t ageCounter = 0;
};

} // namespace hit
