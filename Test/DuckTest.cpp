// =============================================================================
//  DEXTRO DELAY — offline DSP test / demo renderer
// -----------------------------------------------------------------------------
//  Feeds a real vocal WAV through the DuckingDelay engine and verifies the
//  self-ducking behaviour numerically:
//    * while the vocal is present the wet echoes are pulled DOWN, and
//    * in the gaps between phrases the echoes RISE back toward full.
//  It also renders an output WAV (ducking on) plus a "no-duck" reference so the
//  chain can be A/B'd by ear.
//
//  Pure C++ / standard library (plus a tiny PCM-16 WAV reader/writer) so it
//  builds with a plain compiler and needs no host or JUCE.
//
//  Usage:  DuckTest <input.wav> [outDir]
// =============================================================================

#include "../Source/DSP/DuckingDelay.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

// -------------------------------------------------------------- minimal WAV IO
struct Wav
{
    int sampleRate = 44100;
    int channels   = 2;
    std::vector<float> L, R;   // deinterleaved, [-1,1]
};

static uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

static bool loadWav (const std::string& path, Wav& w)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) { std::fprintf (stderr, "cannot open %s\n", path.c_str()); return false; }
    std::fseek (f, 0, SEEK_END); long sz = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    std::vector<uint8_t> b ((size_t) sz);
    if (std::fread (b.data(), 1, (size_t) sz, f) != (size_t) sz) { std::fclose (f); return false; }
    std::fclose (f);

    if (sz < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
        return false;

    int   bits = 16, fmt = 1, chans = 2, rate = 44100;
    const uint8_t* data = nullptr; uint32_t dataLen = 0;

    size_t pos = 12;
    while (pos + 8 <= (size_t) sz)
    {
        const char* id = (const char*) (b.data() + pos);
        uint32_t len = rd32 (b.data() + pos + 4);
        const uint8_t* body = b.data() + pos + 8;
        if (std::memcmp (id, "fmt ", 4) == 0 && len >= 16)
        {
            fmt   = rd16 (body);
            chans = rd16 (body + 2);
            rate  = (int) rd32 (body + 4);
            bits  = rd16 (body + 14);
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            data = body; dataLen = len;
        }
        pos += 8 + len + (len & 1);   // chunks are word-aligned
    }
    if (! data || chans < 1) return false;

    w.sampleRate = rate;
    w.channels   = chans;
    const int bytesPerSample = bits / 8;
    const int frameBytes     = bytesPerSample * chans;
    const uint32_t frames     = dataLen / (uint32_t) frameBytes;
    w.L.resize (frames); w.R.resize (frames);

    for (uint32_t i = 0; i < frames; ++i)
    {
        const uint8_t* fr = data + (size_t) i * frameBytes;
        auto sample = [&] (int ch) -> float
        {
            const uint8_t* s = fr + ch * bytesPerSample;
            if (fmt == 3 && bits == 32) { float v; std::memcpy (&v, s, 4); return v; }         // float
            if (bits == 16) { int16_t v = (int16_t) rd16 (s); return (float) v / 32768.0f; }   // pcm16
            if (bits == 24) { int32_t v = (s[0] | (s[1] << 8) | (s[2] << 16)); if (v & 0x800000) v |= ~0xffffff; return (float) v / 8388608.0f; }
            if (bits == 32) { int32_t v = (int32_t) rd32 (s); return (float) v / 2147483648.0f; }
            return 0.0f;
        };
        w.L[i] = sample (0);
        w.R[i] = sample (chans > 1 ? 1 : 0);
    }
    return true;
}

static bool saveWav (const std::string& path, int sr, const std::vector<float>& L, const std::vector<float>& R)
{
    const uint32_t frames = (uint32_t) L.size();
    const uint16_t chans = 2, bits = 16;
    const uint32_t byteRate = (uint32_t) sr * chans * (bits / 8);
    const uint16_t blockAlign = chans * (bits / 8);
    const uint32_t dataLen = frames * blockAlign;

    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    auto w32 = [&] (uint32_t v) { uint8_t p[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; std::fwrite (p, 1, 4, f); };
    auto w16 = [&] (uint16_t v) { uint8_t p[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; std::fwrite (p, 1, 2, f); };

    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataLen); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (1); w16 (chans); w32 ((uint32_t) sr); w32 (byteRate); w16 (blockAlign); w16 (bits);
    std::fwrite ("data", 1, 4, f); w32 (dataLen);
    for (uint32_t i = 0; i < frames; ++i)
    {
        auto clamp16 = [] (float x) { x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); return (int16_t) std::lround (x * 32767.0f); };
        int16_t l = clamp16 (L[i]);
        int16_t r = clamp16 (R[i]);
        w16 ((uint16_t) l); w16 ((uint16_t) r);
    }
    std::fclose (f);
    return true;
}

