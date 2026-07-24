#pragma once

// =============================================================================
//  DEXTRO DELAY — 5-band EQ (shared DSP + UI helper)
// -----------------------------------------------------------------------------
//  A dependency-free RBJ biquad EQ used to tone-shape the WET (delay) signal.
//  The same coefficient math backs both the audio path (Biquad::process) and
//  the on-screen curve (magnitudeDb), so the drawn response matches what's
//  heard. Five fixed-type bands: a low shelf, three bells, and a high shelf;
//  each band's point controls frequency (x) and gain (y).
// =============================================================================

#include <cmath>
#include <array>

namespace dxeq
{
    static constexpr int kNumBands = 5;

    enum class Type { LowShelf, Peak, HighShelf };

    struct BandConfig
    {
        Type  type;
        float q;
        float defFreq;
        float minFreq;
        float maxFreq;
    };

    // The band layout. Ranges overlap moderately but keep a sensible left→right
    // order for a clean 5-point display.
    inline const std::array<BandConfig, kNumBands>& bands()
    {
        static const std::array<BandConfig, kNumBands> b {{
            { Type::LowShelf,  0.70f,   100.0f,   30.0f,   400.0f },
            { Type::Peak,      1.00f,   300.0f,   80.0f,  1200.0f },
            { Type::Peak,      1.00f,  1000.0f,  300.0f,  4000.0f },
            { Type::Peak,      1.00f,  3500.0f, 1500.0f,  9000.0f },
            { Type::HighShelf, 0.70f,  9000.0f, 3000.0f, 18000.0f },
        }};
        return b;
    }

    static constexpr float kMaxGainDb = 18.0f;
    static constexpr float kMinQ = 0.30f;   // wide / gentle
    static constexpr float kMaxQ = 6.0f;    // narrow / surgical

    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    // RBJ cookbook coefficients (normalised so a0 = 1).
    inline Coeffs computeCoeffs (Type type, double freqHz, double gainDb, double q, double sr)
    {
        Coeffs c;
        if (sr <= 0.0) return c;

        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * M_PI * (freqHz / sr);
        const double cw = std::cos (w0);
        const double sw = std::sin (w0);
        const double alpha = sw / (2.0 * (q > 0.0001 ? q : 0.0001));

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type)
        {
            case Type::Peak:
            {
                b0 = 1.0 + alpha * A;
                b1 = -2.0 * cw;
                b2 = 1.0 - alpha * A;
                a0 = 1.0 + alpha / A;
                a1 = -2.0 * cw;
                a2 = 1.0 - alpha / A;
                break;
            }
            case Type::LowShelf:
            {
                const double sqA = 2.0 * std::sqrt (A) * alpha;
                b0 =      A * ((A + 1.0) - (A - 1.0) * cw + sqA);
                b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
                b2 =      A * ((A + 1.0) - (A - 1.0) * cw - sqA);
                a0 =           (A + 1.0) + (A - 1.0) * cw + sqA;
                a1 = -2.0 *   ((A - 1.0) + (A + 1.0) * cw);
                a2 =           (A + 1.0) + (A - 1.0) * cw - sqA;
                break;
            }
            case Type::HighShelf:
            {
                const double sqA = 2.0 * std::sqrt (A) * alpha;
                b0 =      A * ((A + 1.0) + (A - 1.0) * cw + sqA);
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
                b2 =      A * ((A + 1.0) + (A - 1.0) * cw - sqA);
                a0 =           (A + 1.0) - (A - 1.0) * cw + sqA;
                a1 =  2.0 *   ((A - 1.0) - (A + 1.0) * cw);
                a2 =           (A + 1.0) - (A - 1.0) * cw - sqA;
                break;
            }
        }

        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
        c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }

    // Magnitude (dB) of a biquad at a given frequency — for drawing the curve.
    inline double magnitudeDb (const Coeffs& c, double freqHz, double sr)
    {
        const double w = 2.0 * M_PI * (freqHz / sr);
        const double cw1 = std::cos (-w),     sw1 = std::sin (-w);
        const double cw2 = std::cos (-2.0*w), sw2 = std::sin (-2.0*w);

        const double numRe = c.b0 + c.b1 * cw1 + c.b2 * cw2;
        const double numIm =        c.b1 * sw1 + c.b2 * sw2;
        const double denRe = 1.0  + c.a1 * cw1 + c.a2 * cw2;
        const double denIm =        c.a1 * sw1 + c.a2 * sw2;

        const double numMag2 = numRe * numRe + numIm * numIm;
        const double denMag2 = denRe * denRe + denIm * denIm;
        if (denMag2 < 1e-20) return 0.0;
        return 10.0 * std::log10 (numMag2 / denMag2);
    }

    // Direct-Form-I biquad: settable coeffs, persistent state.
    class Biquad
    {
    public:
        void setCoeffs (const Coeffs& c) { co = c; }
        void reset() { x1 = x2 = y1 = y2 = 0.0f; }

        inline float process (float x)
        {
            const double y = co.b0 * x + co.b1 * x1 + co.b2 * x2
                                       - co.a1 * y1 - co.a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = (float) y;
            return (float) y;
        }

    private:
        Coeffs co;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };
}
