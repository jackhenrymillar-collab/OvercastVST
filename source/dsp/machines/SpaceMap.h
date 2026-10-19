#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

namespace overcast::dsp
{

// Space Map: places the sound in a modelled shoebox room using the image-source method
// (Allen and Berkley, 1979): every first- and second-order wall reflection is computed
// for each ear, then a light FDN adds the diffuse tail. Moving the controls glides the
// reflections smoothly to their new positions.
// param1 = room shape (small room to long hall), param2 = listener distance from the source.
class SpaceMap final : public ReverbMachine
{
public:
    static constexpr std::size_t kTaps = 24; // 6 first-order + 18 second-order reflections

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        earL.prepare (fs, 260.0f);
        earR.prepare (fs, 260.0f);
        fdn.prepare (fs, 40.0f * 1.6f);
        lastShape = lastPos = -1.0f;
        setParams (params);
        reset();
    }

    void reset() override
    {
        earL.reset(); earR.reset(); earL.snap(); earR.snap();
        fdn.reset(); airL.reset(); airR.reset();
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        const float shape = std::clamp (p.param1, 0.0f, 1.0f);
        const float pos = std::clamp (p.param2, 0.0f, 1.0f);

        // Room dimensions in metres: small room -> studio -> long hall.
        const float t = shape * 2.0f;
        const auto lerp3 = [t] (float a, float b, float c) { return t <= 1.0f ? a + t * (b - a) : b + (t - 1.0f) * (c - b); };
        const float L = lerp3 (4.0f, 9.0f, 24.0f), W = lerp3 (3.5f, 6.5f, 12.0f), H = lerp3 (2.6f, 3.5f, 8.0f);

        if (std::abs (shape - lastShape) > 1.0e-6f || std::abs (pos - lastPos) > 1.0e-6f)
        {
            lastShape = shape;
            lastPos = pos;
            const float sx = 0.25f * L, sy = 0.5f * W, sz = 1.5f;
            const float lx = (0.32f + 0.6f * pos) * L, ly = 0.45f * W, lz = 1.6f;
            computeEar (earL, L, W, H, sx, sy, sz, lx, ly + 0.09f, lz);
            computeEar (earR, L, W, H, sx, sy, sz, lx, ly - 0.09f, lz);
        }

        // Diffuse tail sized to the room.
        const float size = std::cbrt (L * W * H) / 6.0f;
        std::array<float, 8> ms {};
        for (std::size_t i = 0; i < 8; ++i)
            ms[i] = std::clamp (kLate[i] * size, 3.0f, 40.0f * 1.6f);
        fdn.setDelays (ms);
        DecaySettings d;
        d.rtMid = p.decaySeconds;
        d.rtLow = p.decaySeconds * 1.1f;
        d.rtHigh = p.decaySeconds * 0.5f;
        fdn.setDecay (d);
        fdn.setModulation (p.mod * 0.4f);
        fdn.setFreeze (p.freeze);
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        const float air = OnePoleLowpass::coefficientFor (9000.0f, fs);
        for (int i = 0; i < n; ++i)
        {
            const float mono = 0.5f * (inL[i] + inR[i]);
            const float el = airL.process (earL.process (mono), air);
            const float er = airR.process (earR.process (mono), air);
            float tl, tr;
            fdn.process (el, er, tl, tr);
            wetL[i] = el + 0.55f * tl;
            wetR[i] = er + 0.55f * tr;
        }
    }

private:
    static void computeEar (TapDelay<kTaps>& ear, float L, float W, float H,
                            float sx, float sy, float sz, float lx, float ly, float lz)
    {
        // Image coordinate along one axis for reflection index n (|n| = number of bounces).
        const auto image = [] (int n, float size, float src) {
            return static_cast<float> (n) * size + ((n % 2 == 0) ? src : size - src);
        };
        std::size_t tap = 0;
        for (int nx = -2; nx <= 2; ++nx)
            for (int ny = -2; ny <= 2; ++ny)
                for (int nz = -2; nz <= 2; ++nz)
                {
                    const int order = std::abs (nx) + std::abs (ny) + std::abs (nz);
                    if (order == 0 || order > 2 || tap >= kTaps)
                        continue;
                    const float dx = image (nx, L, sx) - lx;
                    const float dy = image (ny, W, sy) - ly;
                    const float dz = image (nz, H, sz) - lz;
                    const float r = std::sqrt (dx * dx + dy * dy + dz * dz);
                    const float gain = std::pow (0.8f, static_cast<float> (order)) / std::max (r, 1.0f);
                    ear.setTap (tap++, r / 343.0f * 1000.0f, 2.2f * gain);
                }
        ear.setNumTaps (tap);
    }

    static constexpr std::array<float, 8> kLate { 13.1f, 17.3f, 19.9f, 23.7f, 27.1f, 31.3f, 35.9f, 39.7f };

    double fs = 48000.0;
    TapDelay<kTaps> earL, earR;
    OnePoleLowpass airL, airR;
    Fdn<8> fdn;
    float lastShape = -1.0f, lastPos = -1.0f;
    MachineParams params;
};

} // namespace overcast::dsp
