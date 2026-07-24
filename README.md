# DEXTRO DELAY

A **self-ducking stereo delay** VST3 / Standalone plugin, built on JUCE.
Self-sidechained delay, inspired by DXM.

DEXTRO DELAY uses the dry input as its own sidechain: an envelope follower
watches the incoming vocal and **ducks the wet echoes while the vocal is
present**, then lets them **swell back up in the gaps between phrases**. The
delay and feedback network keeps running underneath the whole time, so the tails
are always there — ducking only controls how much of them you hear. The result
is a big, lush delay that never fights the lead vocal for space.

Styled with the neon/chrome hardware look — a metallic-black faceplate on a
black void — in a **purple + neon-green** palette. The controls are grouped
into two clearly separated panels — **DELAY / ECHO** (green) and
**SELF-DUCK / DYNAMICS** (purple) — above a "wave box" screen that toggles
between a live scope and an interactive EQ.

An **INPUT PAN** slider sits in the top bar. It pans the input **before** the
delay chain (constant-power balance), so ping-pong has real L/R asymmetry to
bounce even on centred/mono material instead of just echoing down the middle.

## Wave box

A button in the wave box's bottom-left corner switches the screen between:
- **SCOPE** — the dry-vocal envelope (blue) against the echo level / duck
  history (purple), with the live gain-reduction and host BPM readouts.
- **EQ** — an interactive **5-point EQ** on the *wet delay signal*: a low
  shelf, three bells, and a high shelf, each a draggable node:
    - **drag** left/right for frequency, up/down for ±18 dB gain,
    - **scroll** over a node to adjust its **Q** (0.3 wide → 6.0 surgical),
    - **double-click** to reset its gain.
  The purple curve is the real biquad response, and the active band's
  frequency / gain / Q read out at the top. Behind the curve, a **reactive
  cube-pixel spectrum** of the wet signal (green at the bottom → blue → purple
  at the top) shows how the EQ is shaping the echoes in real time. The EQ is
  bypassed when the toggle is off.

## Controls

**Delay / Echo (blue)**
| Knob | What it does |
|---|---|
| **SYNC** | Tempo-sync toggle (**on by default**). When on, the Time knob locks to note values from the host tempo; turn it off for free millisecond timing |
| **TIME** | With SYNC on, steps through note divisions — **1/1, 1/2, 1/4, 1/8, 1/16, 1/32**. With SYNC off, a free delay time of 5 ms – 2 s |
| **L/R OFFSET** | Bipolar, centred at 0: turn left to delay the **left** channel more, right to delay the **right** channel more (± 250 ms) |
| **FEEDBACK** | Regeneration, up to 98 % |
| **TONE** | Hi-cut in the feedback path — repeats darken |
| **LOW CUT** | Lo-cut in the feedback path — repeats thin out |
| **WIDTH** | Stereo width of the wet signal |
| **PING-PONG** | Cross-couples the feedback L↔R so echoes bounce |

The delay reads the host tempo (via the play-head), so when synced the echoes
track the session BPM — shown on the scope. The scope shows `120 BPM` as a
fallback when the host reports no tempo (e.g. the Standalone app).

**Self-Duck / Dynamics + Output (purple)**
| Knob | What it does |
|---|---|
| **DUCK** | Depth of the gain reduction applied to the echoes (0–36 dB) |
| **THRESHOLD** | Vocal level above which ducking engages |
| **ATTACK** | How fast the echoes duck when the vocal enters |
| **RELEASE** | How fast the echoes rise back during silence |
| **MIX** | Dry / wet blend |
| **OUTPUT** | Output trim |

## Building

A fresh clone builds with only **CMake + a C++17 compiler** — JUCE is fetched
and statically linked, and the logo is embedded, so the built binary depends
only on system libraries.

### Native (Linux / macOS / Windows host)
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```
Artefacts land in `build/DextroDelay_artefacts/Release/` (VST3 + Standalone).
On Linux, install the usual JUCE dev packages (ALSA, X11, freetype, fontconfig).

### Windows VST3 (cross-compiled from Linux, no CI minutes)
```sh
scripts/build-windows.sh DextroDelay
```
Cross-compiles with MinGW (JUCE 7 pin), **verifies the VST3 actually
instantiates under Wine**, and packages it to
`dist/DextroDelay-VST3-Windows-x64.zip`. Install by extracting `DEXTRO
DELAY.vst3` into `C:\Program Files\Common Files\VST3\`.

### macOS VST3 (CI)
Run the **macOS VST3** GitHub Actions workflow (`.github/workflows/macos-vst3.yml`).
It builds a universal (arm64 + x86_64) VST3, validates it with pluginval, and
uploads it as an artifact. On first use, clear Gatekeeper quarantine once:
```sh
xattr -dr com.apple.quarantine "DEXTRO DELAY.vst3"
```

## DSP test

The self-ducking engine (`Source/DSP/DuckingDelay.h`) is dependency-free, so it
can be verified offline against a real vocal WAV:
```sh
cmake -B build -DDEXTRODELAY_BUILD_TESTS=ON
cmake --build build --target DextroDelayTest
./build/DextroDelayTest vocal.wav out/
```
It renders `dextro_ducked.wav` (ducking on) and `dextro_noduck.wav`
(reference), and asserts that the echoes duck under the vocal and rise back in
the silences.
