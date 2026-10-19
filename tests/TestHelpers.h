#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace overcast::test
{

// RT60 from an impulse response using Schroeder backward integration,
// fitted over the -5 to -25 dB range (T20) and extrapolated to 60 dB.
inline double rt60 (const std::vector<float>& ir, double sampleRate)
{
    std::vector<double> edc (ir.size());
    double acc = 0.0;
    for (std::size_t i = ir.size(); i-- > 0;)
    {
        acc += static_cast<double> (ir[i]) * ir[i];
        edc[i] = acc;
    }
    const double total = edc.front();
    if (total <= 0.0)
        return 0.0;

    std::size_t i5 = 0, i25 = 0;
    for (std::size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / total + 1e-30);
        if (i5 == 0 && db <= -5.0) i5 = i;
        if (db <= -25.0) { i25 = i; break; }
    }
    if (i25 <= i5)
        return 0.0;
    const double seconds = static_cast<double> (i25 - i5) / sampleRate;
    return seconds * 60.0 / 20.0;
}

inline double energy (const float* x, std::size_t n)
{
    double e = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        e += static_cast<double> (x[i]) * x[i];
    return e;
}

inline double correlation (const std::vector<float>& a, const std::vector<float>& b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        ab += static_cast<double> (a[i]) * b[i];
        aa += static_cast<double> (a[i]) * a[i];
        bb += static_cast<double> (b[i]) * b[i];
    }
    return ab / std::sqrt (aa * bb + 1e-30);
}

inline bool allFinite (const std::vector<float>& x)
{
    return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); });
}

} // namespace overcast::test
