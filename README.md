# AI-Powered Sign Language Translator & Automatic Control Telemetry System

Real-time **hand-gesture / sign-language translator** for deaf-mute communication built with
**C++17 + OpenCV 4** on Windows, featuring:

- **Pre-built sign language database** (`data/sign_language_db.json`, 13 gestures) + teach-your-own custom signs (`T`)
- **Smart gesture differentiation** from a rich geometric feature vector: extended finger count
  (convex hull + cosine rule on convexity defects), hand orientation angle (`cv::fitEllipse`),
  solidity, and aspect ratio — distinguishes fist, C-shape, V, wide-L, palm tilts, thumb-side, etc.
- **Hold-to-confirm stability progress bar** (15-frame dwell) rendered under the ROI box
- **Native Windows Text-to-Speech** (SAPI `ISpVoice`, async) speaks every confirmed phrase — `S` to mute
- **Nvidia NIM AI auto-tuner** (`A`) — sends scene telemetry (palm/background HSV stats, ambient
  luminance, contrast, mask noise, base64 ROI snapshot) to `https://integrate.api.nvidia.com/v1/chat/completions`
  and applies the returned optimal HSV bounds, morphological filter strength, and defect-sensitivity
  thresholds; works fully offline via local fallback when no API key/internet
- **AI sentence refiner** (`G`) — turns the raw signed word sequence into one natural sentence
  (displayed, spoken, logged), with offline fallback
- **Automatic session transcript** — `session_transcript.txt` (timestamped log + final summary paragraph)
- **Telemetry CSV logger** (`L`) — `telemetry_log.csv` for the Automatic Control subsystem
- **In-app GitHub updater** (`U` or clickable button) — `git fetch`/`pull origin main` from inside the HUD
- **Automatic Control overlay** — finger-count commands (`STOP/FORWARD/BACKWARD/TURN_LEFT/TURN_RIGHT/STANDBY`)
  via majority vote, with a `sendToHardware()` placeholder ready for Serial/UART (Arduino/ESP32)

## Gesture Dictionary (pre-built)

| Hand shape                     | Fingers | Phrase              |
|--------------------------------|---------|---------------------|
| Compact fist                   | 0       | Stop / No           |
| C-shaped curved hand           | 0       | Food / Hungry       |
| Index up (vertical)            | 1       | I need help         |
| Thumb / index sideways         | 1       | Yes / I agree       |
| Wide L (thumb + index)         | 2       | Water, please       |
| Narrow V (vertical)            | 2       | Peace / Hello       |
| V sideways                     | 2       | Victory / Goodbye   |
| Three fingers up               | 3       | Thank you           |
| Three fingers sideways         | 3       | Where / Question    |
| Four fingers up                | 4       | Excuse me           |
| Four fingers sideways          | 4       | Please / Sorry      |
| Open palm vertical             | 5       | Wait please         |
| Open palm tilted/sideways      | 5       | Good / Fine         |

A gesture must be held stable for **15 consecutive frames** (progress bar reaches 100%) before it is
confirmed, spoken, and appended to the sentence buffer. The same phrase will not repeat until a
different gesture (or a no-hand gap) is made.

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `A` (or click `[ AI TUNE (A) ]`) | Nvidia AI environment & sensitivity auto-tune (async, FPS never drops) |
| `C` | Fast local HSV calibration from the 40x40 palm sample box vs. room background |
| `G` | Send signed words to Nvidia NIM for a refined natural sentence (offline fallback included) |
| `L` | Toggle telemetry CSV logging (`[ REC CSV ]` badge) |
| `U` (or click `[ UPDATE GITHUB (U) ]`) | In-app self-updater: `git fetch origin main` + `git pull origin main` |
| `T` | Teach: capture the current stable hand shape as a new custom sign (console prompt) |
| `S` | Mute / unmute Windows TTS |
| `N` | New sentence: clear the sentence buffer |
| `Q` / `ESC` | Quit (writes the transcript summary) |

## Build (Windows)

OpenCV 4.11.0 prebuilt binaries live at `F:\opencv\opencv\build`; CMake/MSVC come from the
installed Visual Studio.

```powershell
cd "F:\open code\hand-gesture-control"
cmake -B build -DOpenCV_DIR=F:/opencv/opencv/build
cmake --build build --config Release
```

