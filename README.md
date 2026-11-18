# ParametricNAM Capture

A standalone JUCE app for planning and recording amplifier captures, then exporting a dataset for ParametricNAM Training on Colab.

## Make a capture session

1. **Audio setup.** Open **Wiring guide** for the signal routing. Enable two inputs and one or two outputs. Select the amp output and the loopback input. The other input is the microphone. With two outputs, Capture sends the same stimulus and marker on both. Connect the second output to the loopback line input. With one output, use the interface’s internal loopback or split the line-level signal. **Next** applies 48 kHz and checks that the device accepted it before continuing. Connect the amplifier to its usual speaker or rated load and route only line-level signals to the interface.
2. **Capture plan.** Name the amplifier, edit the control table, and choose the training and test take counts. Dial step is the spacing for automatic positions. A minimum of 2 and a step of 0.5 give 2, 2.5, 3, and so on. Optional **Priority takes** count toward the training total. Use a fixed value, a range like `8:10`, or leave a control blank to use its full range. The app fills the remaining training takes automatically and keeps test points distinct. Use the bundled 90-second Night Train training stimulus or import a mono 48 kHz WAV between 3 and 240 seconds. Calibration plays up to six seconds around the loudest peak at the same send level as recording. Each take plays the full stimulus. **Next** asks where to save the session.
3. **Calibrate levels.** Set the amp to the loudest condition you plan to capture and click **Check loud setting**. Adjust the send or interface gains and repeat if needed. Keep the interface levels and physical setup fixed once you begin recording. Then set the amp to the quietest planned condition and click **Check quiet setting** with the interface gains unchanged. The app estimates the microphone signal-to-noise ratio and flags readings below 20 dB. This is an advisory check. New sessions start at 0 dB send trim (unity gain).
4. **Record takes.** Set the knobs to the displayed values and click **Record take**. The app saves a successful take and moves to the first unfinished one. A failed take stays selected for retry. You can select a saved row to retake it. Previous WAVs are preserved. To resume after closing the app, open `session.json` from **Audio setup**, confirm routing and check levels against the original loopback.
5. **Export dataset.** Export is offered once all planned takes are saved. Copy the exported `dataset/` folder to Drive for Colab. Optional **Normalise to -3 dBFS** applies one gain factor to all microphone targets, preserving their relative levels and the input drive levels. The manifest records that gain. Raw captures stay in the session.

The alignment uses a sync marker on the loopback and applies the same shift to both channels. Each take carries its aligned input and microphone WAV. The physical delay from the loopback to the microphone remains in the pair. The updated ParametricNAM VST3 supports one to eight controls per model.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 3
```

CMake uses JUCE from a sibling `JUCE/` checkout or downloads JUCE 8.0.6. Set `PNAM_JUCE_DIR` to use another local checkout. Linux uses ALSA and enables JACK when its development headers are available. The repository includes a build matrix for Linux, Windows and macOS.

On Linux the executable is `build/ParametricNAMCapture_artefacts/Release/ParametricNAM Capture`. For an audio-hardware-free integration check, pass `--self-test` and the absolute path of a new directory. This simulates a capture session and writes a test dataset there.

## GitHub Actions

Each successful build uploads an archive for its operating system and architecture. Pushing a `v*` tag publishes the same archives as GitHub Release assets once all platforms pass. The archives include the standalone application, the standard stimulus, and this guide. On Linux and Windows, keep `CaptureStimulus.wav` alongside the executable. On macOS, the WAV is inside the app bundle. Release binaries are unsigned.

## Updates

The interface checks GitHub for the latest stable release in the background. If that release is newer, the **Update** button opens its download page. The check needs internet access. Installation remains manual.
