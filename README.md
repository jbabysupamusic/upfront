# UPFRONT — Harmonic EQ

UPFRONT listens to a sound, finds its fundamental note, puts EQ bands on that note
and its harmonics from 20 Hz to 20 kHz, and with one click balances them so the
sound sits and translates well in a mix — smooth, clear and never harsh.

New here? Read **START HERE.md** first (download + install in 3 steps).

---

## Controls

| Control | What it does |
|---|---|
| **ANALYZE** | Click, then play your sound. It waits for audio, listens, then finds the fundamental and places the bands. Click again any time to stop early. |
| **LISTEN** | How long ANALYZE listens: drag the bar or **double-click and type** 1–30 seconds. |
| **∞** | Listen until you click ANALYZE again (great for a whole loop or song section). |
| **BALANCE** | Sets every band's gain, the low cut and the output level automatically. |
| **DYNAMIC** | Bands react to the music: cuts dig in when a harmonic jumps out and relax when it's quiet; boosts fill in dips. The white line shows what's happening live. |
| **ANALOG** | Warm, smooth saturation with mostly even harmonics. Runs 8× oversampled, so there's no aliasing. |
| **AMOUNT** | How strong BALANCE is (click BALANCE again after changing). |
| **TILT** | The tone BALANCE aims for. −4.5 is a classic balanced mix; −6 is warmer, −3 is brighter. |
| **BANDS** | How many harmonic bands are used (up to 16). |
| **LOW CUT** | Clean 24 dB/oct high-pass, auto-set just under the fundamental. |
| **DRIVE** | Amount of analog warmth (when ANALOG is on). |
| **OUTPUT** | Level trim. BALANCE sets it so bypass comparisons are fair. |

**On the display:** drag a node up/down for gain, use the **mouse wheel** on a node
for width (Q), and **double-click** a node to reset it. The white node is the
fundamental (F0), and the numbers along the top are harmonic numbers. The blue fill is
the live output, and the teal line is the analysed sound.

Your analysis is saved with your FL Studio project.

## Sound-quality safeguards
- **No harshness:** boosts in the 2–6 kHz zone are capped at +1.5 dB, and cuts
  there are slightly stronger. "Air" above 9 kHz uses soft, wide boosts only.
- **Smooth:** boosts are always wide; only cuts are narrow. Tiny moves under 0.3 dB
  are skipped, and noise is never boosted.
- **No clicks or zipper noise:** every gain change glides over 30 ms.
- **No aliasing:** the EQ is linear (it can't alias), and the analog stage is
  oversampled 8×. In testing, aliasing stayed 94 dB below the signal even with a
  full-volume 15 kHz tone at maximum drive.

## CPU
Measured at 48 kHz stereo: about 0.5% of one core with 16 active bands, under 1% in
Dynamic mode, and about 2% with Dynamic + Analog. Bands left at 0 dB switch
themselves off, so it uses almost nothing at idle.

## Files in this folder
| File | Purpose |
|---|---|
| `START HERE.md` | 3-step download and install guide |
| `Build and Install (Windows).bat` | Build and install on your own PC (needs Visual Studio 2022) |
| `Source/PluginProcessor.*` | The audio engine: EQ, Dynamic, Analog, BALANCE |
| `Source/Analyzer.*` | ANALYZE: listening and finding the fundamental |
| `Source/PluginEditor.*` | The interface |
| `Source/DSP.h` | Small building blocks (filters, saturation) |
| `installer/` | Installer recipe and a manual `Install.bat` |
| `.github/workflows/` | Makes GitHub build the installer for you |
| `CMakeLists.txt` | Build settings |

Built with JUCE 8, which downloads automatically during the build. JUCE is free under
its personal licence up to a revenue limit; check juce.com/legal before selling UPFRONT.
