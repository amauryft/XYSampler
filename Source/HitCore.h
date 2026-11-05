// HitCore.h — JUCE-free DSP core for XYSampler.
//
// Per hit:   pad point (+ motion offset)  ->  decay rate & pitch, frozen for that hit
// Per voice: sample (resampled for pitch) -> saturator (drive) -> decay VCA -> sum -> level
//
// Kept free of JUCE so it can be unit-tested with a plain compiler (tests/core_test.cpp)
// and shared with the planned audio-effect version.
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
// Mappings (shared by DSP and UI)
//==============================================================================
constexpr double kMinT60     = 0.02;  // seconds to -60 dB at decay 0
constexpr double kMaxT60     = 2.0;   // seconds to -60 dB at decay kOpenFrom
constexpr double kOpenFrom   = 0.97;  // above this the decay relaxes to "no decay"
constexpr double kMinDriveDb = -18.0;
constexpr double kMaxDriveDb = 24.0;
constexpr double kLn1000     = 6.907755278982137;
constexpr double kPi         = 3.14159265358979323846;

inline double clamp01 (double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

/** Decay rate (nepers/s) for a decay position 0..1. Gain(t) = exp(-rate * t). */
inline double decayRatePerSec (double x)
{
    x = clamp01 (x);
    const double xe   = std::min (x, kOpenFrom) / kOpenFrom;
    const double t60  = kMinT60 * std::pow (kMaxT60 / kMinT60, xe);
    const double open = x > kOpenFrom ? (x - kOpenFrom) / (1.0 - kOpenFrom) : 0.0;
    return (kLn1000 / t60) * (1.0 - open);
}
inline double effectiveT60 (double x)
{
    const double r = decayRatePerSec (x);
    return r <= 1.0e-9 ? INFINITY : kLn1000 / r;
}
inline double envelopeAt (double t, double x) { return std::exp (-decayRatePerSec (x) * t); }
inline double driveDbFor (double y)           { return kMinDriveDb + (kMaxDriveDb - kMinDriveDb) * clamp01 (y); }
inline double dbToGain (double db)            { return std::exp (db * 0.11512925464970229); }

/** X sensitivity squeezes the pad's decay span around the middle of the range. */
inline double decayPosition (double padX, double xSens) { return clamp01 (0.5 + (padX - 0.5) * xSens); }
/** Pad Y (0..1, centre = 0) to semitones; ySens = semitones at the top/bottom edge. */
inline double pitchSemitones (double padY, double ySens) { return (clamp01 (padY) - 0.5) * 2.0 * ySens; }

//==============================================================================
// Motion patterns: unit vectors the pad point is pushed along, phase in cycles
//==============================================================================
enum Pattern { kCircle = 0, kFigure8, kDecayOnly, kPitchOnly, kCorners, kDrift, kNumPatterns };

inline void motionVector (int pattern, double phase, double& vx, double& vy)
{
    const double th = 2.0 * kPi * phase;
    switch (pattern)
    {
        case kCircle:    vx = std::cos (th); vy = std::sin (th); return;
        case kFigure8:   vx = std::sin (th); vy = std::sin (2.0 * th); return;
        case kDecayOnly: vx = std::sin (th); vy = 0.0; return;
        case kPitchOnly: vx = 0.0; vy = std::sin (th); return;
        case kCorners:
        {
            static const double c[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
            double f = phase - std::floor (phase);
            const int i = std::min (3, (int) (f * 4.0));
            vx = c[i][0] * 0.75; vy = c[i][1] * 0.75; return;
        }
        default: // drift: smooth pseudo-random wander
            vx = 0.6 * std::sin (th) + 0.4 * std::sin (th * 2.3 + 1.0);
            vy = 0.6 * std::sin (th * 1.3 + 2.0) + 0.4 * std::sin (th * 3.1);
            return;
    }
}

//==============================================================================
struct SampleData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    std::string name;

    int    numFrames() const       { return (int) left.size(); }
    double durationSeconds() const { return sampleRate > 0 ? numFrames() / sampleRate : 0.0; }
};

struct Params
{
    double x = 0.55, y = 0.5;    // pad point
    double xSens = 1.0;          // 0.1..1
    double ySens = 12.0;         // semitones at the pad edge
    double sat = 0.25;           // 0..1
    double levelDb = 0.0;
    bool   motion = false;
    int    pattern = kCircle;
    double depth = 0.18;         // pad units (0..0.5)
    double cycleBeats = 4.0;     // one pattern cycle, in quarter notes
    bool   stepClock = false;    // true: advance 1/8 cycle per hit instead of with time
};

/** What a hit actually got (for the UI). */
struct HitInfo { double padX = 0.5, padY = 0.5, t60 = 0.0, semitones = 0.0; };

//==============================================================================
class Saturator
{
public:
    void reset() { u1 = 0.0; F1 = 0.0; }
    inline float process (float x, double drive, double norm)
    {
        const double u = (double) x * drive, F = logCosh (u), du = u - u1;
        const double y = std::abs (du) < 1.0e-6 ? std::tanh (0.5 * (u + u1)) : (F - F1) / du;
        u1 = u; F1 = F;
        return (float) (y * norm);
    }
private:
    static double logCosh (double x) { const double a = std::abs (x); return a - 0.6931471805599453 + std::log1p (std::exp (-2.0 * a)); }
    double u1 = 0.0, F1 = 0.0;
};

//==============================================================================
class Engine
{
public:
    static constexpr int    kVoices = 8;
    static constexpr int    kMaxSounding = 6;
    static constexpr double kStep = 1.0 / 8.0;   // step-clock advance per hit, in cycles

    void prepare (double sr)
    {
        sampleRate = sr;
        smoothCoef = 1.0 - std::exp (-1.0 / (0.010 * sampleRate));
        stealStep  = 1.0f / (float) (0.003 * sampleRate);
        killAll();
        snapParams();
    }

    void setSample (const SampleData* s) { if (s != sample) { killAll(); sample = s; } }
    void setParams (const Params& np)    { p = np; p.depth = std::clamp (p.depth, 0.0, 0.5); p.cycleBeats = std::max (0.0625, p.cycleBeats); }
    void snapParams()                    { satSmooth = p.sat; lvlSmooth = p.levelDb; }

    /** Call once per block before render(). ppq < 0 = unknown. */
    void setTransport (bool playing, double ppq, double bpmIn)
    {
        bpm = bpmIn > 1.0 ? bpmIn : 120.0;
        if (playing && ! wasPlaying) stepPhase = 0.0;          // restart the pattern with the song
        if (playing && ppq >= 0.0) phase = ppq / p.cycleBeats;  // locked to the bar: loops repeat exactly
        wasPlaying = playing;
    }

    void killAll() { for (auto& v : voices) v.active = false; }

    /** Pad point (in pad units) the next hit would get at the current phase. */
    void currentPoint (double& px, double& py) const
    {
        px = p.x; py = p.y;
        if (! p.motion) return;
        double vx, vy;
        motionVector (p.pattern, p.stepClock ? stepPhase : phase, vx, vy);
        px = clamp01 (p.x + p.depth * vx);
        py = clamp01 (p.y + p.depth * vy);
    }

    void noteOn (float velocity)
    {
        double px, py;
        currentPoint (px, py);                                   // sample first, so hit 1 lands on step 0
        if (p.motion && p.stepClock) stepPhase += kStep;
        const double xe = decayPosition (px, p.xSens);
        const double st = pitchSemitones (py, p.ySens);
        last = { px, py, effectiveT60 (xe), st };
        ++hitSerial;

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
        if (sounding >= kMaxSounding && oldest != nullptr) oldest->releasing = true;

        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot == nullptr) { slot = &voices[0]; for (auto& v : voices) if (v.age < slot->age) slot = &v; }

        slot->active    = true;
        slot->releasing = false;
        slot->relGain   = 1.0f;
        slot->pos       = 0.0;
        slot->inc       = sample->sampleRate / sampleRate * std::pow (2.0, st / 12.0);
        slot->env       = 1.0f;
        slot->decayCoef = (float) std::exp (-decayRatePerSec (xe) / sampleRate);
        slot->vel       = std::clamp (velocity, 0.0f, 1.0f);
        slot->age       = ageCounter;
        slot->satL.reset();
        slot->satR.reset();
    }

    int activeVoices() const { int n = 0; for (auto& v : voices) if (v.active) ++n; return n; }
    const HitInfo& lastHit() const { return last; }
    uint64_t hitCount() const { return hitSerial; }
    double motionPhase() const { return p.stepClock ? stepPhase : phase; }

    void render (float* outL, float* outR, int n)
    {
        const int total = sample != nullptr ? sample->numFrames() : 0;
        const double phInc = bpm / 60.0 / sampleRate / p.cycleBeats;

        for (int i = 0; i < n; ++i)
        {
            satSmooth += (p.sat - satSmooth) * smoothCoef;
            lvlSmooth += (p.levelDb - lvlSmooth) * smoothCoef;
            phase += phInc;

            if (total < 2) continue;

            const double drive = dbToGain (driveDbFor (satSmooth));
            const double norm  = 1.0 / std::tanh (drive);
            const float  level = (float) dbToGain (lvlSmooth);
            float sumL = 0.0f, sumR = 0.0f;

            for (auto& v : voices)
            {
                if (! v.active) continue;

                const int   i0 = (int) v.pos;
                const int   i1 = std::min (i0 + 1, total - 1);
                const float fr = (float) (v.pos - (double) i0);
                float sl = sample->left [(size_t) i0] + (sample->left [(size_t) i1] - sample->left [(size_t) i0]) * fr;
                float sr = sample->right[(size_t) i0] + (sample->right[(size_t) i1] - sample->right[(size_t) i0]) * fr;

                sl = v.satL.process (sl, drive, norm);
                sr = v.satR.process (sr, drive, norm);

                const double left = ((double) (total - 1) - v.pos) / v.inc;           // output frames remaining
                const float  endFade = left < kEndFadeFrames ? (float) (left / kEndFadeFrames) : 1.0f;
                const float  amp = v.vel * v.env * endFade * v.relGain;
                sumL += sl * amp;
                sumR += sr * amp;

                v.env *= v.decayCoef;
                if (v.releasing && (v.relGain -= stealStep) <= 0.0f) v.active = false;
                v.pos += v.inc;
                if (v.pos >= (double) (total - 1) || v.env < 1.0e-4f) v.active = false;
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
        float    env = 1.0f, decayCoef = 1.0f, vel = 1.0f, relGain = 1.0f;
        uint64_t age = 0;
        Saturator satL, satR;
    };

    std::array<Voice, kVoices> voices {};
    const SampleData* sample = nullptr;
    Params   p;
    HitInfo  last;
    double   sampleRate = 44100.0, smoothCoef = 0.01, bpm = 120.0;
    double   satSmooth = 0.25, lvlSmooth = 0.0;
    double   phase = 0.0, stepPhase = 0.0;
    bool     wasPlaying = false;
    float    stealStep = 0.01f;
    uint64_t ageCounter = 0, hitSerial = 0;
};

} // namespace hit
