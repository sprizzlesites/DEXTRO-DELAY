#pragma once

// =============================================================================
//  DEXTRO DELAY — self-ducking stereo delay engine
// -----------------------------------------------------------------------------
//  A stereo delay with feedback, tone shaping (hi-cut + lo-cut in the feedback
//  path) and optional ping-pong. Its signature feature is SELF-DUCKING: the dry
//  input (a vocal) is used as its own sidechain. An envelope follower watches
//  the dry signal; while the vocal is present the wet (echo) output is pulled
//  down, and in the gaps between phrases the echoes swell back up to full. The
//  delay/feedback network keeps running underneath, so the tails are always
//  "there" — ducking only controls how much of them you hear.
//
//  Intentionally dependency-free (only the C++ standard library) so it can be
//  compiled into the JUCE plugin and into a headless offline test alike.
// =============================================================================

#include <vector>
#include <cmath>
#include <algorithm>

#include "Equalizer.h"

class DuckingDelay
{
public:
    struct Params
    {
        // --- delay / feedback ---
        float delayMsL    = 380.0f;   // left  delay time  (ms)
        float delayMsR    = 380.0f;   // right delay time  (ms)
        float feedback    = 0.45f;    // 0 .. 0.98
        float dampHz      = 6500.0f;  // hi-cut in the feedback path (repeats darken)
        float lowCutHz    = 120.0f;   // lo-cut in the feedback path (repeats thin)
        float mix         = 0.35f;    // 0 = dry, 1 = wet (equal-power-ish crossfade)
        float width       = 1.0f;     // stereo width of the wet signal (0 = mono)
        bool  pingpong    = false;    // cross-couple the feedback L<->R

        // --- self-ducking (sidechain = dry input) ---
        float duckDepthDb = 18.0f;    // max gain reduction of the wet when vocal is loud
        float thresholdDb = -32.0f;   // vocal level above which ducking engages
        float attackMs    = 12.0f;    // how fast the echoes duck when the vocal enters
        float releaseMs   = 320.0f;   // how fast the echoes rise back during silence

        // --- wet EQ (5-point) ---
        bool  eqOn = false;
        float eqFreq[dxeq::kNumBands] { 100.0f, 300.0f, 1000.0f, 3500.0f, 9000.0f };
        float eqGain[dxeq::kNumBands] { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        // --- output ---
        float outputGainDb = 0.0f;
    };

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = (sampleRate > 0.0 ? sampleRate : 44100.0);

        const int maxDelaySamples = (int) std::ceil (sr * (maxDelayMs * 0.001)) + 8;
        bufLen = std::max (maxDelaySamples, 64);
        bufL.assign ((size_t) bufLen, 0.0f);
        bufR.assign ((size_t) bufLen, 0.0f);

        reset();
        recomputeCoeffs();
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        wIdx = 0;
        lpL = lpR = hpL = hpR = 0.0f;
        for (int i = 0; i < dxeq::kNumBands; ++i) { eqL[i].reset(); eqR[i].reset(); }
        detEnv = 0.0f;
        duckGain = 1.0f;
        smDelayL = msToSamples (p.delayMsL);
        smDelayR = msToSamples (p.delayMsR);
        duckGainMeter = 1.0f;
        inputEnvMeter = 0.0f;
    }

    void setParams (const Params& newParams)
    {
        p = newParams;
        recomputeCoeffs();
    }

    const Params& getParams() const { return p; }

    // Process one interleaved-by-pointer stereo block, in place.
    void process (float* left, float* right, int numSamples)
    {
        const float wet     = p.mix;
        const float dryGain = std::sqrt (1.0f - std::min (1.0f, wet));  // gentle crossfade
        const float wetGain = std::sqrt (std::min (1.0f, wet));
        const float outGain = dbToGain (p.outputGainDb);
        const float fb      = std::min (0.98f, std::max (0.0f, p.feedback));
        const float targetDL = msToSamples (p.delayMsL);
        const float targetDR = msToSamples (p.delayMsR);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = right[n];

            // ---- sidechain detector (dry input is its own key) -------------
            const float rect = 0.5f * (std::fabs (dryL) + std::fabs (dryR));
            if (rect > detEnv) detEnv += detAtkCoeff * (rect - detEnv);
            else               detEnv += detRelCoeff * (rect - detEnv);
            inputEnvMeter = detEnv;

            const float envDb  = gainToDb (detEnv);
            const float over   = envDb - p.thresholdDb;                // dB above threshold
            const float t      = clamp01 (over / kneeDb);              // 0..1 across the knee
            const float redDb  = -p.duckDepthDb * t;                   // target reduction (dB)
            const float target = dbToGain (redDb);                     // target wet gain (linear)

            // attack when ducking down, release when rising back up
            if (target < duckGain) duckGain += duckAtkCoeff * (target - duckGain);
            else                   duckGain += duckRelCoeff * (target - duckGain);
            duckGainMeter = duckGain;

            // ---- smooth the delay times (tape-like glide on automation) ----
            smDelayL += 0.0008f * (targetDL - smDelayL);
            smDelayR += 0.0008f * (targetDR - smDelayR);

            const float delayedL = readFrac (bufL, smDelayL);
            const float delayedR = readFrac (bufR, smDelayR);

            // ---- tone shaping inside the feedback path ---------------------
            // hi-cut (one-pole LP): repeats get darker
            lpL += dampCoeff * (delayedL - lpL);
            lpR += dampCoeff * (delayedR - lpR);
            // lo-cut (one-pole HP): repeats get thinner
            hpL += lowCutCoeff * (lpL - hpL);
            hpR += lowCutCoeff * (lpR - hpR);
            const float filtL = lpL - hpL;
            const float filtR = lpR - hpR;

            // ---- write back into the delay line ----------------------------
            float writeL, writeR;
            if (p.pingpong)
            {
                writeL = dryL + filtR * fb;
                writeR = dryR + filtL * fb;
            }
            else
            {
                writeL = dryL + filtL * fb;
                writeR = dryR + filtR * fb;
            }
            bufL[(size_t) wIdx] = writeL;
            bufR[(size_t) wIdx] = writeR;
            wIdx = (wIdx + 1) % bufLen;

            // ---- wet = the filtered delayed signal, width + ducking --------
            float wetL = filtL;
            float wetR = filtR;

            // stereo width (mid/side) on the wet only
            const float mid  = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * p.width;
            wetL = mid + side;
            wetR = mid - side;

            // 5-band EQ shapes the echo tone (before ducking controls its level)
            if (p.eqOn)
                for (int b = 0; b < dxeq::kNumBands; ++b)
                {
                    wetL = eqL[b].process (wetL);
                    wetR = eqR[b].process (wetR);
                }

            wetL *= duckGain;
            wetR *= duckGain;

            // ---- final mix -------------------------------------------------
            left[n]  = (dryL * dryGain + wetL * wetGain) * outGain;
            right[n] = (dryR * dryGain + wetR * wetGain) * outGain;
        }
    }

    // --- metering (for the UI scope) ---------------------------------------
    float getDuckGain()  const { return duckGainMeter; }  // 1 = open, <1 = ducked
    float getInputEnv()  const { return inputEnvMeter; }   // linear dry envelope

