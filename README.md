# ParametricNAM Capture

A standalone JUCE app for planning and recording amplifier captures, then exporting a dataset for ParametricNAM Training on Colab.

## Make a capture session

1. **Plan session.** Name the amplifier, edit the control table, and choose the training and test take counts. Each control has a stable lowercase ID, a display label, a minimum, a maximum and a capture step. Optional model endpoints specify how the dial is mapped into the model. The default mapping is 0–1.
2. **Add priorities.** Use **+ Priority** to add a row. Set **Takes** to the number of training takes to reserve. Each control cell accepts an exact value or a range like `8:10`, and an empty cell uses the full range. For example, set **Takes** to 6 and **Gain** to `8:10` to reserve six points for high gain. Priority counts are part of the training budget. Test points are distinct from training points.
3. **Choose the stimulus.** Start with the built-in 12-second test signal or import a mono 48 kHz WAV between 3 and 240 seconds. The stimulus is copied into the session.
4. **Audio setup.** Use the same interface clock for playback and recording at 48 kHz. Enable one output routed to the amplifier and two inputs for the loopback and the microphone. Set **Loopback input** to the first or second enabled input. The remaining input is the microphone. Connect the amplifier to its usual speaker or load and route only line-level signals to the interface.
5. **Calibrate.** Set the amp to the loudest condition you plan to capture and run calibration. Check the live peak readout. Adjust the send level or interface gains until both inputs have headroom. The app then holds the software send level for the session. Keep the interface levels and physical setup fixed.
6. **Record.** Dial in the values shown, select the row and record. The app advances to the first unfinished take. Select a finished row to retake it. A retake preserves the prior WAVs. To resume after closing the app, open `session.json` and recalibrate against the original loopback.
7. **Export dataset.** Copy the entire exported `dataset/` folder to Drive for Colab. Raw captures stay in the session. Optional **Normalise to -3 dBFS** applies one gain factor to all microphone targets, preserving their relative levels and the input drive levels. The manifest records the gain used.

The alignment uses a sync marker on the loopback and applies the same shift to both channels. Each take carries its aligned input and microphone WAV. The physical delay from the loopback to the microphone remains in the pair. The updated ParametricNAM VST3 supports one to eight controls per model.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 3
```

CMake uses JUCE from a sibling `JUCE/` checkout or downloads JUCE 8.0.6. Set `PNAM_JUCE_DIR` to use another local checkout. Linux uses ALSA and enables JACK when its development headers are available. The repository includes a build matrix for Linux, Windows and macOS.

On Linux the executable is `build/ParametricNAMCapture_artefacts/Release/ParametricNAM Capture`. For an audio-hardware-free integration check, pass `--self-test` and the absolute path of a new directory. This simulates a capture session and writes a test dataset there.

## GitHub Actions

Each successful build uploads an archive for its operating system and architecture. Pushing a `v*` tag publishes the same archives as GitHub Release assets once all platforms pass. The archives include the standalone application and this guide. Release binaries are unsigned.
