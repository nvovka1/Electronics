# Face Recognition on Raspberry Pi 5 — Design

**Date:** 2026-09-02
**Status:** Approved (design); implementation plan to follow
**Location:** `RasberiPi/face-recognition/`

## Goal

A Python application on a Raspberry Pi 5 (2 GB) that shows the live IMX219 camera
feed on the HDMI monitor, draws a box around every face, labels enrolled people by
name, labels everyone else "Unknown", and lights an LED while a known person is in
frame. People are enrolled live from the camera by pressing a key.

## Hardware and environment

| Item | Value |
|---|---|
| Board | Raspberry Pi 5, 2 GB RAM |
| Camera | IMX219 8 MP, MIPI-CSI, 79.3° FOV (part 536626) |
| Display | HDMI monitor attached to the Pi |
| OS | Raspberry Pi OS with desktop (the desktop is visible on the monitor) |
| Developer access | SSH from a Windows PC; code is edited on Windows, pushed to GitHub, pulled on the Pi |

This is the author's first Raspberry Pi project. The design favours a short path to
a working picture on the screen over sophistication.

## Non-goals

Explicitly out of scope for this version:

- **No liveness / anti-spoofing.** A photograph of an enrolled person held up to the
  camera will be recognized as that person. This is a demo and a learning project, not
  an access-control system.
- No web interface, no remote streaming, no recording of video.
- No multi-camera support.
- No cloud services; everything runs offline on the Pi.
- No reliable recognition of faces smaller than roughly 100 px wide (about 2–3 m from
  this lens). Such faces are still detected and boxed; they will usually read as
  Unknown. Enrollment refuses them outright.

## Recognition engine

OpenCV's bundled face models, driven through OpenCV's own API — no extra ML runtime:

- **YuNet** (`cv2.FaceDetectorYN`) — finds faces, returns a box, five landmarks and a
  confidence score per face. ~230 KB.
- **SFace** (`cv2.FaceRecognizerSF`) — aligns a face to 112×112 using those landmarks
  and produces a 128-float embedding. ~37 MB.

Chosen over `face_recognition`/dlib (no aarch64 wheel; a 30–60 minute source compile
that can exhaust 2 GB of RAM) and over InsightFace + onnxruntime (better accuracy, but
a heavier install and several hundred MB of RAM). If accuracy proves insufficient,
InsightFace can replace `recognizer.py` alone — the interface it exposes
(`embed(frame, face) -> vector`) is model-agnostic by design.

### Model / OpenCV version compatibility

The YuNet model file must match the OpenCV version:

- OpenCV >= 4.8 -> `face_detection_yunet_2023mar.onnx`
- OpenCV 4.5.4-4.7 -> `face_detection_yunet_2022mar.onnx`

Raspberry Pi OS Bookworm's `python3-opencv` is 4.6, so the 2022mar model is the likely
one. `download_models.sh` reads `cv2.__version__` and fetches the correct file. The
download URLs point at the `opencv/opencv_zoo` repository and **must be verified as
live during implementation** — that repository occasionally renames model files.

## Architecture

Ten small modules, each with one responsibility and a narrow interface, so each can be
understood and tested without reading the others.

```
RasberiPi/face-recognition/
├── README.md
├── run.sh                     # sets display env vars, launches the app
├── download_models.sh
├── models/                    # gitignored
├── data/                      # gitignored — the face database
│   ├── faces.npz
│   └── people.json
├── src/
│   ├── config.py              # every threshold, size and pin number
│   ├── camera.py              # frame sources
│   ├── detector.py            # YuNet wrapper
│   ├── recognizer.py          # SFace wrapper
│   ├── face_database.py       # enrolled people; matching
│   ├── tracker.py             # face identity across frames
│   ├── overlay.py             # drawing
│   ├── enroll.py              # live capture flow
│   ├── led.py                 # GPIO
│   └── main.py                # the loop
└── tests/
```

### Module contracts

**`config.py`** — a single frozen dataclass holding all tunables. Nothing else in the
codebase contains a magic number.

**`camera.py`** — exposes `FrameSource` with `read() -> frame | None` and `close()`.
Two implementations:

- `PiCameraSource` — Picamera2 configured for a 1280×720 BGR888 main stream.
- `FileSource` — replays a still image or a directory of images.

`FileSource` exists so the entire pipeline can be exercised on a machine with no
camera, including this Windows development PC. It is a testing seam, not a feature.

**`detector.py`** — `Detector.detect(frame) -> list[Detection]`, where `Detection`
carries `box` (x, y, w, h in *frame* coordinates), `landmarks` (5 points) and `score`.
Internally it detects on a 640×360 copy and scales results back up; callers never see
the detection resolution.