private:
    static float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    static float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
    static float gainToDb (float g)  { return 20.0f * std::log10 (std::max (1.0e-6f, g)); }

    float msToSamples (float ms) const
    {
        const float s = (float) (ms * 0.001 * sr);
        return std::min ((float) (bufLen - 2), std::max (1.0f, s));
    }

    // Read `delaySamples` behind the write head with linear interpolation.
    float readFrac (const std::vector<float>& buf, float delaySamples) const
    {
        float readPos = (float) wIdx - delaySamples;
        while (readPos < 0.0f) readPos += (float) bufLen;
        const int   i0 = (int) readPos;
        const int   i1 = (i0 + 1) % bufLen;
        const float fr = readPos - (float) i0;
        return buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
    }

    static float onePoleCoeff (float hz, double sr)
    {
        const float x = (float) (1.0 - std::exp (-2.0 * M_PI * (double) hz / sr));
        return std::min (1.0f, std::max (0.0f, x));
    }

    void recomputeCoeffs()
    {
        dampCoeff   = onePoleCoeff (std::max (200.0f,  p.dampHz),   sr);
        lowCutCoeff = onePoleCoeff (std::max (20.0f,   p.lowCutHz), sr);

        // detector: quick attack, moderate release so it tracks the vocal
        detAtkCoeff = onePoleCoeff (msToHz (1.5f),  sr);
        detRelCoeff = onePoleCoeff (msToHz (45.0f), sr);

        // duck envelope uses the user's attack/release
        duckAtkCoeff = onePoleCoeff (msToHz (std::max (0.5f,  p.attackMs)),  sr);
        duckRelCoeff = onePoleCoeff (msToHz (std::max (5.0f,  p.releaseMs)), sr);

        // wet EQ biquads (coeffs only — state is preserved across recompute)
        const auto& cfg = dxeq::bands();
        for (int b = 0; b < dxeq::kNumBands; ++b)
        {
            const auto co = dxeq::computeCoeffs (cfg[(size_t) b].type,
                                                 (double) p.eqFreq[b],
                                                 (double) p.eqGain[b],
                                                 (double) cfg[(size_t) b].q, sr);
            eqL[b].setCoeffs (co);
            eqR[b].setCoeffs (co);
        }
    }

    static float msToHz (float ms) { return 1000.0f / std::max (0.01f, ms); }

    // config
    // 5 s ceiling so a synced whole note fits even at slow tempos
    // (1/1 at 48 BPM = 5000 ms); the free-time knob still tops out at 2 s.
    static constexpr double maxDelayMs = 5000.0;
    static constexpr float  kneeDb     = 6.0f;

    Params p;
    double sr = 44100.0;

    std::vector<float> bufL, bufR;
    int   bufLen = 0;
    int   wIdx   = 0;

    // filter + smoothing state
    float lpL = 0, lpR = 0, hpL = 0, hpR = 0;
    float smDelayL = 1.0f, smDelayR = 1.0f;

    // wet EQ (5 bands per channel)
    dxeq::Biquad eqL[dxeq::kNumBands];
    dxeq::Biquad eqR[dxeq::kNumBands];

    // ducking state
    float detEnv = 0.0f;
    float duckGain = 1.0f;

    // coefficients
    float dampCoeff = 0.5f, lowCutCoeff = 0.02f;
    float detAtkCoeff = 0.3f, detRelCoeff = 0.01f;
    float duckAtkCoeff = 0.2f, duckRelCoeff = 0.01f;

    // meters
    float duckGainMeter = 1.0f;
    float inputEnvMeter = 0.0f;
};
