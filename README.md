# Hand Gesture Control — Real-Time Vision Subsystem (Automatic Control L3)

Real-time hand-gesture recognition via PC webcam using **C++17 + OpenCV 4**, producing
discrete control commands (`STOP`, `FORWARD`, `BACKWARD`, `TURN_LEFT`, `TURN_RIGHT`,
`STANDBY`) ready to be forwarded to an Arduino/ESP32 over Serial/UART.

## Gesture Mapping

| Hand in ROI            | Fingers | Command       |
|------------------------|---------|---------------|
| Fist                   | 0       | `STOP`        |
| Index finger           | 1       | `FORWARD`     |
| Index + middle         | 2       | `BACKWARD`    |
| Three fingers          | 3       | `TURN_LEFT`   |
| Four fingers           | 4       | `TURN_RIGHT`  |
| Open palm              | 5       | `STANDBY`     |
| No hand in ROI         | —       | `NO_HAND`     |

Commands are debounced by a **majority vote over the last 10 frames**; the stable command
is only re-sent to the hardware placeholder when it changes. Ties keep the previous
command (hysteresis).

## Project Structure

```
hand-gesture-control/
├── CMakeLists.txt          # C++17, links OpenCV 4, auto-copies runtime DLLs (Windows)
├── CMakePresets.json       # Pre-configured for the prebuilt OpenCV on this machine
├── include/
│   ├── CameraStream.hpp    # cv::VideoCapture wrapper: open, grab, mirror-flip, release
│   ├── HandProcessor.hpp   # ROI, HSV mask, morphology, hull + convexity-defect finger counting
│   └── ControlLogic.hpp    # Finger count -> command mapping + 10-frame majority vote
└── src/
    ├── CameraStream.cpp
    ├── HandProcessor.cpp
    ├── ControlLogic.cpp
    └── main.cpp            # Real-time loop, GUI windows, FPS + command overlay
```

## Build (Windows — this machine is pre-configured)

OpenCV 4.11.0 prebuilt binaries were downloaded and extracted to `F:\opencv\opencv\build`.
CMake and the MSVC toolchain come from the installed Visual Studio.

```powershell
cd "F:\open code\hand-gesture-control"
cmake -B build -DOpenCV_DIR=F:/opencv/opencv/build
cmake --build build --config Release
```

Or simply:

```powershell
cmake --preset windows-default
cmake --build --preset windows-release
```

The executable is created at `build\Release\hand_gesture_control.exe`, and all required
OpenCV DLLs (`opencv_world4110.dll`, `opencv_videoio_ffmpeg4110_64.dll`, …) are copied
next to it automatically at build time — no PATH changes needed.

Note: the prebuilt OpenCV package's config script does not yet recognize the newest MSVC
toolset (VS 2026); `CMakeLists.txt` detects this and imports `opencv_world` directly as a
fallback, so no patching of OpenCV files was required.

### Linux / macOS

```bash
sudo apt-get install -y build-essential cmake pkg-config libopencv-dev   # Ubuntu/Debian
# or: brew install cmake opencv                                          # macOS
cmake -B build && cmake --build build
```

## Run

```powershell
.\build\Release\hand_gesture_control.exe          # camera 0
.\build\Release\hand_gesture_control.exe 1        # camera index 1
```

Press **q** or **ESC** to quit cleanly.

Windows opens three windows:

- **Hand Gesture Control** — full frame, green ROI box, overlay with the stable command,
  finger count, and FPS.
- **HSV Mask** — the binary skin mask inside the ROI (white = detected skin). Your hand
  should appear as a solid white blob; the background as black.
- **Hand Processing** — the ROI with the hand contour (green), convex hull (cyan),
  finger-valley points (red), and hull-defect rays (orange).

## HSV Calibration (do this first, per lighting condition)

1. Place your open hand fully inside the green ROI box with typical room lighting.
2. In the **HSV Calibration** window, adjust the six trackbars until your hand is a clean,
   solid white blob in the **HSV Mask** window and the background is black:
   - **H_min / H_max** — hue range of your skin (defaults `0–20`; OpenCV hue is 0–179).
   - **S_min / S_max** — saturation range (defaults `30–150`; raise S_min if the white
     wall behind you appears in the mask, lower S_max if your mask has black holes).
   - **V_min / V_max** — brightness range (defaults `60–255`; raise V_min in bright
     sunlight, lower it in dim rooms).
3. The mask should show exactly one blob for your hand. If it flickers, adjust S_min and
   V_min slightly upward.
4. Keep your hand roughly vertical (fingers pointing up) — the finger counter uses the
   convexity defects between extended fingers plus the cosine rule on the defect angle
   (< 90° = a valley between fingers).

## Hardware Integration (Serial/UART)

`ControlLogic::sendToHardware(const std::string& command)` in
`src/ControlLogic.cpp` is the single integration point. It currently logs to the console;
replace its body with a serial write, e.g. open `\\.\COM3` via `CreateFile()` (Windows) or
`/dev/ttyUSB0` via `write()` (Linux) and transmit the command string + `'\n'`. The main
loop calls it only when the debounced command changes, so the link stays quiet during
stable operation.

## Troubleshooting

- **"could not open camera N"** — wrong index (try 0, 1, 2), camera used by another app,
  or Windows privacy: *Settings → Bluetooth & devices → Cameras → allow desktop apps*.
- **Mask is all black** — widen the HSV ranges (start with V_min down to 30).
- **Mask includes background** — raise S_min (e.g. 50–80) and H range tightening.
- **Finger count unstable** — recalibrate the mask; keep the hand inside the ROI and
  vertical; the 10-frame vote already filters short flickers.
