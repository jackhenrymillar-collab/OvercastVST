#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

#include <cstdint>
#include <vector>

namespace overcast::dsp
{

// Aurora: modal reverb (Abel, Coffin and Spratt, "A modal architecture for artificial
// reverberation with application to room acoustics modeling", AES 2014). Instead of
// delay lines, the tail is a bank of 2048 resonators, each one a single decaying
// sine (a complex one-pole filter). Because every mode is its own oscillator, the
// whole tail can be retuned while it rings:
// param1 = pitch (-12 to +12 semitones; the tail glides when it moves),
// param2 = harmony (0 = natural, randomly spread modes; 1 = modes drawn onto the
// harmonics of A, so the tail rings as a chord).
// Mod makes every mode drift slightly in pitch, so the tail shimmers and beats.
//
// Long decays: a mode that holds perfectly still rings as its own pitch, and past a few
// seconds of decay the natural (Harmony 0) tail turned into a cloud of separate tones.
// From 3 s to 8 s of decay (and when frozen) every mode keeps a small drift of about
// 2 cents even with Mod at zero, which blends neighbouring modes into a smoother tail.
// The drifts keep their usual slow rates; faster drift made held notes warble.
class Aurora final : public ReverbMachine
{
public:
    static constexpr std::size_t kModes = 2048;

    Aurora()
    {
        for (auto* v : { &re, &im, &ar, &ai, &dar, &dai, &tar, &tai, &bL, &bR, &cL, &cR, &baseHz, &harmonicHz, &rate, &drift, &phase })
            v->assign (kModes, 0.0f);
        layoutModes();
    }

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        // A resonator's response grows with the number of samples per second it rings
        // for, so the output is scaled to sound the same at every sample rate.
        outScale = kOutputScale * 48000.0f / static_cast<float> (fs);
        diffL.prepare (fs, { 4.1f, 6.3f, 9.7f, 13.9f });
        diffR.prepare (fs, { 4.7f, 5.9f, 10.3f, 13.1f });
        pitch.prepare (fs, 250.0f);
        harmony.prepare (fs, 250.0f);
        modAmount.prepare (fs, 100.0f);
        inputGain.prepare (fs, 50.0f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        std::fill (re.begin(), re.end(), 0.0f);
        std::fill (im.begin(), im.end(), 0.0f);
        phase = drift;
        diffL.reset(); diffR.reset();
        pitch.snapTo (pitch.getTarget());
        harmony.snapTo (harmony.getTarget());
        modAmount.snapTo (modAmount.getTarget());
        inputGain.snapTo (inputGain.getTarget());
        counter = 0;
        snapPoles = true;
        updatePoles();
        snapPoles = false;
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        const float semis = (std::clamp (p.param1, 0.0f, 1.0f) - 0.5f) * 24.0f;
        pitch.setTarget (std::pow (2.0f, semis / 12.0f));
        harmony.setTarget (std::clamp (p.param2, 0.0f, 1.0f));
        modAmount.setTarget (std::clamp (p.mod, 0.0f, 1.0f));
        inputGain.setTarget (p.freeze ? 0.0f : 1.0f);
        rtLow = p.decaySeconds * 1.1f;
        rtMid = p.decaySeconds;
        rtHigh = p.decaySeconds * 0.45f;
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        int done = 0;
        while (done < n)
        {
            if (counter == 0)
            {
                // Retune every mode a few hundred times a second: fine enough that pitch
                // glides and drift sound smooth, cheap enough to leave CPU for the modes.
                pitch.skip (static_cast<int> (kUpdate));
                harmony.skip (static_cast<int> (kUpdate));
                modAmount.skip (static_cast<int> (kUpdate));
                updatePoles();
                counter = kUpdate;
            }
            const int len = static_cast<int> (std::min<std::size_t> (counter, static_cast<std::size_t> (n - done)));
            glidePos = kUpdate - counter;
            runModes (inL + done, inR + done, wetL + done, wetR + done, len);
            counter -= static_cast<std::size_t> (len);
            done += len;
        }
    }

private:
    // The modes are run sixteen at a time across a short block: the sixteen lanes are
    // independent, so the inner loop vectorises, and each sample's partial sums
    // are added up once at the end.
    void runModes (const float* inL, const float* inR, float* wetL, float* wetR, int len) noexcept
    {
        constexpr std::size_t W = kLanes;
        float xl[kUpdate], xr[kUpdate];
        for (int i = 0; i < len; ++i)
        {
            const float g = inputGain.next();
            xl[i] = diffL.process (inL[i] * g, 0.6f);
            xr[i] = diffR.process (inR[i] * g, 0.6f);
        }
        std::fill (accL.begin(), accL.begin() + static_cast<std::ptrdiff_t> (static_cast<std::size_t> (len) * W), 0.0f);
        std::fill (accR.begin(), accR.begin() + static_cast<std::ptrdiff_t> (static_cast<std::size_t> (len) * W), 0.0f);
        float* __restrict const aL = accL.data();
        float* __restrict const aR = accR.data();

        for (std::size_t k = 0; k < kModes; k += W)
        {
            float sr[W], si[W], pr[W], pi[W], dr[W], di[W], gl[W], gr[W], ol[W], orr[W];
            for (std::size_t j = 0; j < W; ++j)
            {
                sr[j] = re[k + j]; si[j] = im[k + j];
                pr[j] = ar[k + j]; pi[j] = ai[k + j];   // where this glide started
                dr[j] = dar[k + j]; di[j] = dai[k + j]; // step per sample
                gl[j] = bL[k + j]; gr[j] = bR[k + j];
                ol[j] = cL[k + j]; orr[j] = cR[k + j];
            }
            for (int i = 0; i < len; ++i)
            {
                const float x0 = xl[i], x1 = xr[i];
                // Coefficients glide in a straight line from the last retune to the next.
                // They are worked out from the start point every sample rather than added
                // up step by step: summed steps pile up rounding error, which near a pole
                // radius of 1 is enough to make a frozen tail grow.
                const float t = static_cast<float> (glidePos + static_cast<std::size_t> (i) + 1);
                float* __restrict const l = aL + static_cast<std::size_t> (i) * W;
                float* __restrict const r = aR + static_cast<std::size_t> (i) * W;
                for (std::size_t j = 0; j < W; ++j)
                {
                    const float cr = pr[j] + dr[j] * t, ci = pi[j] + di[j] * t;
                    const float r1 = cr * sr[j] - ci * si[j] + gl[j] * x0 + gr[j] * x1;
                    const float i1 = cr * si[j] + ci * sr[j];
                    sr[j] = r1;
                    si[j] = i1;
                    l[j] += ol[j] * r1;
                    r[j] += orr[j] * i1;
                }
            }
            // Safety guard, once per short block so the loop above still vectorises: a mode
            // can never grow past kModeLimit, and a non-finite value is dropped.
            for (std::size_t j = 0; j < W; ++j)
            {
                const float m2 = sr[j] * sr[j] + si[j] * si[j];
                const float scale = ! (m2 < 1.0e30f) ? 0.0f : (m2 > kModeLimit2 ? std::sqrt (kModeLimit2 / m2) : 1.0f);
                re[k + j] = sr[j] * scale;
                im[k + j] = si[j] * scale;
            }
        }
        for (int i = 0; i < len; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (std::size_t j = 0; j < W; ++j)
            {
                l += aL[static_cast<std::size_t> (i) * W + j];
                r += aR[static_cast<std::size_t> (i) * W + j];
            }
            wetL[i] = l * outScale;
            wetR[i] = r * outScale;
        }
    }

