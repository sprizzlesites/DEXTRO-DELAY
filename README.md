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
black void — re-tuned to a **purple + deep-blue** palette, with a live scope
that shows the vocal envelope (blue) against the echo level / duck history
(purple).

## Controls

**Delay / Echo (blue)**
| Knob | What it does |
|---|---|
| **TIME** | Left delay time, 5 ms – 2 s |
| **R OFFSET** | Adds to the right channel's time for stereo spread |
| **FEEDBACK** | Regeneration, up to 98 % |
| **TONE** | Hi-cut in the feedback path — repeats darken |
| **LOW CUT** | Lo-cut in the feedback path — repeats thin out |
| **WIDTH** | Stereo width of the wet signal |
| **PING-PONG** | Cross-couples the feedback L↔R so echoes bounce |

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
