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

## Raspberry Pi

Target: Raspberry Pi 3 A+ (512 MB) running **Raspberry Pi OS Lite 64-bit (trixie, Qt 6.8)**, with an
800x480 DSI touch panel. The panel is auto-detected (`display_auto_detect=1`) and touch is an FT5x06.

One-time setup on a fresh image (SSH in as `admin`):

```sh
sudo apt install --no-install-recommends cmake g++ make git pkg-config libasound2-dev   qt6-base-dev qt6-declarative-dev qt6-qpa-plugins qml6-module-qtquick qml6-module-qtquick-controls   qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtquick-window   qml6-module-qtqml-workerscript libgl-dev libegl-dev libgles-dev
```

Then copy the source to `~/HandWave` and run:

```sh
./deploy/install-pi.sh --kiosk     # build (-j1, ~4 min), install to /opt/handwave, boot into the app
./deploy/install-pi.sh --no-kiosk  # undo: normal console login again
journalctl -u handwave -f          # app logs
```

- **Kiosk boot:** [deploy/handwave.service](deploy/handwave.service) runs the app with Qt's `eglfs` backend
  (OpenGL ES straight to the display, no desktop) on tty1, and restarts it if it crashes. The install script
  also disables the tty1 login, quiets the kernel console, and disables cloud-init (Imager's first-boot tool).
  The app is on screen about 12 s after the kernel starts.
- **Display config:** [deploy/kms.json](deploy/kms.json) selects the DSI output and turns HDMI off.
- **Audio:** the 3.5 mm jack (ALSA card 0) works but is noisy; an I2S DAC HAT (PCM5102A / HiFiBerry DAC+) is
  the planned upgrade.

The code still avoids APIs newer than Qt 6.4, so older images keep working.

## Roadmap
QWERTY/MIDI notes and polyphony → drawn LFO → drawn envelope with ADSR markers → wave morphing (multi-frame tables, animated canvas) → CV out through an SPI DAC.
