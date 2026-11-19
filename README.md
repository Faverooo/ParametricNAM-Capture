# ParametricNAM Capture

Record how a real guitar amp sounds at different knob settings. Capture guides you through the connections, records each take, and exports a dataset for training. It runs on its own.

## Get started

1. Download Capture for your system from [Releases](https://github.com/Faverooo/ParametricNAM-Capture/releases/latest). Extract the package and open the app. On Windows and Linux, keep `CaptureStimulus.wav` next to the app.
2. Follow **Wiring guide**. You need two inputs, one for the microphone and one for the loopback. The loopback is a clean copy of the signal sent to the amp. Use a second output to send that copy, or use your interface’s internal loopback. In **Audio**, choose the channels. Capture checks for **48 kHz**.
3. In **Take plan**, name your amp, enter the knobs you want to model and choose how many takes to record. Add priority settings if there are positions you particularly care about.
4. Check the loudest and quietest amp settings in **Levels**. Keep the interface gains fixed once you start recording. Follow the knob settings on screen for each take.
5. Select **Export dataset** and copy the entire `dataset` folder to Drive. Open it with [ParametricNAM Training](https://github.com/Faverooo/ParametricNAM-Training).

Connect the amplifier to its speaker or rated load. Never plug a speaker output into your audio interface.

## License

The project code is [MIT licensed](LICENSE). [JUCE](https://juce.com/legal/) has its own license terms. Audio and models are outside the scope of the code license.