**`recognizer.py`** — `Recognizer.embed(frame, detection) -> np.ndarray` returning an
L2-normalized 128-float vector. Alignment uses the detection's landmarks.

**`face_database.py`** — owns the enrolled people and all matching policy:

- `add(name, embeddings)` — append samples for a person.
- `match(embedding) -> MatchResult(name | None, score, runner_up_name, runner_up_score)`
- `save()` / `load()`
- `people() -> list[PersonInfo]`

**`tracker.py`** — `Tracker.update(detections) -> list[Track]`. Assigns each detection
to an existing track by box overlap (IoU), creates tracks for unmatched detections, and
drops tracks unseen for too long. Each `Track` holds a stable id, the current box, a
short history of identity votes and the frame number of its last identity check.

**`overlay.py`** — pure drawing. `draw(frame, tracks, hud_state) -> frame`. No model
calls, no state.

**`enroll.py`** — the live capture state machine. Given a name, collects samples over
successive frames, applies the quality gate, and returns accepted embeddings or a
failure reason. It does not touch the database itself; `main.py` commits the result.

**`led.py`** — `Led.set(on: bool)`. Backed by `gpiozero` when enabled and available;
a silent no-op stub otherwise, so the app never crashes because a wire is missing.

**`main.py`** — owns the loop, the key handling and the frame counter. It is the only
module that knows about all the others.

## Data flow (per frame)

```
PiCameraSource  ->  1280×720 BGR frame
                     |  resized copy at 640×360
                  YuNet detect  ->  boxes + landmarks + confidence
                     |  boxes scaled back to 720p coordinates
                  Tracker.update  ->  stable track ids
                     |  only for new tracks, or every 10th frame per track
                  SFace embed  ->  128-float vector
                     |
                  FaceDatabase.match  ->  (name | None, score)
                     |  appended to that track's vote history
                  Overlay.draw on the full-res frame
                     |
                  cv2.imshow, fullscreen, on the HDMI monitor
                     |
                  Led.set(any track currently labelled with a known name)
```

Detection runs every frame because it is cheap. Embedding runs only for new tracks and
then once every 10 frames per track, because it is the expensive step and a face does
not become a different person between consecutive frames. This is what keeps the app at
a usable frame rate instead of a slideshow.

## Identity decision

For each identity check on a track:

1. Compute the embedding, L2-normalized.
2. Score it against **every stored sample of every person** by cosine similarity (a dot
   product of normalized vectors). A person's score is their **best-matching sample**,
   so a person enrolled both with and without glasses can be matched by either.
3. The highest-scoring person is the candidate.
4. Accept the candidate only if **both** conditions hold:
   - `score >= MATCH_THRESHOLD` (0.40), and
   - `score - runner_up_score >= MATCH_MARGIN` (0.05).
5. Otherwise the result is `Unknown`, carrying the best candidate and score for display.

The margin rule matters once two enrolled people resemble each other: scoring 0.44
against one and 0.42 against the other is not evidence, and guessing would be worse than
admitting ignorance.

The threshold starts at 0.40 rather than OpenCV's published 0.363 because a false
"that's you" is more confusing to debug than a false "Unknown", and the HUD makes the
correct value easy to find empirically.

### Vote smoothing

Each track keeps its last 5 identity results. The displayed label is the majority of
that history. A single bad frame cannot relabel a person, and a newly arrived person is
named within roughly half a second.

## Enrollment

Pressing `E` prompts for a name in the SSH terminal, then the app collects 5 samples
over successive frames while showing an on-screen countdown.

A frame contributes a sample only if:

| Gate | Value |
|---|---|
| Faces in frame | exactly 1 |
| Detector confidence | >= 0.9 |
| Face width | >= 100 px |

After collection, samples that disagree with the median sample (cosine < 0.5) are
dropped as outliers. If fewer than 3 samples survive, nothing is saved and the reason is
shown on screen and in the terminal. This keeps a bad enrollment from poisoning the
database, which would otherwise show up later as inexplicable false matches.

Enrolling a name that already exists appends samples to that person rather than
replacing them.

Face images are not stored — only embeddings.

### Storage format

- `data/faces.npz` — `embeddings` (M×128 float32) and `labels` (M strings).
- `data/people.json` — per person: name, sample count, enrollment timestamp. Human-
  readable, so the database can be inspected without running the app.

Both are gitignored: they are machine-specific and contain biometric data.

## Screen layout