    // Fixed-seed layout so the machine sounds the same every time it loads.
    void layoutModes()
    {
        std::uint32_t seed = 0x2545f491u;
        const auto next = [&seed]
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            return static_cast<float> (seed >> 8) / 16777216.0f;
        };
        const float lo = 40.0f, hi = 15000.0f;
        for (std::size_t k = 0; k < kModes; ++k)
        {
            // Log spacing, warped so higher octaves get more modes (as in a real room),
            // with each mode jittered inside its slot so the spacing never repeats.
            const float x = (static_cast<float> (k) + next()) / static_cast<float> (kModes);
            const float hz = lo * std::pow (hi / lo, std::pow (x, 0.8f));
            baseHz[k] = hz;
            const float n = std::max (1.0f, std::round (hz / kRootHz));
            harmonicHz[k] = n * kRootHz * (1.0f + 0.0025f * (next() - 0.5f)); // slightly detuned partials beat gently
            rate[k] = 0.01f * std::round (5.0f + 35.0f * next()); // 0.05 to 0.4 Hz
            drift[k] = next();
            phase[k] = drift[k];
            bL[k] = (k % 2 == 0) ? (next() < 0.5f ? -1.0f : 1.0f) : 0.0f;
            bR[k] = (k % 2 == 1) ? (next() < 0.5f ? -1.0f : 1.0f) : 0.0f;
            cL[k] = next() < 0.5f ? -1.0f : 1.0f;
            cR[k] = next() < 0.5f ? -1.0f : 1.0f;
        }
        // Weight each mode by the share of the spectrum it covers. Low modes are farther
        // apart than high ones, so with equal weights the low end piled up: the tail
        // sounded heavy and sustained low notes rang loudly. Weighted, the energy per
        // hertz is even, like a dense delay network's.
        const float meanGap = (baseHz[kModes - 1] - baseHz[0]) / static_cast<float> (kModes - 1);
        for (std::size_t k = 0; k < kModes; ++k)
        {
            const float below = baseHz[k > 0 ? k - 1 : k], above = baseHz[k + 1 < kModes ? k + 1 : k];
            const float gap = (above - below) / (k > 0 && k + 1 < kModes ? 2.0f : 1.0f);
            const float w = std::sqrt (std::max (gap, 1.0e-3f) / meanGap);
            bL[k] *= w;
            bR[k] *= w;
        }
    }

