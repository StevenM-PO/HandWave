# HandWave

A wavetable synthesizer where you **draw** the waveshapes on a touchscreen.
The target hardware is a Raspberry Pi 3 B+ with an 800x480 DSI touch display that boots straight into the app. Development happens on the desktop first.

**Milestone 1 (current):** draw one cycle of a waveform and hear it as a drone, with pitch and volume control.

## Layout

| Folder | What lives there |
|---|---|
| `core/` | Pure C++ DSP with no Qt: FFT, `Wavetable`, `buildWavetable()` (drawing to band-limited table), `WavetableOscillator`, smoothing, lock-free queue. The LFO and envelope will reuse it later. |
| `audio/` | `AudioEngine`, which handles the output device through [miniaudio](https://miniaud.io) (WASAPI on Windows, ALSA on the Pi) and the real-time thread hand-off. |
| `app/` | Qt Quick UI: `WaveCanvas` (scene-graph drawing surface), `SynthController` (QML-to-engine bridge), `qml/Main.qml`. |
| `tests/` | Catch2 unit tests for `core/` and the engine hand-off. |
| `third_party/` | Vendored miniaudio (public domain / MIT-0). |

### How a drawing becomes sound
1. `WaveCanvas` stores 512 y-values and fills gaps left by fast strokes.
2. `SynthController` merges updates to at most one per frame and calls `hw::buildWavetable()`. That resamples the drawing to 2048 samples, removes DC, builds 11 band-limited mip levels via FFT (one per octave, so high notes don't alias), and normalizes the result.
3. The new table goes to the audio thread through an atomic mailbox. The oscillator crossfades to it over 10 ms, so redrawing while the drone plays doesn't click. Old tables come back through a lock-free queue and are freed on the UI thread, because the audio thread never allocates or frees memory.

## Building on Windows

Requires Qt 6 (MinGW 64-bit kit) from the Qt Online Installer, plus CMake ≥ 3.21. The presets assume Qt 6.12.0 in `C:\Qt`; edit `CMakePresets.json` if yours differs.

```powershell
.\scripts\run-windows.ps1          # configure + build + launch
ctest --preset windows-mingw       # run unit tests (after a build)
```

In VS Code (CMake Tools) or Qt Creator, open the folder and pick the **windows-mingw** preset.

## Raspberry Pi (planned next step)

- Raspberry Pi OS Lite (Bookworm) with `dtoverlay=vc4-kms-v3d`.
- `sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-window qml6-module-qtqml-workerscript libasound2-dev`
- `cmake --preset pi && cmake --build --preset pi`
- Run full-screen without a desktop: `QT_QPA_PLATFORM=eglfs ./build/pi/handwave`
- Kiosk boot: a systemd service that starts the app on boot, the tty1 getty disabled, quiet boot plus splash, and optionally a read-only root (overlayfs).
- Audio: the Pi 3B+ 3.5 mm jack is noisy, so an I2S DAC HAT (PCM5102A / HiFiBerry DAC+) is recommended.

The code sticks to Qt 6.4 APIs because that's what Bookworm's apt provides.

## Roadmap
QWERTY/MIDI notes and polyphony → drawn LFO → drawn envelope with ADSR markers → wave morphing (multi-frame tables, animated canvas) → CV out through an SPI DAC.