Fullscreen window on the HDMI monitor.

- **Known face** — green box, label above it: `Volodymyr 0.61`.
- **Unknown face** — red box, label above it: `Unknown`, and beneath it in dim grey:
  `best: Volodymyr 0.34`.
- **HUD**, top-left, small: frame rate, face count, number of enrolled people, and the
  active threshold.
- **Empty database** — the HUD reads `No one enrolled — press E`.
- **Enrolling** — a banner across the top: `Enrolling "Volodymyr" — 3/5 samples`, plus
  the reason when a frame is rejected (`face too small`, `two faces in view`).

Showing the live score rather than only the verdict is deliberate: it turns "why doesn't
it recognize me" from guesswork into a reading.

## Controls

| Key | Action |
|---|---|
| `Q` or `Esc` | Quit |
| `E` | Enroll a person (name is typed in the terminal) |
| `F` | Toggle fullscreen |
| `H` | Toggle the HUD |

## GPIO

One LED with a series resistor (~330 Ω) on **BCM GPIO 17**, driven through `gpiozero`.

- On while at least one track is labelled with a known name.
- Off otherwise, after a 500 ms hold so it does not flicker when a label briefly drops.
- Pin and enable flag live in `config.py`; the LED can be switched off entirely.

## Running it over SSH

A GUI process started over SSH has no idea which display to draw on — this is the
specific problem currently blocking the author. `run.sh` sets the session variables
(`WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, and `DISPLAY` as an X11 fallback) so the window
appears on the HDMI monitor, then launches the app.

If no display is reachable, the app does not crash: it prints the exact `export` lines
needed and continues in `--headless` mode, printing recognitions to the terminal. That
mode also makes the app usable over a plain SSH session with no monitor attached.

## Installation

System packages, not a pip virtualenv. Picamera2 is installed system-wide by the OS and
is awkward to reach from a plain venv, and Bookworm blocks `pip install` into the system
Python (PEP 668):

```
sudo apt install -y python3-picamera2 python3-opencv python3-numpy python3-gpiozero
```

If a virtualenv is ever required, it must be created with `--system-site-packages`.

## Error handling

| Situation | Behaviour |
|---|---|
| No camera detected | Exit with a message naming `rpicam-hello --list-cameras` as the check to run |
| Model files missing | Exit with a message naming `./download_models.sh` |
| OpenCV too old for the model | Exit explaining which model version that OpenCV needs |
| No display available | Warn, print the required `export` lines, continue headless |
| Empty face database | Run normally; every face is Unknown; the HUD says to press `E` |
| `gpiozero` missing or pin unavailable | Warn once, continue with the LED disabled |
| Enrollment fails the quality gate | Save nothing; state the reason on screen |
| Camera returns no frame | Retry a few times, then exit with a clear message rather than hanging |

## Testing

**Unit tests (`pytest`), runnable on the Windows development machine with no camera and
no Picamera2** — this is why the pure-logic modules are kept free of hardware calls:

- `face_database` — add, save, load round-trip; matching above and below threshold; the
  margin rule rejecting an ambiguous pair; best-sample-wins across multiple samples;
  behaviour on an empty database.
- `tracker` — IoU assignment across frames; new track creation; track expiry after
  misses; vote majority including the tie case.
- `enroll` — the quality gate accepting good samples and rejecting small faces, low
  confidence, multiple faces and outliers; the fewer-than-3 failure path.
- `overlay` — box coordinates scaling correctly from detection resolution to display
  resolution, and labels staying inside the frame when a face is at an edge.

All of these operate on synthetic embeddings and boxes — no models are loaded.

**On-device verification:**

- `./download_models.sh`, then a still-image run:
  `python -m src.main --source file:tests/fixtures/face.jpg --headless`, which exercises
  the camera-free source, detection, embedding, matching and printing.
- Live run: enroll one person, confirm the green box and name; confirm a second,
  un-enrolled person shows Unknown; confirm the LED follows.
- Frame rate is read from the HUD; the target is >= 15 FPS at 1280×720.

## Risks

- **YuNet model / OpenCV version mismatch** is the most likely first failure, and it
  fails with an unhelpful ONNX shape error. `download_models.sh` selecting the model by
  `cv2.__version__` is the mitigation.
- **Picamera2 not visible to Python** if a venv is created without `--system-site-packages`.
- **Model download URLs** may have moved; verify before writing the script.
- **Accuracy in poor light** is the known weakness of SFace. The mitigation is
  re-enrolling under the actual conditions; the fallback is InsightFace behind the same
  `recognizer.py` interface.