// ------------------------------------------------------------------------ main
int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: DuckTest <input.wav> [outDir]\n"); return 2; }
    const std::string inPath = argv[1];
    const std::string outDir = argc > 2 ? argv[2] : ".";

    Wav in;
    if (! loadWav (inPath, in)) { std::fprintf (stderr, "failed to load %s\n", inPath.c_str()); return 1; }
    std::printf ("Loaded %s: %d Hz, %d ch, %.2f s\n",
                 inPath.c_str(), in.sampleRate, in.channels, (double) in.L.size() / in.sampleRate);

    const int    n  = (int) in.L.size();
    const double sr = in.sampleRate;

    // A musical self-ducking preset for the test.
    DuckingDelay::Params p;
    p.delayMsL = 340.0f; p.delayMsR = 500.0f;   // slight offset for stereo spread
    p.feedback = 0.55f;
    p.dampHz   = 5500.0f;
    p.lowCutHz = 140.0f;
    p.mix      = 0.5f;
    p.width    = 1.0f;
    p.pingpong = true;
    p.duckDepthDb = 24.0f;
    p.thresholdDb = -34.0f;
    p.attackMs    = 8.0f;
    p.releaseMs   = 320.0f;
    p.outputGainDb = 0.0f;

    // ---- pass 1: ducking ON, capture per-block meters + render ----
    DuckingDelay eng; eng.prepare (sr, 512); eng.setParams (p);

    std::vector<float> outL (n), outR (n);
    std::vector<float> envTrace, duckTrace;   // one entry per block
    const int block = 64;

    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        for (int k = 0; k < m; ++k) { outL[i + k] = in.L[i + k]; outR[i + k] = in.R[i + k]; }
        eng.process (&outL[i], &outR[i], m);
        envTrace.push_back (eng.getInputEnv());
        duckTrace.push_back (eng.getDuckGain());
    }
    saveWav (outDir + "/dextro_ducked.wav", in.sampleRate, outL, outR);

    // ---- pass 2: ducking OFF (reference) ----
    DuckingDelay::Params pNo = p; pNo.duckDepthDb = 0.0f;
    DuckingDelay eng2; eng2.prepare (sr, 512); eng2.setParams (pNo);
    std::vector<float> refL (n), refR (n);
    for (int i = 0; i < n; ++i) { refL[i] = in.L[i]; refR[i] = in.R[i]; }
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        eng2.process (&refL[i], &refR[i], m);
    }
    saveWav (outDir + "/dextro_noduck.wav", in.sampleRate, refL, refR);

    // ---- analysis: classify blocks as vocal-loud vs vocal-quiet ----
    // Threshold used by the ducker is -34 dBFS; classify well inside each side.
    const float loudLin  = std::pow (10.0f, -28.0f / 20.0f);   // clearly above threshold
    const float quietLin = std::pow (10.0f, -50.0f / 20.0f);   // clearly silent

    double sumLoud = 0, sumQuiet = 0; long cLoud = 0, cQuiet = 0;
    float minDuckLoud = 1.0f, maxDuckQuiet = 0.0f;
    for (size_t i = 0; i < envTrace.size(); ++i)
    {
        if (envTrace[i] >= loudLin)  { sumLoud  += duckTrace[i]; ++cLoud;  minDuckLoud  = std::min (minDuckLoud,  duckTrace[i]); }
        if (envTrace[i] <= quietLin) { sumQuiet += duckTrace[i]; ++cQuiet; maxDuckQuiet = std::max (maxDuckQuiet, duckTrace[i]); }
    }

    if (cLoud == 0 || cQuiet == 0)
    {
        std::fprintf (stderr, "test inconclusive: not enough loud (%ld) / quiet (%ld) blocks\n", cLoud, cQuiet);
        return 1;
    }

    const double avgDuckLoud  = sumLoud  / (double) cLoud;
    const double avgDuckQuiet = sumQuiet / (double) cQuiet;

    std::printf ("\n--- self-ducking analysis ---\n");
    std::printf ("blocks: loud=%ld quiet=%ld\n", cLoud, cQuiet);
    std::printf ("avg duck gain  while vocal LOUD : %.3f  (%.1f dB)\n", avgDuckLoud,  20.0 * std::log10 (std::max (1e-6, avgDuckLoud)));
    std::printf ("avg duck gain  while vocal QUIET: %.3f  (%.1f dB)\n", avgDuckQuiet, 20.0 * std::log10 (std::max (1e-6, avgDuckQuiet)));
    std::printf ("deepest duck during loud passages: %.3f\n", minDuckLoud);

    // ---- assertions ----
    bool ok = true;
    // 1) echoes clearly ducked while the vocal is loud
    if (! (avgDuckLoud < 0.85)) { std::fprintf (stderr, "FAIL: echoes not ducked enough while vocal present\n"); ok = false; }
    // 2) echoes essentially open in the gaps
    if (! (avgDuckQuiet > 0.90)) { std::fprintf (stderr, "FAIL: echoes did not rise back up during silence\n"); ok = false; }
    // 3) a real, meaningful difference between the two states
    if (! (avgDuckQuiet - avgDuckLoud > 0.15)) { std::fprintf (stderr, "FAIL: ducking difference too small\n"); ok = false; }

    // 4) output is finite and non-trivial
    double peak = 0; for (int i = 0; i < n; ++i) { peak = std::max (peak, (double) std::fabs (outL[i])); peak = std::max (peak, (double) std::fabs (outR[i])); if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i])) { ok = false; } }
    if (! (peak > 0.01)) { std::fprintf (stderr, "FAIL: output silent/degenerate (peak %.4f)\n", peak); ok = false; }
    std::printf ("output peak: %.3f\n", peak);

    // ---- wet EQ: transparency (flat) + effect (boost) + stability ----
    {
        const int M = std::min (n, (int) (sr * 20.0));   // first ~20 s is plenty
        auto runEq = [&] (bool eqOn, int band, float gainDb, double& outPeak, double& outRms)
        {
            DuckingDelay::Params ep = p;
            ep.eqOn = eqOn;
            for (int b = 0; b < dxeq::kNumBands; ++b) ep.eqGain[b] = 0.0f;
            if (band >= 0) ep.eqGain[band] = gainDb;
            DuckingDelay e; e.prepare (sr, 512); e.setParams (ep);
            std::vector<float> l (M), r (M);
            for (int i = 0; i < M; ++i) { l[i] = in.L[i]; r[i] = in.R[i]; }
            for (int i = 0; i < M; i += block) e.process (&l[i], &r[i], std::min (block, M - i));
            double pk = 0, e2 = 0;
            for (int i = 0; i < M; ++i) { pk = std::max (pk, (double) std::fabs (l[i])); e2 += (double) l[i] * l[i]; if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) pk = 1e9; }
            outPeak = pk; outRms = std::sqrt (e2 / std::max (1, M));
        };

        double offPk, offRms, flatPk, flatRms, boostPk, boostRms;
        runEq (false, -1, 0.0f,  offPk,  offRms);
        runEq (true,  -1, 0.0f,  flatPk, flatRms);
        runEq (true,   2, 12.0f, boostPk, boostRms);   // band 2 = 1 kHz bell (vocal energy)

        std::printf ("\n--- wet EQ ---\n");
        std::printf ("eq off         : peak %.3f rms %.4f\n", offPk,  offRms);
        std::printf ("eq flat (0 dB) : peak %.3f rms %.4f\n", flatPk, flatRms);
        std::printf ("eq +12 @ 1 kHz : peak %.3f rms %.4f\n", boostPk, boostRms);

        if (! (std::fabs (flatRms - offRms) < 0.02 * std::max (1e-6, offRms) + 1e-5))
            { std::fprintf (stderr, "FAIL: flat EQ is not transparent\n"); ok = false; }
        if (! (boostRms > flatRms * 1.02))
            { std::fprintf (stderr, "FAIL: low-shelf boost did not add low-end energy\n"); ok = false; }
        if (! (std::isfinite (boostPk) && boostPk < 4.0))
            { std::fprintf (stderr, "FAIL: EQ produced non-finite / runaway output\n"); ok = false; }
    }

    // ---- input pan must NOT affect the dry pass-through ----
    // At mix = 0 (fully dry) the output must be identical regardless of pan,
    // because pan is applied only to the signal fed into the delay.
    {
        const int M = std::min (n, (int) (sr * 10.0));
        auto runPan = [&] (float pl, float pr, std::vector<float>& L)
        {
            DuckingDelay::Params pp = p;
            pp.mix = 0.0f; pp.inPanL = pl; pp.inPanR = pr;
            DuckingDelay e; e.prepare (sr, 512); e.setParams (pp);
            std::vector<float> r (M); L.assign (M, 0.0f);
            for (int i = 0; i < M; ++i) { L[i] = in.L[i]; r[i] = in.R[i]; }
            for (int i = 0; i < M; i += block) e.process (&L[i], &r[i], std::min (block, M - i));
        };
        std::vector<float> centre, hardLeft;
        runPan (1.0f, 1.0f, centre);       // centred
        runPan (1.4142f, 0.0f, hardLeft);  // hard left
        double maxDiff = 0.0;
        for (int i = 0; i < M; ++i) maxDiff = std::max (maxDiff, (double) std::fabs (centre[i] - hardLeft[i]));
        std::printf ("\n--- pan vs dry ---\ndry-passthrough diff (centre vs hard-left, mix=0): %.2e\n", maxDiff);
        if (! (maxDiff < 1e-6)) { std::fprintf (stderr, "FAIL: input pan altered the dry pass-through\n"); ok = false; }
    }

    std::printf ("\n%s\n", ok ? "PASS: self-ducking delay + wet EQ + dry-safe pan verified."
                              : "TEST FAILED");
    return ok ? 0 : 1;
}