    float rt60For (float hz) const noexcept
    {
        // Smooth three-band decay in log frequency: low below 250 Hz, high above 4 kHz.
        const float t = std::clamp (std::log2 (hz / 250.0f) / std::log2 (4000.0f / 250.0f), 0.0f, 1.0f);
        const float lowSide = std::clamp (1.0f - 2.0f * t, 0.0f, 1.0f);
        const float highSide = std::clamp (2.0f * t - 1.0f, 0.0f, 1.0f);
        return lowSide * rtLow + highSide * rtHigh + (1.0f - lowSide - highSide) * rtMid;
    }

    void updatePoles()
    {
        const float ratio = pitch.getCurrent();
        const float h = harmony.getCurrent();
        const float t = params.freeze ? 1.0f : std::clamp ((rtMid - kTailStart) / (kTailFull - kTailStart), 0.0f, 1.0f);
        const float tail = t * t * (3.0f - 2.0f * t);
        const float depth = 0.004f * std::max (modAmount.getCurrent(), kTailDrift * tail);
        const float nyquistGuard = 0.45f * static_cast<float> (fs);
        const float step = static_cast<float> (kUpdate) / static_cast<float> (fs);
        for (std::size_t k = 0; k < kModes; ++k)
        {
            phase[k] += rate[k] * step;
            if (phase[k] >= 1.0f) phase[k] -= 1.0f;
            const float wobble = 1.0f + depth * std::sin (6.28318531f * phase[k]);
            const float hz = ratio * (baseHz[k] + h * (harmonicHz[k] - baseHz[k])) * wobble;
            // A mode pushed above the audio band (by the Pitch knob, or a low sample rate)
            // is held at the band edge and dies away within a few milliseconds; cutting it
            // off at once would click.
            const bool outOfBand = hz >= nyquistGuard;
            const float rt = outOfBand ? 0.01f
                           : params.freeze ? kFreezeSeconds : std::max (0.05f, rt60For (hz));
            const float w = 6.28318531f * std::min (hz, nyquistGuard) / static_cast<float> (fs);
            const float radius = std::exp (-6.90775528f / (rt * static_cast<float> (fs)));
            // Glide from the previous retune's exact values to the new ones over the next block.
            const float inv = 1.0f / static_cast<float> (kUpdate);
            const float nr = radius * std::cos (w), ni = radius * std::sin (w);
            if (snapPoles)
            {
                tar[k] = nr;
                tai[k] = ni;
            }
            ar[k] = tar[k];
            ai[k] = tai[k];
            dar[k] = (nr - tar[k]) * inv;
            dai[k] = (ni - tai[k]) * inv;
            tar[k] = nr;
            tai[k] = ni;
        }
    }

    static constexpr float kTailStart = 3.0f, kTailFull = 8.0f; // seconds of decay
    // Mode safety limit, squared: about 7x (17 dB) above the loudest mode measured with
    // +12 dBFS noise held into a frozen 60 s tail at 96 kHz.
    static constexpr float kModeLimit2 = 32768.0f * 32768.0f;
    static constexpr float kTailDrift = 0.3f;                    // as a share of full Mod
    static constexpr float kRootHz = 55.0f; // A1: the harmony knob pulls modes onto its harmonics
    // Not truly endless: a pole radius any closer to 1 is lost in float rounding and the
    // tail can creep louder. 300 s fades only about 1 dB in five seconds.
    static constexpr float kFreezeSeconds = 300.0f;
    static constexpr std::size_t kUpdate = 128;
    static constexpr std::size_t kLanes = 16; // enough independent modes in flight to hide arithmetic latency
    static constexpr float kOutputScale = 0.00035f;

    double fs = 48000.0;
    std::array<float, kUpdate * kLanes> accL {}, accR {};
    std::vector<float> re, im, ar, ai, dar, dai, tar, tai, bL, bR, cL, cR, baseHz, harmonicHz, rate, drift, phase;
    Diffuser<4> diffL, diffR;
    Smoother pitch, harmony, modAmount, inputGain;
    float rtLow = 3.3f, rtMid = 3.0f, rtHigh = 1.35f;
    std::size_t counter = 0;
    bool snapPoles = false;
    float outScale = kOutputScale;
    std::size_t glidePos = 0;
    MachineParams params;
};

} // namespace overcast::dsp