Run: `.\build\Release\hand_gesture_control.exe [camera_index]`
All required OpenCV DLLs are copied next to the exe automatically at build time.

### Linux / macOS

```bash
sudo apt-get install -y build-essential cmake pkg-config libopencv-dev   # Ubuntu/Debian
cmake -B build && cmake --build build        # TTS/AI-HTTP/updater are Windows-only guards
```

## Credentials & GitHub (private repo)

Secrets live **only** in the untracked `.env` file (see `.env.example`):

```
GITHUB_PAT_TOKEN=<your fine-grained PAT with Contents: read+write>
NVIDIA_API_KEY=<your key from build.nvidia.com>
GITHUB_REPO_NAME=hand-gesture-sign-language-cpp
NVIDIA_MODEL=z-ai/glm-5.3
```

AI features call Nvidia NIM (`https://integrate.api.nvidia.com/v1/chat/completions`) with the
**GLM** model (`z-ai/glm-5.3`; use `z-ai/glm-5.3-flash` for lower latency by changing
`NVIDIA_MODEL` in `.env`).

`.gitignore` excludes `.env`, `build/`, `.vs/`, `*.exe`, `*.dll`, transcripts and CSVs, so secret
scanning never blocks the push.

To create the **private** repo `hand-gesture-sign-language-cpp` on your account and push `main`:

```powershell
# 1. paste your real token into .env
powershell -ExecutionPolicy Bypass -File push_to_github.ps1
```

The script authenticates via `GET /user`, creates the private repo if missing, adds the remote, and
pushes using a token injected as an HTTP extra-header (the token never appears in the remote URL).
The repository is already committed locally and ready.

The **in-app updater** (`U` / HUD button) uses the same header-auth technique to
`git fetch origin main` + `git pull origin main`, showing the result on the HUD; it resolves Git
from `C:\Program Files\Git` or the portable `F:\tools\MinGit`. For a full rebuild after an update,
run the generated `update_and_build.bat` (regenerated at every CMake configure).

## Output Files

| File | Produced by | Content |
|------|-------------|---------|
| `session_transcript.txt` | `Q`/`ESC` exit | Session start/end timestamps, `[HH:MM:SS] Gesture -> Phrase` log, AI-refined sentences, full communication summary paragraph |
| `telemetry_log.csv` | `L` toggle | `Time_ms, Raw_Fingers, Filtered_Fingers, Defect_Count, Orientation_Deg, Solidity_Ratio, Ambient_Brightness, Stability_Pct, Confirmed_Phrase` |
| `data/sign_language_db.json` | App + your `T` captures | The gesture dictionary (pre-built + custom signatures, persisted) |
| `update_and_build.bat` | CMake configure | Pull + rebuild helper |

## Windows Project Structure

```
hand-gesture-control/
├── CMakeLists.txt / CMakePresets.json / update_and_build.bat
├── .env.example / .gitignore / push_to_github.ps1
├── data/sign_language_db.json        # 13 pre-built gestures + your custom signs
├── third_party/nlohmann/json.hpp     # vendored single-header JSON
├── cmake/update_and_build.bat.in
├── include/                          # CameraStream, HandProcessor, ControlLogic, SignLanguageDB,
│                                     # SpeechEngine, NvidiaAIClient, TranscriptWriter, TelemetryLogger, EnvLoader
└── src/                              # matching implementations + main.cpp (HUD, keys, dwell logic)
```

## HUD Overview

- Top-left: automatic-control command (`CMD:`) + current recognized sign & phrase
- Under the ROI: hold-to-confirm **stability progress bar** (0–100%)
- Bottom: live sentence buffer, AI-refined sentence, FPS, TTS state
- Top-right: clickable `[ AI TUNE (A) ]` and `[ UPDATE GITHUB (U) ]` buttons
- Badges: `[ REC CSV ]` while logging, AI lighting diagnosis, auto-assist prompt when the mask is
  >85% white (glare/background bleed) or <2% white for an extended period

## Calibration Workflow

1. **Fast local** — put your palm on the orange 40x40 box in the ROI center and press `C`.
2. **AI tune** — press `A` (with `NVIDIA_API_KEY` in `.env` for the full NIM analysis; without a
   key a local statistical tuner is used and clearly labeled on the HUD).
3. Manual fine-tuning — the six HSV trackbars in the **HSV Calibration** window stay in sync with
   both calibrators.
