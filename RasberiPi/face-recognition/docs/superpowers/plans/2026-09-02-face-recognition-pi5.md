# Face Recognition on Raspberry Pi 5 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Python app on a Raspberry Pi 5 that shows the live IMX219 camera feed fullscreen on the HDMI monitor, labels enrolled people by name and everyone else "Unknown", and lights an LED while a known person is in frame.

**Architecture:** Ten small single-responsibility modules under `src/`, wired together by one loop in `src/main.py`. Face detection uses OpenCV's YuNet; identity uses OpenCV's SFace embeddings compared by cosine similarity against a local database of enrolled samples. Detection runs every frame; the expensive embedding step runs only for new faces and then every tenth frame per face.

**Tech Stack:** Python 3, OpenCV (`cv2`), NumPy, Picamera2, gpiozero, pytest. All installed from Raspberry Pi OS apt packages — no pip, no virtualenv.

**Spec:** [2026-09-02-face-recognition-pi5-design.md](../specs/2026-09-02-face-recognition-pi5-design.md)

## Global Constraints

- **All code runs and all tests run on the Raspberry Pi**, reached at `admin@192.168.0.111`. The Windows machine only edits files. Windows has Python 3.14 with no NumPy, OpenCV or pytest — never try to run the test suite there.
- **Project root on the Pi:** `/home/admin/face-recognition`. Files are pushed there with `sync.ps1` from Windows.
- **Project root in the repo:** `RasberiPi/face-recognition/`. All paths in this plan are relative to that directory unless stated otherwise.
- **No pip installs.** Raspberry Pi OS Bookworm blocks installing into the system Python (PEP 668), and Picamera2 is a system package. Everything comes from `apt`.
- **No magic numbers outside `src/config.py`.** Every threshold, size, and pin number is a field on `Config`.
- **Match threshold `0.40`, match margin `0.05`, vote window `5`, re-identify every `10` frames, LED on BCM GPIO `17` with a `500` ms off-hold, `5` enrollment samples, minimum face width `100` px, minimum detector confidence `0.9`, outlier cosine cut-off `0.5`, minimum `3` accepted samples.** These exact values go in `Config`.
- **Capture at 1280×720, detect at 640×360.**
- **Enrolled person names are stored in a fixed-width 64-character NumPy string column.** Names longer than 64 characters are truncated on save.
- **`models/` and `data/` are gitignored.** The face database holds biometric data and must never be committed.
- **Commit after every task**, with the test suite green.

---

## File Structure

| File | Responsibility |
|---|---|
| `.gitignore` | Keep models, the face database and Python caches out of git |
| `README.md` | Setup, run, keys, tuning, troubleshooting |
| `sync.ps1` | Windows → Pi file copy over SSH |
| `run.sh` | Set display environment variables so a window lands on the HDMI monitor, then launch Python |
| `download_models.sh` | Fetch the YuNet and SFace ONNX files matching the installed OpenCV |
| `src/config.py` | Every tunable value |
| `src/camera.py` | `PiCameraSource` and `FileSource`, both yielding BGR frames |
| `src/detector.py` | YuNet wrapper; boxes in full-frame coordinates |
| `src/recognizer.py` | SFace wrapper; one face → a 128-float unit vector |
| `src/face_database.py` | Enrolled people, persistence, and the accept/reject policy |
| `src/tracker.py` | Face identity across frames by box overlap; vote smoothing |
| `src/overlay.py` | All drawing |
| `src/enroll.py` | Enrollment quality gate and sample collection |
| `src/led.py` | GPIO LED with an off-hold, and a no-op fallback |
| `src/main.py` | The loop and key handling |
| `tools/check_camera.py` | Standalone camera-to-screen smoke test |
| `tools/grab_still.py` | Save one camera frame as a test fixture |
| `tests/conftest.py` | Put the project root on `sys.path` |
| `tests/test_*.py` | One test module per logic module |

---

## Task 1: Skeleton, config, camera source, and a picture on the HDMI screen

The first milestone is deliberately not recognition. It proves the camera works, proves a window can reach the HDMI monitor from an SSH session, and establishes the sync loop that every later task depends on. If anything about this project is going to fail, it will fail here.

**Files:**
- Create: `.gitignore`
- Create: `sync.ps1`
- Create: `run.sh`
- Create: `src/__init__.py`
- Create: `src/config.py`
- Create: `src/camera.py`
- Create: `tools/check_camera.py`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `src.config.CONFIG` — a frozen `Config` instance; every later task reads its fields.
  - `src.camera.open_source(spec: str, width: int, height: int) -> FrameSource` where `spec` is `"camera"` or `"file:<path>"`.
  - `src.camera.FrameSource.read() -> np.ndarray | None` and `.close() -> None`.
  - `src.camera.CameraError` — raised for any unusable frame source.

- [ ] **Step 1: Install the system packages on the Pi**

Run over SSH:

```bash
ssh admin@192.168.0.111 "sudo apt update && sudo apt install -y python3-picamera2 python3-opencv python3-numpy python3-gpiozero python3-pytest curl"
```

Expected: apt completes without error. Then confirm the versions, which decide which YuNet model Task 5 downloads:

```bash
ssh admin@192.168.0.111 "python3 -c 'import cv2, numpy; print(\"opencv\", cv2.__version__); print(\"numpy\", numpy.__version__)'"
```

Expected: two version lines, e.g. `opencv 4.6.0`. Write the OpenCV version down — Task 5 needs it.

- [ ] **Step 2: Confirm the camera is seen by the system**

```bash
ssh admin@192.168.0.111 "rpicam-hello --list-cameras"
```

Expected: a block describing an `imx219` sensor with available modes. If it says no cameras were found, stop here — the ribbon cable or `/boot/firmware/config.txt` is the problem, and nothing later in this plan can work.

- [ ] **Step 3: Create `.gitignore`**

```
models/
data/
__pycache__/
*.pyc
.pytest_cache/
tests/fixtures/
```

`tests/fixtures/` is ignored because the only fixture is a photograph of a real person's face.

- [ ] **Step 4: Create `src/__init__.py`**

An empty file. It makes `src` a package so `from src.camera import ...` works.

```python
```

- [ ] **Step 5: Create `src/config.py`**

```python
"""Every tunable value in the application lives here.

No other module may contain a magic number. When behaviour needs adjusting -
recognition too strict, LED flickering, frame rate too low - this is the only
file to edit.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Config:
    # Frame sizes. Capture is what you see; detection runs on a smaller copy
    # because finding faces at 640x360 costs a quarter of what it costs at 720p.
    capture_width: int = 1280
    capture_height: int = 720
    detect_width: int = 640
    detect_height: int = 360

    # YuNet detection.
    detect_score_threshold: float = 0.85
    detect_nms_threshold: float = 0.3
    detect_top_k: int = 50

    # Identity. A face is accepted as a known person only when it clears
    # match_threshold AND beats the runner-up person by match_margin.
    match_threshold: float = 0.40
    match_margin: float = 0.05
    vote_window: int = 5
    reidentify_every: int = 10

    # Tracking a face between frames.
    track_iou_min: float = 0.3
    track_max_misses: int = 8

    # Enrollment quality gate. Samples are taken every enroll_sample_interval
    # frames rather than every frame, so the five samples span a second or two
    # of real movement instead of being five copies of one instant.
    enroll_samples: int = 5
    enroll_sample_interval: int = 8
    enroll_min_face_px: int = 100
    enroll_min_det_score: float = 0.9
    enroll_outlier_similarity: float = 0.5
    enroll_min_accepted: int = 3

    # LED.
    led_enabled: bool = True
    led_pin: int = 17
    led_hold_ms: int = 500

    # Directories, relative to the project root.
    models_dir: str = "models"
    data_dir: str = "data"


CONFIG = Config()
```

- [ ] **Step 6: Create `src/camera.py`**

```python
"""Frame sources.

Everything downstream sees a plain BGR NumPy array and does not care whether it
came from the camera or from a file on disk. The file source exists so the whole
pipeline can be exercised without a camera - it is a testing seam, not a feature.
"""
from __future__ import annotations

import glob
import os
from typing import Optional

import cv2
import numpy as np


class CameraError(RuntimeError):
    """A frame source could not be opened, or has stopped producing frames."""


class FrameSource:
    def read(self) -> Optional[np.ndarray]:
        raise NotImplementedError

    def close(self) -> None:
        raise NotImplementedError


class PiCameraSource(FrameSource):
    def __init__(self, width: int, height: int) -> None:
        try:
            from picamera2 import Picamera2
        except ImportError as exc:
            raise CameraError(
                "picamera2 is not installed. Run:\n"
                "  sudo apt install -y python3-picamera2"
            ) from exc

        try:
            self._camera = Picamera2()
        except Exception as exc:
            raise CameraError(
                "No camera found. Check the ribbon cable, then run:\n"
                "  rpicam-hello --list-cameras"
            ) from exc

        # Picamera2's "RGB888" produces an array whose channels are in BGR order,
        # which is exactly what OpenCV expects. This is a known quirk of the
        # library, not a mistake. If colours look inverted on screen (skin looks
        # blue), switch this string to "BGR888".
        configuration = self._camera.create_preview_configuration(
            main={"size": (width, height), "format": "RGB888"}
        )
        self._camera.configure(configuration)
        self._camera.start()

    def read(self) -> Optional[np.ndarray]:
        return self._camera.capture_array()

    def close(self) -> None:
        self._camera.stop()
        self._camera.close()


class FileSource(FrameSource):
    """Replays a still image, or every image in a directory, looping forever."""

    def __init__(self, path: str, width: int, height: int) -> None:
        if os.path.isdir(path):
            self._paths = sorted(
                candidate
                for candidate in glob.glob(os.path.join(path, "*"))
                if candidate.lower().endswith((".jpg", ".jpeg", ".png"))
            )
        else:
            self._paths = [path]
        if not self._paths:
            raise CameraError(f"No images found at {path}")
        self._size = (width, height)
        self._index = 0

    def read(self) -> Optional[np.ndarray]:
        path = self._paths[self._index % len(self._paths)]
        self._index += 1
        image = cv2.imread(path)
        if image is None:
            raise CameraError(f"Could not read image {path}")
        return cv2.resize(image, self._size)

    def close(self) -> None:
        pass


def open_source(spec: str, width: int, height: int) -> FrameSource:
    """spec is "camera", or "file:<path to an image or a directory>"."""
    if spec == "camera":
        return PiCameraSource(width, height)
    if spec.startswith("file:"):
        return FileSource(spec[len("file:"):], width, height)
    raise CameraError(f'Unknown source "{spec}". Use "camera" or "file:<path>".')
```

- [ ] **Step 7: Create `run.sh`**

This is the fix for the problem that currently blocks the project: a GUI program started over SSH has no idea which display to draw on.

```bash
#!/usr/bin/env bash
# Launch a GUI Python program so its window appears on the HDMI monitor, even
# when this script is started from an SSH session on another machine.
#
#   ./run.sh tools/check_camera.py
#   ./run.sh -m src.main
set -euo pipefail

export XDG_RUNTIME_DIR="/run/user/$(id -u)"

# Raspberry Pi OS desktop runs Wayland. Find the compositor's socket rather than
# guessing its number, which changes between sessions.
if [ -z "${WAYLAND_DISPLAY:-}" ]; then
  for socket in "$XDG_RUNTIME_DIR"/wayland-*; do
    [ -S "$socket" ] || continue
    export WAYLAND_DISPLAY="$(basename "$socket")"
    break
  done
fi

# X11 fallback, used when the desktop is running Xorg instead of Wayland.
export DISPLAY="${DISPLAY:-:0}"

cd "$(dirname "$0")"
exec python3 "$@"
```

- [ ] **Step 8: Create `tools/check_camera.py`**

```python
"""Smoke test: put the camera picture on the HDMI monitor and nothing else.

    ./run.sh tools/check_camera.py

Press Q to quit. If this shows a picture, the camera and the display are both
working and every later problem is a software problem.
"""
import os
import sys
import time

import cv2

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.camera import CameraError, open_source  # noqa: E402
from src.config import CONFIG  # noqa: E402

WINDOW = "Camera check"


def main() -> int:
    if not (os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY")):
        print("No display set. Start this with ./run.sh instead of python3.",
              file=sys.stderr)
        return 1

    try:
        source = open_source("camera", CONFIG.capture_width, CONFIG.capture_height)
    except CameraError as error:
        print(error, file=sys.stderr)
        return 1

    cv2.namedWindow(WINDOW, cv2.WINDOW_NORMAL)
    cv2.setWindowProperty(WINDOW, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    fps = 0.0
    last = time.monotonic()
    try:
        while True:
            frame = source.read()
            if frame is None:
                continue
            now = time.monotonic()
            instant = 1.0 / max(now - last, 1e-6)
            fps = instant if fps == 0.0 else 0.9 * fps + 0.1 * instant
            last = now

            cv2.putText(frame, f"{fps:4.1f} fps  -  press Q to quit", (14, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2, cv2.LINE_AA)
            cv2.imshow(WINDOW, frame)
            if (cv2.waitKey(1) & 0xFF) in (ord("q"), 27):
                break
    finally:
        source.close()
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 9: Create `sync.ps1`**

```powershell
# Copy the project to the Raspberry Pi over SSH. Run this from Windows after
# every edit:  .\sync.ps1
#
# models/ and data/ are deliberately not copied - they live only on the Pi.

$PiTarget  = if ($env:PI_TARGET) { $env:PI_TARGET } else { "admin@192.168.0.111" }
$RemoteDir = "/home/admin/face-recognition"
$LocalDir  = $PSScriptRoot

Write-Host "Syncing $LocalDir -> ${PiTarget}:$RemoteDir"
ssh $PiTarget "mkdir -p $RemoteDir"

foreach ($item in @("src", "tools", "tests", "run.sh", "download_models.sh")) {
    $path = Join-Path $LocalDir $item
    if (Test-Path $path) {
        scp -r $path "${PiTarget}:$RemoteDir/"
    }
}

ssh $PiTarget "chmod +x $RemoteDir/run.sh $RemoteDir/download_models.sh 2>/dev/null; true"
Write-Host "Done."
```

- [ ] **Step 10: Sync to the Pi**

From `RasberiPi/face-recognition` on Windows:

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

Expected: `Syncing ... Done.` with no scp errors. If `ssh` is not recognised, install the Windows OpenSSH client from Settings → Apps → Optional features.

- [ ] **Step 11: Verify the picture appears on the HDMI monitor**

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && ./run.sh tools/check_camera.py"
```

Expected: **the live camera picture fills the HDMI monitor**, with a frame-rate counter in the top-left. Press `Q` on a keyboard attached to the Pi, or `Ctrl+C` in the SSH session, to stop.

Three things to check while it runs, because each one is cheaper to fix now than later:

1. Colours are natural. If faces look blue, change `"RGB888"` to `"BGR888"` in `src/camera.py` and re-sync.
2. The frame rate is at least 25 fps. Lower than that means something is wrong before any recognition work has been added.
3. The image is the right way up. If the camera is mounted upside down, add `"transform": Transform(hflip=1, vflip=1)` to the Picamera2 configuration — but only if needed.

If instead it prints a Qt or Wayland error and no window appears, run `ssh admin@192.168.0.111 "ls /run/user/1000/"` to confirm a `wayland-0` or `wayland-1` socket exists. If it does not, the desktop session is not running under that user.

- [ ] **Step 12: Commit**

```bash
git add RasberiPi/face-recognition
git commit -m "feat: camera preview on HDMI, project skeleton and Pi sync script"
```

---

## Task 2: The face database

Pure logic — no camera, no models. This is the module that decides whether a face is a known person, so it gets the most thorough tests in the project.

**Files:**
- Create: `tests/conftest.py`
- Create: `src/face_database.py`
- Test: `tests/test_face_database.py`

**Interfaces:**
- Consumes: `src.config.CONFIG` (by the caller, not by this module — thresholds are constructor arguments so tests can vary them).
- Produces:
  - `normalize(vector: np.ndarray) -> np.ndarray` — unit-length float32.
  - `MatchResult(name: str | None, score: float, best_name: str | None, runner_up_name: str | None, runner_up_score: float)`. `name is None` means Unknown.
  - `PersonInfo(name: str, samples: int, enrolled_at: str)`.
  - `FaceDatabase(threshold: float, margin: float)` with `.add(name, embeddings)`, `.match(embedding) -> MatchResult`, `.people() -> list[PersonInfo]`, `.is_empty() -> bool`, `.save(data_dir)`, and the classmethod `.load(data_dir, threshold, margin) -> FaceDatabase`.

- [ ] **Step 1: Create `tests/conftest.py`**

Without this, `from src.face_database import ...` fails inside pytest.

```python
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
```

- [ ] **Step 2: Write the failing tests**

Create `tests/test_face_database.py`:

```python
import numpy as np
import pytest

from src.face_database import FaceDatabase, normalize

THRESHOLD = 0.40
MARGIN = 0.05


def vector(*values):
    """Build a 128-length vector whose first entries are the given values."""
    result = np.zeros(128, dtype=np.float32)
    for index, value in enumerate(values):
        result[index] = value
    return result


@pytest.fixture
def database():
    return FaceDatabase(THRESHOLD, MARGIN)


def test_empty_database_returns_unknown(database):
    result = database.match(vector(1.0))
    assert result.name is None
    assert result.score == 0.0
    assert database.is_empty()


def test_exact_match_is_accepted(database):
    database.add("Alice", [vector(1.0, 0.0)])
    database.add("Bob", [vector(0.0, 1.0)])

    result = database.match(vector(1.0, 0.0))

    assert result.name == "Alice"
    assert result.score == pytest.approx(1.0, abs=1e-5)


def test_score_below_threshold_is_unknown_but_reports_the_candidate(database):
    database.add("Alice", [vector(1.0, 0.0)])
    database.add("Bob", [vector(0.0, 1.0)])

    # Equally 0.30 similar to both, so neither clears 0.40.
    result = database.match(vector(1.0, 1.0, 3.0))

    assert result.name is None
    assert result.best_name in ("Alice", "Bob")
    assert result.score == pytest.approx(1.0 / np.sqrt(11.0), abs=1e-5)


def test_ambiguous_pair_is_rejected_by_the_margin_rule(database):
    database.add("Alice", [vector(1.0, 0.0)])
    database.add("Bob", [vector(0.0, 1.0)])

    # 0.707 against both: over the threshold, but no margin between them.
    result = database.match(vector(1.0, 1.0))

    assert result.name is None
    assert result.score == pytest.approx(0.7071, abs=1e-3)
    assert result.runner_up_score == pytest.approx(0.7071, abs=1e-3)


def test_a_person_scores_as_their_best_sample(database):
    # Alice enrolled twice, looking quite different each time.
    database.add("Alice", [vector(1.0, 0.0), vector(0.0, 1.0)])
    database.add("Bob", [vector(0.0, 0.0, 1.0)])

    result = database.match(vector(0.0, 1.0))

    assert result.name == "Alice"
    assert result.score == pytest.approx(1.0, abs=1e-5)


def test_people_lists_names_and_sample_counts(database):
    database.add("Alice", [vector(1.0), vector(1.0, 0.1)])
    database.add("Bob", [vector(0.0, 1.0)])

    people = {person.name: person.samples for person in database.people()}

    assert people == {"Alice": 2, "Bob": 1}


def test_adding_an_existing_name_appends_samples(database):
    database.add("Alice", [vector(1.0)])
    database.add("Alice", [vector(0.0, 1.0)])

    assert database.people()[0].samples == 2


def test_save_and_load_round_trip(tmp_path):
    original = FaceDatabase(THRESHOLD, MARGIN)
    original.add("Alice", [vector(1.0, 0.0)])
    original.add("Bob", [vector(0.0, 1.0)])
    original.save(str(tmp_path))

    restored = FaceDatabase.load(str(tmp_path), THRESHOLD, MARGIN)

    assert [p.name for p in restored.people()] == ["Alice", "Bob"]
    assert restored.match(vector(1.0, 0.0)).name == "Alice"
    assert restored.people()[0].enrolled_at != ""


def test_loading_a_missing_database_gives_an_empty_one(tmp_path):
    restored = FaceDatabase.load(str(tmp_path / "nothing"), THRESHOLD, MARGIN)
    assert restored.is_empty()


def test_normalize_makes_unit_length():
    assert np.linalg.norm(normalize(vector(3.0, 4.0))) == pytest.approx(1.0, abs=1e-6)


def test_normalize_leaves_a_zero_vector_alone():
    assert np.linalg.norm(normalize(np.zeros(128, dtype=np.float32))) == 0.0
```

- [ ] **Step 3: Sync and run the tests to verify they fail**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

Then:

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/test_face_database.py -v"
```

Expected: collection error — `ModuleNotFoundError: No module named 'src.face_database'`.

- [ ] **Step 4: Create `src/face_database.py`**

```python
"""The enrolled people, and the policy that decides whether a face is one of them.

Identity is a cosine similarity between 128-number vectors. Two vectors are
compared with a dot product, which is a cosine only when both are unit length -
so every vector entering this module is normalized on the way in.
"""
from __future__ import annotations

import json
import os
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Optional

import numpy as np

EMBEDDING_SIZE = 128
NAME_DTYPE = "<U64"  # names are stored in a fixed-width column, so 64 characters max


@dataclass(frozen=True)
class MatchResult:
    name: Optional[str]              # None means Unknown
    score: float                     # the best similarity found
    best_name: Optional[str]         # the top candidate, named even when rejected
    runner_up_name: Optional[str]
    runner_up_score: float


@dataclass(frozen=True)
class PersonInfo:
    name: str
    samples: int
    enrolled_at: str


def normalize(vector: np.ndarray) -> np.ndarray:
    """Scale a vector to unit length, so a dot product with another such vector
    is a cosine similarity. A zero vector is returned unchanged."""
    array = np.asarray(vector, dtype=np.float32)
    norm = float(np.linalg.norm(array))
    if norm == 0.0:
        return array
    return (array / norm).astype(np.float32)


class FaceDatabase:
    def __init__(self, threshold: float, margin: float) -> None:
        self._threshold = threshold
        self._margin = margin
        self._embeddings = np.zeros((0, EMBEDDING_SIZE), dtype=np.float32)
        self._labels: list[str] = []
        self._enrolled_at: dict[str, str] = {}

    def is_empty(self) -> bool:
        return len(self._labels) == 0

    def add(self, name: str, embeddings) -> None:
        """Append samples for a person. Enrolling an existing name adds to that
        person rather than replacing them."""
        rows = np.vstack([normalize(embedding) for embedding in embeddings])
        self._embeddings = np.vstack([self._embeddings, rows])
        self._labels.extend([name] * len(rows))
        self._enrolled_at.setdefault(
            name, datetime.now(timezone.utc).isoformat(timespec="seconds")
        )

    def people(self) -> list[PersonInfo]:
        counts: dict[str, int] = {}
        for label in self._labels:
            counts[label] = counts.get(label, 0) + 1
        return [
            PersonInfo(name, count, self._enrolled_at.get(name, ""))
            for name, count in sorted(counts.items())
        ]

    def match(self, embedding) -> MatchResult:
        if self.is_empty():
            return MatchResult(None, 0.0, None, None, 0.0)

        query = normalize(embedding)
        similarities = self._embeddings @ query

        # A person scores as their best-matching sample, so somebody enrolled
        # both with and without glasses can be matched by either.
        best_per_person: dict[str, float] = {}
        for label, similarity in zip(self._labels, similarities):
            if similarity > best_per_person.get(label, -1.0):
                best_per_person[label] = float(similarity)

        ranked = sorted(best_per_person.items(), key=lambda item: item[1], reverse=True)
        best_name, best_score = ranked[0]
        runner_up_name, runner_up_score = ranked[1] if len(ranked) > 1 else (None, 0.0)

        # Both rules must hold: clear the bar, and be clearly ahead of the next
        # candidate. Guessing between two similar people is worse than Unknown.
        accepted = (
            best_score >= self._threshold
            and best_score - runner_up_score >= self._margin
        )

        return MatchResult(
            name=best_name if accepted else None,
            score=best_score,
            best_name=best_name,
            runner_up_name=runner_up_name,
            runner_up_score=runner_up_score,
        )

    def save(self, data_dir: str) -> None:
        os.makedirs(data_dir, exist_ok=True)
        np.savez(
            os.path.join(data_dir, "faces.npz"),
            embeddings=self._embeddings,
            labels=np.array(self._labels, dtype=NAME_DTYPE),
        )
        with open(os.path.join(data_dir, "people.json"), "w", encoding="utf-8") as handle:
            json.dump(
                {"people": [asdict(person) for person in self.people()]},
                handle,
                indent=2,
                ensure_ascii=False,
            )

    @classmethod
    def load(cls, data_dir: str, threshold: float, margin: float) -> "FaceDatabase":
        database = cls(threshold, margin)

        archive_path = os.path.join(data_dir, "faces.npz")
        if not os.path.exists(archive_path):
            return database

        with np.load(archive_path) as archive:
            database._embeddings = archive["embeddings"].astype(np.float32)
            database._labels = [str(label) for label in archive["labels"]]

        json_path = os.path.join(data_dir, "people.json")
        if os.path.exists(json_path):
            with open(json_path, encoding="utf-8") as handle:
                for entry in json.load(handle).get("people", []):
                    database._enrolled_at[entry["name"]] = entry["enrolled_at"]

        return database
```

- [ ] **Step 5: Sync and run the tests to verify they pass**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/test_face_database.py -v"
```

Expected: 11 passed.

- [ ] **Step 6: Commit**

```bash
git add RasberiPi/face-recognition/src/face_database.py RasberiPi/face-recognition/tests
git commit -m "feat: face database with threshold and margin matching"
```

---

## Task 3: Tracking faces between frames

Without this, every frame is judged independently and labels flicker. The tracker gives each face a stable identity across frames so that a majority vote can smooth out bad readings, and so the expensive embedding step can be skipped for a face already identified.

**Files:**
- Create: `src/tracker.py`
- Test: `tests/test_tracker.py`

**Interfaces:**
- Consumes: nothing. `Tracker.update()` accepts any object with a `.box` attribute — it does not import the detector, which keeps it testable with simple stubs.
- Produces:
  - `iou(a: Box, b: Box) -> float` where `Box = (x, y, w, h)`.
  - `Track` with fields `id`, `box`, `votes`, `detection`, `misses`, `last_identified_frame`, `last_score`, `last_best_name`; the property `label -> str | None`; and the methods `record_identity(name, score, best_name, frame_number)` and `needs_identity(frame_number, interval) -> bool`.
  - `Tracker(iou_min, max_misses, vote_window)` with `.update(detections) -> list[Track]` returning only the tracks visible in this frame.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_tracker.py`:

```python
from dataclasses import dataclass

import pytest

from src.tracker import Tracker, iou

IOU_MIN = 0.3
MAX_MISSES = 8
VOTE_WINDOW = 5


@dataclass
class FakeDetection:
    box: tuple


def tracker():
    return Tracker(IOU_MIN, MAX_MISSES, VOTE_WINDOW)


def test_iou_of_identical_boxes_is_one():
    assert iou((0, 0, 10, 10), (0, 0, 10, 10)) == pytest.approx(1.0)


def test_iou_of_disjoint_boxes_is_zero():
    assert iou((0, 0, 10, 10), (50, 50, 10, 10)) == 0.0


def test_iou_of_half_overlapping_boxes():
    # Intersection 50, union 150.
    assert iou((0, 0, 10, 10), (5, 0, 10, 10)) == pytest.approx(1.0 / 3.0)


def test_a_face_keeps_its_id_across_frames():
    subject = tracker()

    first = subject.update([FakeDetection((100, 100, 80, 80))])
    second = subject.update([FakeDetection((104, 102, 80, 80))])

    assert first[0].id == second[0].id


def test_a_face_somewhere_else_gets_a_new_id():
    subject = tracker()

    first = subject.update([FakeDetection((100, 100, 80, 80))])
    second = subject.update([FakeDetection((600, 400, 80, 80))])

    assert first[0].id != second[0].id


def test_two_faces_get_separate_ids():
    subject = tracker()

    tracks = subject.update([
        FakeDetection((100, 100, 80, 80)),
        FakeDetection((600, 400, 80, 80)),
    ])

    assert len({track.id for track in tracks}) == 2


def test_a_briefly_missing_face_keeps_its_id():
    subject = tracker()
    original = subject.update([FakeDetection((100, 100, 80, 80))])[0].id

    for _ in range(3):
        assert subject.update([]) == []

    returned = subject.update([FakeDetection((100, 100, 80, 80))])

    assert returned[0].id == original


def test_a_long_gone_face_gets_a_new_id():
    subject = tracker()
    original = subject.update([FakeDetection((100, 100, 80, 80))])[0].id

    for _ in range(MAX_MISSES + 2):
        subject.update([])

    returned = subject.update([FakeDetection((100, 100, 80, 80))])

    assert returned[0].id != original


def test_label_is_the_majority_of_recent_votes():
    subject = tracker()
    track = subject.update([FakeDetection((100, 100, 80, 80))])[0]

    track.record_identity("Bob", 0.5, "Bob", 1)
    track.record_identity("Alice", 0.7, "Alice", 2)
    track.record_identity("Alice", 0.7, "Alice", 3)

    assert track.label == "Alice"


def test_unknown_wins_when_it_is_the_majority():
    subject = tracker()
    track = subject.update([FakeDetection((100, 100, 80, 80))])[0]

    track.record_identity(None, 0.2, "Alice", 1)
    track.record_identity(None, 0.2, "Alice", 2)
    track.record_identity("Alice", 0.5, "Alice", 3)

    assert track.label is None


def test_votes_older_than_the_window_are_forgotten():
    subject = tracker()
    track = subject.update([FakeDetection((100, 100, 80, 80))])[0]

    for frame in range(VOTE_WINDOW):
        track.record_identity("Bob", 0.5, "Bob", frame)
    for frame in range(VOTE_WINDOW, VOTE_WINDOW * 2):
        track.record_identity("Alice", 0.7, "Alice", frame)

    assert track.label == "Alice"


def test_a_new_track_needs_identifying():
    subject = tracker()
    track = subject.update([FakeDetection((100, 100, 80, 80))])[0]

    assert track.needs_identity(frame_number=1, interval=10)


def test_a_recently_identified_track_does_not_need_identifying():
    subject = tracker()
    track = subject.update([FakeDetection((100, 100, 80, 80))])[0]
    track.record_identity("Alice", 0.7, "Alice", frame_number=10)

    assert not track.needs_identity(frame_number=15, interval=10)
    assert track.needs_identity(frame_number=20, interval=10)


def test_the_detection_is_kept_on_the_track():
    subject = tracker()
    detection = FakeDetection((100, 100, 80, 80))

    track = subject.update([detection])[0]

    assert track.detection is detection
```

- [ ] **Step 2: Sync and run the tests to verify they fail**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/test_tracker.py -v"
```

Expected: `ModuleNotFoundError: No module named 'src.tracker'`.

- [ ] **Step 3: Create `src/tracker.py`**

```python
"""Follows each face from frame to frame, so its label can stay put.

Matching is by box overlap: the same face barely moves between two consecutive
frames, so the box that overlaps most is the same person. This is deliberately
simple - it is not a motion model, and two faces that cross over each other may
swap ids. For a desk-facing camera that is a fair trade for the simplicity.
"""
from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass, field
from typing import Optional

Box = tuple[int, int, int, int]  # x, y, w, h


def iou(first: Box, second: Box) -> float:
    """Intersection over union: 1.0 for identical boxes, 0.0 for no overlap."""
    ax, ay, aw, ah = first
    bx, by, bw, bh = second

    left = max(ax, bx)
    top = max(ay, by)
    right = min(ax + aw, bx + bw)
    bottom = min(ay + ah, by + bh)

    if right <= left or bottom <= top:
        return 0.0

    intersection = (right - left) * (bottom - top)
    union = aw * ah + bw * bh - intersection
    return intersection / union if union > 0 else 0.0


@dataclass
class Track:
    id: int
    box: Box
    votes: deque
    detection: object = None
    misses: int = 0
    last_identified_frame: int = -1
    last_score: float = 0.0
    last_best_name: Optional[str] = None

    def record_identity(
        self,
        name: Optional[str],
        score: float,
        best_name: Optional[str],
        frame_number: int,
    ) -> None:
        self.votes.append(name)
        self.last_score = score
        self.last_best_name = best_name
        self.last_identified_frame = frame_number

    @property
    def label(self) -> Optional[str]:
        """The majority of recent identity checks. None means Unknown.

        Voting is what stops a single bad frame from renaming somebody."""
        if not self.votes:
            return None
        winner, _ = Counter(self.votes).most_common(1)[0]
        return winner

    def needs_identity(self, frame_number: int, interval: int) -> bool:
        """True for a face never identified, or not identified recently.

        Re-checking on an interval is what keeps the frame rate up: embedding a
        face is the expensive step, and a face does not become a different
        person between consecutive frames."""
        if self.last_identified_frame < 0:
            return True
        return frame_number - self.last_identified_frame >= interval


class Tracker:
    def __init__(self, iou_min: float, max_misses: int, vote_window: int) -> None:
        self._iou_min = iou_min
        self._max_misses = max_misses
        self._vote_window = vote_window
        self._tracks: list[Track] = []
        self._next_id = 1

    def update(self, detections) -> list[Track]:
        """Feed this frame's detections in; get back the tracks visible now.

        `detections` is any sequence of objects carrying a `.box` attribute."""
        unmatched = list(range(len(detections)))

        for track in self._tracks:
            best_index: Optional[int] = None
            best_overlap = self._iou_min
            for index in unmatched:
                overlap = iou(track.box, detections[index].box)
                if overlap >= best_overlap:
                    best_index, best_overlap = index, overlap

            if best_index is None:
                track.misses += 1
            else:
                track.box = detections[best_index].box
                track.detection = detections[best_index]
                track.misses = 0
                unmatched.remove(best_index)

        for index in unmatched:
            self._tracks.append(
                Track(
                    id=self._next_id,
                    box=detections[index].box,
                    votes=deque(maxlen=self._vote_window),
                    detection=detections[index],
                )
            )
            self._next_id += 1

        self._tracks = [t for t in self._tracks if t.misses <= self._max_misses]
        return [t for t in self._tracks if t.misses == 0]
```

- [ ] **Step 4: Sync and run the tests to verify they pass**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/ -v"
```

Expected: 25 passed (11 from Task 2, 14 here).

- [ ] **Step 5: Commit**

```bash
git add RasberiPi/face-recognition/src/tracker.py RasberiPi/face-recognition/tests/test_tracker.py
git commit -m "feat: face tracking across frames with vote smoothing"
```

---

## Task 4: Drawing

All drawing lives here so that the main loop stays about logic. The one piece of real logic — keeping a label on screen when a face is at the top edge — is tested.

**Files:**
- Create: `src/overlay.py`
- Test: `tests/test_overlay.py`

**Interfaces:**
- Consumes: `src.tracker.Track` (reads `.box`, `.label`, `.last_score`, `.last_best_name`).
- Produces:
  - `text_origin(box, frame_height, text_height, above_gap=8) -> (x, y)`.
  - `draw_track(frame, track) -> None` — mutates the frame.
  - `draw_hud(frame, lines: list[str]) -> None`.
  - `draw_banner(frame, text: str) -> None`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_overlay.py`:

```python
from collections import deque

import numpy as np

from src import overlay
from src.tracker import Track


def blank_frame(width=1280, height=720):
    return np.zeros((height, width, 3), dtype=np.uint8)


def make_track(box, label=None, score=0.0, best_name=None):
    track = Track(id=1, box=box, votes=deque(maxlen=5))
    track.last_score = score
    track.last_best_name = best_name
    if label is not None:
        track.votes.append(label)
    return track


def test_label_sits_above_a_face_with_room_over_it():
    _, y = overlay.text_origin((100, 300, 80, 80), frame_height=720, text_height=14)
    assert y < 300


def test_label_moves_below_a_face_at_the_top_edge():
    _, y = overlay.text_origin((100, 2, 80, 80), frame_height=720, text_height=14)
    assert y > 2 + 80


def test_label_never_leaves_the_bottom_of_the_frame():
    _, y = overlay.text_origin((100, 0, 80, 719), frame_height=720, text_height=14)
    assert y < 720


def test_label_keeps_the_x_of_the_box():
    x, _ = overlay.text_origin((137, 300, 80, 80), frame_height=720, text_height=14)
    assert x == 137


def test_drawing_a_known_face_marks_the_frame():
    frame = blank_frame()
    overlay.draw_track(frame, make_track((100, 100, 80, 80), label="Alice", score=0.61))
    assert frame.any()


def test_drawing_an_unknown_face_marks_the_frame():
    frame = blank_frame()
    overlay.draw_track(
        frame, make_track((100, 100, 80, 80), score=0.34, best_name="Alice")
    )
    assert frame.any()


def test_drawing_a_face_at_the_edge_does_not_raise():
    frame = blank_frame()
    overlay.draw_track(frame, make_track((0, 0, 60, 60), label="Alice", score=0.5))
    overlay.draw_track(frame, make_track((1220, 660, 60, 60), label="Bob", score=0.5))


def test_hud_marks_the_frame():
    frame = blank_frame()
    overlay.draw_hud(frame, ["30.0 fps   faces: 1", "enrolled: 2   threshold: 0.40"])
    assert frame.any()


def test_banner_marks_the_frame():
    frame = blank_frame()
    overlay.draw_banner(frame, 'Enrolling "Alice" - 3/5 samples')
    assert frame.any()
```

- [ ] **Step 2: Sync and run the tests to verify they fail**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/test_overlay.py -v"
```

Expected: `ModuleNotFoundError: No module named 'src.overlay'`.

- [ ] **Step 3: Create `src/overlay.py`**

```python
"""All drawing. No state, no model calls, no decisions about identity.

The overlay deliberately shows the raw similarity score next to every face. That
turns "why doesn't it recognise me" from guesswork into a reading: if your face
sits at 0.34 and the threshold is 0.40, you know exactly what to change.
"""
from __future__ import annotations

import cv2

GREEN = (0, 200, 0)
RED = (0, 0, 220)
GREY = (160, 160, 160)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

FONT = cv2.FONT_HERSHEY_SIMPLEX
LABEL_SCALE = 0.6
LABEL_THICKNESS = 1
SUB_SCALE = 0.45
BOX_THICKNESS = 2
BANNER_HEIGHT = 44


def text_origin(box, frame_height: int, text_height: int, above_gap: int = 8):
    """Where to put a label so it stays on screen when the face is at an edge.

    Above the box normally; below it when the face is against the top of the
    frame; clamped inside the frame either way."""
    x, y, _, h = box
    if y - above_gap - text_height >= 0:
        return x, y - above_gap
    return x, min(y + h + above_gap + text_height, frame_height - 2)


def draw_track(frame, track) -> None:
    x, y, w, h = track.box
    known = track.label is not None
    colour = GREEN if known else RED

    cv2.rectangle(frame, (x, y), (x + w, y + h), colour, BOX_THICKNESS)

    if known:
        caption = f"{track.label} {track.last_score:.2f}"
        subcaption = None
    else:
        caption = "Unknown"
        subcaption = (
            f"best: {track.last_best_name} {track.last_score:.2f}"
            if track.last_best_name
            else None
        )

    (text_width, text_height), _ = cv2.getTextSize(
        caption, FONT, LABEL_SCALE, LABEL_THICKNESS
    )
    tx, ty = text_origin(track.box, frame.shape[0], text_height)

    cv2.rectangle(
        frame, (tx, ty - text_height - 4), (tx + text_width + 6, ty + 4), BLACK, cv2.FILLED
    )
    cv2.putText(
        frame, caption, (tx + 3, ty), FONT, LABEL_SCALE, colour, LABEL_THICKNESS, cv2.LINE_AA
    )

    if subcaption:
        cv2.putText(
            frame, subcaption, (tx + 3, ty + 16), FONT, SUB_SCALE, GREY, 1, cv2.LINE_AA
        )


def draw_hud(frame, lines) -> None:
    y = 24
    for line in lines:
        cv2.putText(frame, line, (12, y), FONT, 0.5, WHITE, 1, cv2.LINE_AA)
        y += 20


def draw_banner(frame, text: str) -> None:
    height, width = frame.shape[:2]
    cv2.rectangle(frame, (0, 0), (width, BANNER_HEIGHT), BLACK, cv2.FILLED)
    cv2.putText(frame, text, (16, 30), FONT, 0.7, WHITE, 2, cv2.LINE_AA)
```

- [ ] **Step 4: Sync and run the tests to verify they pass**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/ -v"
```

Expected: 34 passed.

- [ ] **Step 5: Commit**

```bash
git add RasberiPi/face-recognition/src/overlay.py RasberiPi/face-recognition/tests/test_overlay.py
git commit -m "feat: overlay drawing for boxes, labels, HUD and banner"
```

---

## Task 5: The models — detection and embedding

The first task that touches the neural networks. The most likely failure in the whole project is here: the current YuNet model needs OpenCV 4.8 or newer, and Raspberry Pi OS Bookworm ships 4.6. The download script picks the model to match.

**Files:**
- Create: `download_models.sh`
- Create: `src/detector.py`
- Create: `src/recognizer.py`
- Create: `tools/grab_still.py`
- Test: `tests/test_detector.py`

**Interfaces:**
- Consumes: `src.camera.open_source`, `src.config.CONFIG`.
- Produces:
  - `src.detector.ModelError` — raised for a missing or unloadable model file.
  - `src.detector.Detection(box: tuple[int,int,int,int], score: float, row: np.ndarray)`. `row` is YuNet's 15-value output rescaled to full-frame coordinates, which is what `SFace.alignCrop()` expects.
  - `src.detector.scale_row(row, scale_x, scale_y) -> np.ndarray`.
  - `src.detector.Detector(model_path, detect_size, score_threshold, nms_threshold, top_k)` with `.detect(frame) -> list[Detection]`.
  - `src.recognizer.Recognizer(model_path)` with `.embed(frame, detection) -> np.ndarray` (128 floats, unit length).

- [ ] **Step 1: Create `download_models.sh`**

```bash
#!/usr/bin/env bash
# Download the two ONNX models into models/, choosing the YuNet version that
# the installed OpenCV can actually load.
#
# Note the host: opencv_zoo keeps its models in Git LFS, and
# raw.githubusercontent.com serves a 130-byte text pointer for those instead of
# the file. media.githubusercontent.com/media/... resolves LFS properly.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p models

BASE="https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models"

OPENCV_VERSION="$(python3 -c 'import cv2; print(cv2.__version__)')"
MAJOR="${OPENCV_VERSION%%.*}"
REST="${OPENCV_VERSION#*.}"
MINOR="${REST%%.*}"

# The 2023mar YuNet changed its input and output shapes and needs OpenCV 4.8+.
if [ "$MAJOR" -gt 4 ] || { [ "$MAJOR" -eq 4 ] && [ "$MINOR" -ge 8 ]; }; then
  YUNET="face_detection_yunet_2023mar.onnx"
else
  YUNET="face_detection_yunet_2022mar.onnx"
fi
SFACE="face_recognition_sface_2021dec.onnx"

echo "OpenCV $OPENCV_VERSION -> $YUNET"

fetch() {
  url="$1"
  name="$2"
  min_bytes="$3"

  if [ -s "models/$name" ] && [ "$(stat -c%s "models/$name")" -ge "$min_bytes" ]; then
    echo "already have $name"
    return
  fi

  echo "downloading $name"
  curl -fsSL --retry 3 -o "models/$name.part" "$url"

  # A Git LFS pointer is valid text and a successful HTTP 200, so nothing fails
  # until OpenCV tries to parse it three steps later. Catch it here instead.
  if head -c 40 "models/$name.part" | grep -q "git-lfs"; then
    rm -f "models/$name.part"
    echo "ERROR: got a Git LFS pointer instead of $name." >&2
    echo "The URL must resolve LFS content (media.githubusercontent.com/media/...)." >&2
    exit 1
  fi

  actual="$(stat -c%s "models/$name.part")"
  if [ "$actual" -lt "$min_bytes" ]; then
    rm -f "models/$name.part"
    echo "ERROR: $name is only $actual bytes, expected at least $min_bytes." >&2
    exit 1
  fi

  mv "models/$name.part" "models/$name"
}

fetch "$BASE/face_detection_yunet/$YUNET" "$YUNET" 200000
fetch "$BASE/face_recognition_sface/$SFACE" "$SFACE" 30000000

# The code always opens models/yunet.onnx and models/sface.onnx, so the model
# version is a deployment detail rather than something baked into the source.
ln -sf "$YUNET" models/yunet.onnx
ln -sf "$SFACE" models/sface.onnx

echo
ls -l models/
```

- [ ] **Step 2: Run the download and check the file sizes**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && ./download_models.sh"
```

Expected: two downloads, then a listing showing the YuNet file at roughly 230 KB and the SFace file at roughly 37 MB, plus the two symlinks.

If `curl` reports a 404, the model files have been renamed in the `opencv/opencv_zoo` repository. Browse `https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet` and `.../face_recognition_sface`, take the current filenames, and update `BASE`/`YUNET`/`SFACE` in the script. Do not continue with a zero-byte model file — it fails later with a confusing ONNX error.

- [ ] **Step 3: Write the failing test for coordinate rescaling**

This is the one part of detection that is pure arithmetic and worth testing directly: results come back in 640×360 space and must be reported in 1280×720 space, or every box will be drawn in the wrong place.

Create `tests/test_detector.py`:

```python
import os

import numpy as np
import pytest

from src.detector import Detector, ModelError, scale_row

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
YUNET = os.path.join(PROJECT_ROOT, "models", "yunet.onnx")
SFACE = os.path.join(PROJECT_ROOT, "models", "sface.onnx")
FIXTURE = os.path.join(PROJECT_ROOT, "tests", "fixtures", "face.jpg")


def yunet_row(x, y, w, h, score=0.9):
    """A YuNet result row: box, then five landmark x/y pairs, then the score."""
    row = np.arange(15, dtype=np.float32)
    row[0:4] = (x, y, w, h)
    row[14] = score
    return row


def test_scale_row_doubles_the_box():
    scaled = scale_row(yunet_row(10, 20, 30, 40), scale_x=2.0, scale_y=2.0)
    assert list(scaled[:4]) == [20.0, 40.0, 60.0, 80.0]


def test_scale_row_scales_x_and_y_independently():
    scaled = scale_row(yunet_row(10, 20, 30, 40), scale_x=2.0, scale_y=3.0)
    assert list(scaled[:4]) == [20.0, 60.0, 60.0, 120.0]


def test_scale_row_scales_all_five_landmarks():
    row = yunet_row(0, 0, 10, 10)
    row[4:14] = 1.0

    scaled = scale_row(row, scale_x=2.0, scale_y=4.0)

    assert list(scaled[4:14:2]) == [2.0] * 5   # landmark x values
    assert list(scaled[5:14:2]) == [4.0] * 5   # landmark y values


def test_scale_row_leaves_the_score_alone():
    scaled = scale_row(yunet_row(10, 20, 30, 40, score=0.77), 2.0, 2.0)
    assert scaled[14] == pytest.approx(0.77)


def test_scale_row_does_not_modify_its_input():
    row = yunet_row(10, 20, 30, 40)
    scale_row(row, 2.0, 2.0)
    assert row[0] == 10.0


def test_a_missing_model_file_says_what_to_run():
    with pytest.raises(ModelError, match="download_models.sh"):
        Detector("models/does-not-exist.onnx", (640, 360), 0.85, 0.3, 50)


@pytest.mark.skipif(
    not os.path.exists(YUNET) or not os.path.exists(FIXTURE),
    reason="needs the downloaded models and a face fixture from tools/grab_still.py",
)
def test_a_real_face_is_detected_and_embedded():
    import cv2

    from src.recognizer import Recognizer

    frame = cv2.imread(FIXTURE)
    detector = Detector(YUNET, (640, 360), 0.85, 0.3, 50)
    recognizer = Recognizer(SFACE)

    detections = detector.detect(frame)

    assert len(detections) >= 1
    x, y, w, h = detections[0].box
    assert 0 <= x < frame.shape[1] and 0 <= y < frame.shape[0]
    assert w > 40 and h > 40

    embedding = recognizer.embed(frame, detections[0])

    assert embedding.shape == (128,)
    assert float(np.linalg.norm(embedding)) == pytest.approx(1.0, abs=1e-4)
```

- [ ] **Step 4: Sync and run the tests to verify they fail**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/test_detector.py -v"
```

Expected: `ModuleNotFoundError: No module named 'src.detector'`.

- [ ] **Step 5: Create `src/detector.py`**

```python
"""YuNet face detection.

Detection runs on a small copy of the frame because it costs a quarter as much
there, but every coordinate this module returns is in full-frame space. Callers
never need to know that a smaller image was involved.
"""
from __future__ import annotations

import os
from dataclasses import dataclass

import cv2
import numpy as np

BOX_AND_LANDMARK_VALUES = 14  # indices 0..13: x, y, w, h then five x/y pairs
SCORE_INDEX = 14


class ModelError(RuntimeError):
    """A model file is missing, or this OpenCV cannot load it."""


@dataclass(frozen=True)
class Detection:
    box: tuple[int, int, int, int]  # x, y, w, h in full-frame coordinates
    score: float
    row: np.ndarray                 # YuNet's 15 values, rescaled to the full
                                    # frame; SFace.alignCrop() consumes this


def scale_row(row, scale_x: float, scale_y: float) -> np.ndarray:
    """Rescale a YuNet result row from detection space to frame space.

    The row is [x, y, w, h, then five landmark x/y pairs, then the score], so
    every even index up to 13 is a horizontal value and every odd one vertical.
    The score at index 14 is left alone."""
    scaled = np.array(row, dtype=np.float32)
    scaled[0:BOX_AND_LANDMARK_VALUES:2] *= scale_x
    scaled[1:BOX_AND_LANDMARK_VALUES:2] *= scale_y
    return scaled


class Detector:
    def __init__(
        self,
        model_path: str,
        detect_size: tuple[int, int],
        score_threshold: float,
        nms_threshold: float,
        top_k: int,
    ) -> None:
        if not os.path.exists(model_path):
            raise ModelError(
                f"Missing model {model_path}.\nRun:  ./download_models.sh"
            )
        try:
            self._model = cv2.FaceDetectorYN.create(
                model=model_path,
                config="",
                input_size=detect_size,
                score_threshold=score_threshold,
                nms_threshold=nms_threshold,
                top_k=top_k,
            )
        except cv2.error as exc:
            raise ModelError(
                f"OpenCV {cv2.__version__} could not load "
                f"{os.path.basename(os.path.realpath(model_path))}.\n"
                "The YuNet 2023mar model needs OpenCV 4.8 or newer; older OpenCV "
                "needs the 2022mar model.\n"
                "Delete models/ and re-run ./download_models.sh"
            ) from exc
        self._detect_size = detect_size

    def detect(self, frame) -> list[Detection]:
        height, width = frame.shape[:2]
        small = cv2.resize(frame, self._detect_size)

        _, faces = self._model.detect(small)
        if faces is None:
            return []

        scale_x = width / self._detect_size[0]
        scale_y = height / self._detect_size[1]

        detections = []
        for face in faces:
            row = scale_row(face, scale_x, scale_y)
            x, y, w, h = (int(round(float(value))) for value in row[:4])
            detections.append(
                Detection(box=(x, y, w, h), score=float(row[SCORE_INDEX]), row=row)
            )
        return detections
```

- [ ] **Step 6: Create `src/recognizer.py`**

```python
"""SFace embeddings: one detected face becomes 128 numbers.

The model never learns anyone's name. It only learns to place two pictures of
the same person close together and two different people far apart. Naming is
what src/face_database.py does with the result.
"""
from __future__ import annotations

import os

import cv2
import numpy as np

from src.detector import ModelError


class Recognizer:
    def __init__(self, model_path: str) -> None:
        if not os.path.exists(model_path):
            raise ModelError(
                f"Missing model {model_path}.\nRun:  ./download_models.sh"
            )
        try:
            self._model = cv2.FaceRecognizerSF.create(model=model_path, config="")
        except cv2.error as exc:
            raise ModelError(
                f"OpenCV {cv2.__version__} could not load {os.path.basename(model_path)}."
            ) from exc

    def embed(self, frame, detection) -> np.ndarray:
        """Align the face to 112x112 using its landmarks, then embed it.

        Cropping from the full-resolution frame rather than the small detection
        copy is what makes the alignment worth doing."""
        aligned = self._model.alignCrop(frame, detection.row)
        feature = self._model.feature(aligned)

        vector = np.asarray(feature, dtype=np.float32).flatten()
        norm = float(np.linalg.norm(vector))
        return (vector / norm).astype(np.float32) if norm else vector
```

- [ ] **Step 7: Create `tools/grab_still.py`**

The device test needs a photograph of a real face taken by this camera. It is gitignored, because it is biometric data.

```python
"""Save one camera frame as a test fixture.

    ./run.sh tools/grab_still.py

Sit in front of the camera and run it. Writes tests/fixtures/face.jpg, which
tests/test_detector.py uses to check that detection and embedding really work.
"""
import os
import sys

import cv2

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.camera import CameraError, open_source  # noqa: E402
from src.config import CONFIG  # noqa: E402

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(PROJECT_ROOT, "tests", "fixtures", "face.jpg")
WARMUP_FRAMES = 15  # let auto-exposure settle before keeping a frame


def main() -> int:
    try:
        source = open_source("camera", CONFIG.capture_width, CONFIG.capture_height)
    except CameraError as error:
        print(error, file=sys.stderr)
        return 1

    try:
        frame = None
        for _ in range(WARMUP_FRAMES):
            frame = source.read()
    finally:
        source.close()

    if frame is None:
        print("Camera produced no frames.", file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    cv2.imwrite(OUTPUT, frame)
    print(f"Wrote {OUTPUT}  ({frame.shape[1]}x{frame.shape[0]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 8: Capture a fixture and run the tests**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

Sit in front of the camera, then:

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && ./run.sh tools/grab_still.py"
```

Expected: `Wrote /home/admin/face-recognition/tests/fixtures/face.jpg  (1280x720)`.

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/ -v"
```

Expected: 41 passed, with `test_a_real_face_is_detected_and_embedded` among them rather than skipped. A skip means the fixture or the models are missing. A failure on that test with zero detections means the face was too small, too dark, or too far off-centre — retake the fixture.

- [ ] **Step 9: Commit**

```bash
git add RasberiPi/face-recognition/download_models.sh RasberiPi/face-recognition/src/detector.py RasberiPi/face-recognition/src/recognizer.py RasberiPi/face-recognition/tools/grab_still.py RasberiPi/face-recognition/tests/test_detector.py
git commit -m "feat: YuNet detection and SFace embedding"
```

---

## Task 6: Enrollment

The quality gate here is what keeps the database trustworthy. A single bad sample — a blurred face, someone walking past in the background — produces false matches later that are very hard to diagnose, so bad frames are refused at the point of entry.

**Files:**
- Create: `src/enroll.py`
- Test: `tests/test_enroll.py`

**Interfaces:**
- Consumes: nothing at import time. `check_frame` takes any objects with `.score` and `.box`, so it needs no model.
- Produces:
  - `SampleCheck(accepted: bool, reason: str)`.
  - `check_frame(detections, min_score, min_face_px) -> SampleCheck`.
  - `drop_outliers(embeddings, min_similarity) -> list[np.ndarray]`.
  - `EnrollmentSession(name, target_samples, min_score, min_face_px)` with `.offer(detections, embed) -> bool`, `.done -> bool`, `.finish(min_similarity, min_accepted) -> (list | None, str)`, `.status() -> str`, and the attributes `.name`, `.samples`, `.last_reason`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_enroll.py`:

```python
from dataclasses import dataclass

import numpy as np
import pytest

from src.enroll import EnrollmentSession, check_frame, drop_outliers

MIN_SCORE = 0.9
MIN_FACE_PX = 100
MIN_SIMILARITY = 0.5
MIN_ACCEPTED = 3


@dataclass
class FakeDetection:
    box: tuple
    score: float


def good_detection():
    return FakeDetection(box=(100, 100, 150, 150), score=0.95)


def unit(*values):
    vector = np.zeros(128, dtype=np.float32)
    for index, value in enumerate(values):
        vector[index] = value
    norm = np.linalg.norm(vector)
    return vector / norm if norm else vector


def test_a_good_frame_is_accepted():
    assert check_frame([good_detection()], MIN_SCORE, MIN_FACE_PX).accepted


def test_no_face_is_rejected():
    check = check_frame([], MIN_SCORE, MIN_FACE_PX)
    assert not check.accepted
    assert "no face" in check.reason


def test_two_faces_are_rejected():
    check = check_frame([good_detection(), good_detection()], MIN_SCORE, MIN_FACE_PX)
    assert not check.accepted
    assert "one face" in check.reason


def test_a_low_confidence_face_is_rejected():
    detection = FakeDetection(box=(100, 100, 150, 150), score=0.5)
    check = check_frame([detection], MIN_SCORE, MIN_FACE_PX)
    assert not check.accepted
    assert "clear" in check.reason


def test_a_small_face_is_rejected():
    detection = FakeDetection(box=(100, 100, 60, 60), score=0.95)
    check = check_frame([detection], MIN_SCORE, MIN_FACE_PX)
    assert not check.accepted
    assert "closer" in check.reason


def test_consistent_samples_all_survive():
    samples = [unit(1.0, 0.05 * n) for n in range(5)]
    assert len(drop_outliers(samples, MIN_SIMILARITY)) == 5


def test_the_odd_sample_out_is_dropped():
    samples = [unit(1.0), unit(1.0), unit(1.0), unit(1.0), unit(0.0, 1.0)]
    kept = drop_outliers(samples, MIN_SIMILARITY)
    assert len(kept) == 4


def test_too_few_samples_to_judge_are_all_kept():
    samples = [unit(1.0), unit(0.0, 1.0)]
    assert len(drop_outliers(samples, MIN_SIMILARITY)) == 2


def test_a_session_collects_until_it_is_done():
    session = EnrollmentSession("Alice", 3, MIN_SCORE, MIN_FACE_PX)

    for _ in range(3):
        assert session.offer([good_detection()], lambda detection: unit(1.0))

    assert session.done
    assert len(session.samples) == 3


def test_a_session_refuses_a_bad_frame_without_embedding_it():
    session = EnrollmentSession("Alice", 3, MIN_SCORE, MIN_FACE_PX)

    def embed(detection):
        raise AssertionError("must not embed a rejected frame")

    assert not session.offer([], embed)
    assert session.samples == []
    assert "no face" in session.last_reason


def test_finish_returns_the_kept_samples():
    session = EnrollmentSession("Alice", 4, MIN_SCORE, MIN_FACE_PX)
    for _ in range(4):
        session.offer([good_detection()], lambda detection: unit(1.0))

    kept, reason = session.finish(MIN_SIMILARITY, MIN_ACCEPTED)

    assert kept is not None
    assert len(kept) == 4
    assert reason == ""


def test_finish_refuses_when_too_few_samples_survive():
    # Five samples pointing in five different directions: no group for any of
    # them to agree with, so every one is dropped as an outlier.
    session = EnrollmentSession("Alice", 5, MIN_SCORE, MIN_FACE_PX)
    vectors = iter([
        unit(1.0),
        unit(0.0, 1.0),
        unit(0.0, 0.0, 1.0),
        unit(0.0, 0.0, 0.0, 1.0),
        unit(0.0, 0.0, 0.0, 0.0, 1.0),
    ])
    for _ in range(5):
        session.offer([good_detection()], lambda detection: next(vectors))

    kept, reason = session.finish(MIN_SIMILARITY, MIN_ACCEPTED)

    assert kept is None
    assert "need 3" in reason


def test_status_shows_progress():
    session = EnrollmentSession("Alice", 5, MIN_SCORE, MIN_FACE_PX)
    session.offer([good_detection()], lambda detection: unit(1.0))

    assert "Alice" in session.status()
    assert "1/5" in session.status()


def test_status_shows_why_a_frame_was_refused():
    session = EnrollmentSession("Alice", 5, MIN_SCORE, MIN_FACE_PX)
    session.offer([], lambda detection: unit(1.0))

    assert "no face" in session.status()
```

- [ ] **Step 2: Sync and run the tests to verify they fail**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/test_enroll.py -v"
```

Expected: `ModuleNotFoundError: No module named 'src.enroll'`.

- [ ] **Step 3: Create `src/enroll.py`**

```python
"""Collecting a few good samples of one person's face.

Everything here exists to keep bad data out of the database. A blurred face or
somebody walking past in the background, accepted once during enrollment, causes
false matches for weeks afterwards and gives no clue where they came from.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

import numpy as np


@dataclass(frozen=True)
class SampleCheck:
    accepted: bool
    reason: str  # empty when accepted; shown on screen otherwise


def check_frame(detections, min_score: float, min_face_px: int) -> SampleCheck:
    """Decide whether this frame may contribute an enrollment sample."""
    if len(detections) == 0:
        return SampleCheck(False, "no face in view")
    if len(detections) > 1:
        return SampleCheck(False, "more than one face in view")

    detection = detections[0]
    if detection.score < min_score:
        return SampleCheck(False, "face not clear enough")
    if detection.box[2] < min_face_px:
        return SampleCheck(False, "move closer")

    return SampleCheck(True, "")


def drop_outliers(embeddings, min_similarity: float) -> list:
    """Discard samples that disagree with the rest of the group.

    With two samples or fewer there is no majority to disagree with, so
    everything is kept."""
    if len(embeddings) <= 2:
        return list(embeddings)

    centre = np.vstack(embeddings).mean(axis=0)
    norm = float(np.linalg.norm(centre))
    if norm == 0.0:
        return list(embeddings)
    centre = centre / norm

    return [
        embedding
        for embedding in embeddings
        if float(np.dot(embedding, centre)) >= min_similarity
    ]


class EnrollmentSession:
    """Collects samples for one person over successive frames.

    The session never touches the database; the caller decides what to do with
    the samples it hands back."""

    def __init__(
        self, name: str, target_samples: int, min_score: float, min_face_px: int
    ) -> None:
        self.name = name
        self.target = target_samples
        self.samples: list = []
        self.last_reason = ""
        self._min_score = min_score
        self._min_face_px = min_face_px

    @property
    def done(self) -> bool:
        return len(self.samples) >= self.target

    def offer(self, detections, embed) -> bool:
        """Try to take one sample from this frame.

        `embed` is called only once the frame has passed the quality gate, so a
        rejected frame costs nothing."""
        check = check_frame(detections, self._min_score, self._min_face_px)
        self.last_reason = check.reason
        if not check.accepted:
            return False
        self.samples.append(embed(detections[0]))
        return True

    def finish(self, min_similarity: float, min_accepted: int):
        """Return (samples, "") on success, or (None, reason) on failure."""
        kept = drop_outliers(self.samples, min_similarity)
        if len(kept) < min_accepted:
            return None, f"only {len(kept)} good samples, need {min_accepted}"
        return kept, ""

    def status(self) -> str:
        progress = f'Enrolling "{self.name}" - {len(self.samples)}/{self.target} samples'
        return f"{progress}  ({self.last_reason})" if self.last_reason else progress
```

- [ ] **Step 4: Sync and run the tests to verify they pass**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/ -v"
```

Expected: 55 passed.

- [ ] **Step 5: Commit**

```bash
git add RasberiPi/face-recognition/src/enroll.py RasberiPi/face-recognition/tests/test_enroll.py
git commit -m "feat: enrollment with a sample quality gate"
```

---

## Task 7: The LED

Small, but it is the only module that touches hardware other than the camera, and it must never be the reason the app crashes. It also needs an off-delay: without one the LED flickers every time a label momentarily drops.

**Wiring:** the LED's long leg (anode) goes through a ~330 Ω resistor to physical pin 11 (BCM GPIO 17); the short leg (cathode) goes to any ground pin, for example physical pin 9.

**Files:**
- Create: `src/led.py`
- Test: `tests/test_led.py`

**Interfaces:**
- Consumes: `src.config.CONFIG` (by the caller).
- Produces:
  - `NullLed` with `.set(on)` and `.close()`, both doing nothing.
  - `HoldingLed(backend, hold_ms, now=time.monotonic)` with `.set(on)` and `.close()`. `backend` is anything with `.on()`, `.off()` and `.close()`.
  - `create_led(enabled: bool, pin: int, hold_ms: int)` returning one of the two.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_led.py`:

```python
from src.led import HoldingLed, NullLed, create_led

HOLD_MS = 500


class FakeBackend:
    def __init__(self):
        self.events = []

    def on(self):
        self.events.append("on")

    def off(self):
        self.events.append("off")

    def close(self):
        self.events.append("close")


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def advance(self, seconds):
        self.now += seconds


def test_turning_on_reaches_the_hardware_immediately():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)

    led.set(True)

    assert backend.events == ["on"]


def test_staying_on_does_not_repeat_the_command():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)

    led.set(True)
    led.set(True)
    led.set(True)

    assert backend.events == ["on"]


def test_a_brief_dropout_does_not_turn_the_led_off():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)
    led.set(True)

    led.set(False)          # starts the hold
    clock.advance(0.2)
    led.set(False)
    led.set(True)           # back before the hold expired

    assert backend.events == ["on"]


def test_the_led_turns_off_once_the_hold_expires():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)
    led.set(True)

    led.set(False)          # starts the hold
    clock.advance(0.6)
    led.set(False)          # hold expired

    assert backend.events == ["on", "off"]


def test_setting_off_on_an_already_off_led_does_nothing():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)

    led.set(False)
    clock.advance(10.0)
    led.set(False)

    assert backend.events == []


def test_closing_turns_a_lit_led_off():
    backend, clock = FakeBackend(), FakeClock()
    led = HoldingLed(backend, HOLD_MS, now=clock)
    led.set(True)

    led.close()

    assert backend.events == ["on", "off", "close"]


def test_a_disabled_led_is_a_null_led():
    led = create_led(enabled=False, pin=17, hold_ms=HOLD_MS)

    assert isinstance(led, NullLed)
    led.set(True)
    led.close()
```

- [ ] **Step 2: Sync and run the tests to verify they fail**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/test_led.py -v"
```

Expected: `ModuleNotFoundError: No module named 'src.led'`.

- [ ] **Step 3: Create `src/led.py`**

```python
"""The known-face LED.

Wiring: LED anode -> ~330 ohm resistor -> physical pin 11 (BCM GPIO 17);
cathode -> any ground pin, e.g. physical pin 9.

A missing wire, a missing library or a busy pin must never stop the app: in any
of those cases this falls back to a light that does nothing.
"""
from __future__ import annotations

import sys
import time


class NullLed:
    """Used when the LED is disabled or the GPIO is unavailable."""

    def set(self, on: bool) -> None:
        pass

    def close(self) -> None:
        pass


class HoldingLed:
    """On immediately; off only after `hold_ms` of continuous off requests.

    The hold exists because recognition drops out for a frame or two quite
    normally - without it the LED strobes while somebody sits still.

    `set` is expected to be called every frame; the turn-off happens on a later
    call, not on a timer of its own.
    """

    def __init__(self, backend, hold_ms: int, now=time.monotonic) -> None:
        self._backend = backend
        self._hold_seconds = hold_ms / 1000.0
        self._now = now
        self._on = False
        self._off_requested_at = None

    def set(self, on: bool) -> None:
        if on:
            self._off_requested_at = None
            if not self._on:
                self._backend.on()
                self._on = True
            return

        if not self._on:
            return

        if self._off_requested_at is None:
            self._off_requested_at = self._now()
        elif self._now() - self._off_requested_at >= self._hold_seconds:
            self._backend.off()
            self._on = False
            self._off_requested_at = None

    def close(self) -> None:
        if self._on:
            self._backend.off()
            self._on = False
        self._backend.close()


def create_led(enabled: bool, pin: int, hold_ms: int):
    if not enabled:
        return NullLed()

    try:
        from gpiozero import LED
    except ImportError:
        print("gpiozero is not installed; running without the LED.", file=sys.stderr)
        return NullLed()

    try:
        return HoldingLed(LED(pin), hold_ms)
    except Exception as error:
        print(f"Could not use GPIO {pin} ({error}); running without the LED.",
              file=sys.stderr)
        return NullLed()
```

- [ ] **Step 4: Sync and run the tests to verify they pass**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/ -v"
```

Expected: 62 passed.

- [ ] **Step 5: Commit**

```bash
git add RasberiPi/face-recognition/src/led.py RasberiPi/face-recognition/tests/test_led.py
git commit -m "feat: known-face LED with an off-hold and a safe fallback"
```

---

## Task 8: The application

Everything built so far, wired into one loop. This task has no unit tests — it is verified by running it — which is precisely why every decision it makes was pushed into a tested module first.

**Files:**
- Create: `src/main.py`

**Interfaces:**
- Consumes: every module built so far.
- Produces: `python3 -m src.main [--source camera|file:<path>] [--headless]`, launched through `./run.sh`.

- [ ] **Step 1: Create `src/main.py`**

```python
"""Live face recognition on the Raspberry Pi 5.

    ./run.sh -m src.main
    ./run.sh -m src.main --source file:tests/fixtures/face.jpg --headless

Commands are typed into this terminal, a single letter then Enter, because the
Pi is reached over SSH and has no keyboard of its own. The same letters also
work at the window for anyone who does attach one.

    q   quit
    e   enroll a person - the name is typed at the prompt that follows
    f   toggle fullscreen
    h   toggle the HUD
"""
from __future__ import annotations

import argparse
import os
import select
import sys
import time

import cv2

from src import overlay
from src.camera import CameraError, open_source
from src.config import CONFIG
from src.detector import Detector, ModelError
from src.enroll import EnrollmentSession
from src.face_database import FaceDatabase
from src.led import create_led
from src.recognizer import Recognizer
from src.tracker import Tracker

WINDOW = "Face Recognition"
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MESSAGE_SECONDS = 3.0
MAX_EMPTY_READS = 30
HELP = "Commands (type here, then Enter):  e = enroll   q = quit   h = hud   f = fullscreen"


def read_terminal_command():
    """One typed command from the terminal, or None if nothing was typed.

    This is what makes the app usable over SSH: the window never receives a
    keypress, because there is no keyboard attached to the Pi. select() with a
    zero timeout keeps the video running while nothing is being typed."""
    if not sys.stdin.isatty():
        return None
    ready, _, _ = select.select([sys.stdin], [], [], 0)
    if not ready:
        return None
    line = sys.stdin.readline()
    if not line:
        return None
    return line.strip().lower()[:1] or None


def key_to_command(key: int):
    """Translate an OpenCV keycode into the same command letters."""
    if key in (27, ord("q")):
        return "q"
    if 32 <= key < 127:
        return chr(key).lower()
    return None


def parse_args():
    parser = argparse.ArgumentParser(description="Live face recognition")
    parser.add_argument(
        "--source",
        default="camera",
        help='"camera" (default), or "file:<path to an image or directory>"',
    )
    parser.add_argument(
        "--headless",
        action="store_true",
        help="do not open a window; print recognitions to the terminal instead",
    )
    return parser.parse_args()


def display_available() -> bool:
    return bool(os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY"))


def explain_missing_display() -> None:
    print(
        "No display found, so there is nothing to draw on.\n"
        "To put the window on the HDMI monitor, start the app with:\n"
        "    ./run.sh -m src.main\n"
        "or set these yourself first:\n"
        f"    export XDG_RUNTIME_DIR=/run/user/{os.getuid()}\n"
        "    export WAYLAND_DISPLAY=wayland-0\n"
        "Continuing without a window.\n",
        file=sys.stderr,
    )


def main() -> int:
    args = parse_args()
    headless = args.headless

    if not headless and not display_available():
        explain_missing_display()
        headless = True

    models_dir = os.path.join(PROJECT_ROOT, CONFIG.models_dir)
    data_dir = os.path.join(PROJECT_ROOT, CONFIG.data_dir)

    try:
        detector = Detector(
            os.path.join(models_dir, "yunet.onnx"),
            (CONFIG.detect_width, CONFIG.detect_height),
            CONFIG.detect_score_threshold,
            CONFIG.detect_nms_threshold,
            CONFIG.detect_top_k,
        )
        recognizer = Recognizer(os.path.join(models_dir, "sface.onnx"))
        source = open_source(args.source, CONFIG.capture_width, CONFIG.capture_height)
    except (ModelError, CameraError) as error:
        print(error, file=sys.stderr)
        return 1

    database = FaceDatabase.load(data_dir, CONFIG.match_threshold, CONFIG.match_margin)
    tracker = Tracker(CONFIG.track_iou_min, CONFIG.track_max_misses, CONFIG.vote_window)
    led = create_led(CONFIG.led_enabled, CONFIG.led_pin, CONFIG.led_hold_ms)

    if not headless:
        cv2.namedWindow(WINDOW, cv2.WINDOW_NORMAL)
        cv2.setWindowProperty(WINDOW, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    print(HELP)

    session = None
    show_hud = True
    fullscreen = True
    frame_number = 0
    empty_reads = 0
    fps = 0.0
    last_time = time.monotonic()
    message = ""
    message_until = 0.0
    printed: dict[int, str] = {}

    try:
        while True:
            frame = source.read()
            if frame is None:
                empty_reads += 1
                if empty_reads > MAX_EMPTY_READS:
                    print("The camera stopped returning frames.", file=sys.stderr)
                    return 1
                continue
            empty_reads = 0
            frame_number += 1

            detections = detector.detect(frame)
            tracks = tracker.update(detections)

            for track in tracks:
                if track.detection is None:
                    continue
                if track.needs_identity(frame_number, CONFIG.reidentify_every):
                    embedding = recognizer.embed(frame, track.detection)
                    result = database.match(embedding)
                    track.record_identity(
                        result.name, result.score, result.best_name, frame_number
                    )

            led.set(any(track.label is not None for track in tracks))

            if session is not None:
                if (
                    not session.done
                    and frame_number % CONFIG.enroll_sample_interval == 0
                ):
                    session.offer(
                        detections, lambda detection: recognizer.embed(frame, detection)
                    )
                if session.done:
                    kept, reason = session.finish(
                        CONFIG.enroll_outlier_similarity, CONFIG.enroll_min_accepted
                    )
                    if kept is None:
                        message = f"Enrollment failed: {reason}"
                    else:
                        database.add(session.name, kept)
                        database.save(data_dir)
                        message = f'Enrolled "{session.name}" ({len(kept)} samples)'
                    print(message)
                    message_until = time.monotonic() + MESSAGE_SECONDS
                    session = None

            command = read_terminal_command()

            if headless:
                for track in tracks:
                    label = track.label or "Unknown"
                    if printed.get(track.id) != label:
                        printed[track.id] = label
                        print(f"track {track.id}: {label} {track.last_score:.2f}")
                if args.source.startswith("file:"):
                    break
            else:
                for track in tracks:
                    overlay.draw_track(frame, track)

                if show_hud:
                    if database.is_empty():
                        second_line = "No one enrolled - press E"
                    else:
                        second_line = (
                            f"enrolled: {len(database.people())}   "
                            f"threshold: {CONFIG.match_threshold:.2f}"
                        )
                    overlay.draw_hud(
                        frame, [f"{fps:4.1f} fps   faces: {len(tracks)}", second_line]
                    )

                if session is not None:
                    overlay.draw_banner(frame, session.status())
                elif message and time.monotonic() < message_until:
                    overlay.draw_banner(frame, message)

                cv2.imshow(WINDOW, frame)

                # waitKey must be called every frame to pump the GUI, whether or
                # not anybody is pressing anything. A key at the window wins over
                # the terminal only because it is read second.
                command = key_to_command(cv2.waitKey(1) & 0xFF) or command

            if command == "q":
                break
            if command == "e" and session is None:
                # This blocks the video until a name is typed. That is a fair
                # trade for not having to write a text field in OpenCV.
                name = input("Name to enroll: ").strip()
                if name:
                    session = EnrollmentSession(
                        name,
                        CONFIG.enroll_samples,
                        CONFIG.enroll_min_det_score,
                        CONFIG.enroll_min_face_px,
                    )
            elif command == "h":
                show_hud = not show_hud
            elif command == "f" and not headless:
                fullscreen = not fullscreen
                cv2.setWindowProperty(
                    WINDOW,
                    cv2.WND_PROP_FULLSCREEN,
                    cv2.WINDOW_FULLSCREEN if fullscreen else cv2.WINDOW_NORMAL,
                )

            now = time.monotonic()
            instant = 1.0 / max(now - last_time, 1e-6)
            fps = instant if fps == 0.0 else 0.9 * fps + 0.1 * instant
            last_time = now
    except KeyboardInterrupt:
        pass
    finally:
        source.close()
        led.close()
        cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Sync and run the headless smoke test**

```bash
powershell -ExecutionPolicy Bypass -File .\sync.ps1
```

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m src.main --source file:tests/fixtures/face.jpg --headless"
```

Expected: one line, `track 1: Unknown 0.00`, then exit. This proves detection, tracking, embedding and matching all run end to end with nobody enrolled yet. A stack trace here is a wiring bug between modules, and is much easier to read without a window in the way.

- [ ] **Step 3: Confirm the whole test suite is still green**

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/ -v"
```

Expected: 62 passed.

- [ ] **Step 4: Commit**

```bash
git add RasberiPi/face-recognition/src/main.py
git commit -m "feat: live recognition application"
```

---

## Task 9: Acceptance on the device, and the README

The point of this task is to prove the thing works as a whole, and to leave behind the notes needed to run it again in three months.

**Files:**
- Create: `README.md`

- [ ] **Step 1: Run the app on the HDMI monitor**

This step and the next must be run by a person in their own terminal, not through an automated SSH call: typing commands needs a real terminal on standard input.

```bash
ssh -t admin@192.168.0.111 "cd /home/admin/face-recognition && ./run.sh -m src.main"
```

Expected: fullscreen video on the monitor, a red box around your face labelled `Unknown`, and the HUD reading `No one enrolled - press E`. The terminal prints the command line.

- [ ] **Step 2: Enroll yourself**

Type `e` and press Enter in that terminal. It asks `Name to enroll:` — the video freezes while it waits. Type your name, press Enter, then sit still facing the camera about half a metre away.

Expected: a banner counting `1/5` … `5/5`, then `Enrolled "<name>" (5 samples)` both on screen and in the terminal. Your box turns green with your name and a score.

If the banner sticks at `0/5` with `move closer`, the face is under 100 px wide — get closer or lower `enroll_min_face_px` in `src/config.py`.

- [ ] **Step 3: Check the acceptance criteria**

Work down this list; each one is a claim the spec makes.

1. Your face shows a green box with your name and a score above 0.40.
2. A second, un-enrolled person shows a red box reading `Unknown`, with a dim `best:` line underneath.
3. The LED is lit while you are in frame and goes out about half a second after you leave.
4. The HUD shows at least 15 fps.
5. The label stays steady rather than flickering between your name and Unknown.
6. Typing `h` hides the HUD; `f` leaves fullscreen; `q` quits cleanly with no traceback.
7. Restart the app: your enrollment survived, and the HUD shows `enrolled: 1`.

If step 1 fails and your score sits just under the threshold, that is the tuning case the HUD was built for: lower `match_threshold` in `src/config.py` a little, or re-enroll under the lighting you actually use.

- [ ] **Step 4: Create `README.md`**

```markdown
# Face Recognition — Raspberry Pi 5

Live face recognition on a Raspberry Pi 5 with an IMX219 camera, shown fullscreen
on the HDMI monitor. Known people are labelled by name; everyone else is Unknown.
An LED lights while a known person is in frame.

**This is not a security system.** There is no anti-spoofing: a photograph of an
enrolled person held up to the camera is recognised as that person.

## How it works

A face is turned into 128 numbers (an "embedding") by OpenCV's SFace model. Two
pictures of the same person produce vectors that point in nearly the same
direction; different people point elsewhere. Recognising somebody is therefore
just measuring an angle — a cosine similarity between 0 and 1 — against the
samples saved during enrollment.

A face is accepted as a known person only when it clears `match_threshold`
**and** beats the second-best person by `match_margin`. Guessing between two
similar-scoring people is worse than saying Unknown.

## Setup on the Pi

```bash
sudo apt update
sudo apt install -y python3-picamera2 python3-opencv python3-numpy python3-gpiozero python3-pytest curl
./download_models.sh
```

## Running

```bash
./run.sh -m src.main
```

`run.sh` sets the display variables so the window lands on the HDMI monitor even
when launched over SSH. Running `python3 -m src.main` directly from SSH will not
show a window.

Other modes:

```bash
./run.sh -m src.main --source file:tests/fixtures/face.jpg --headless   # no camera, no window
./run.sh tools/check_camera.py                                          # camera only, no recognition
./run.sh tools/grab_still.py                                            # save a test fixture
```

## Commands

The Pi has no keyboard of its own, so commands are typed into the SSH terminal
that launched the app — one letter, then Enter. The same letters work at the
window if you do attach a keyboard.

| Command | Action |
|---|---|
| `q` | Quit |
| `e` | Enroll — the name is typed at the prompt that follows (the video pauses meanwhile) |
| `f` | Toggle fullscreen |
| `h` | Toggle the HUD |

## Enrolling well

Enrollment takes five samples and refuses any frame with no face, more than one
face, a low-confidence detection, or a face narrower than 100 px. Samples that
disagree with the others are dropped; fewer than three survivors saves nothing.

Enroll in the light you will actually use. Enrolling the same name twice adds
samples rather than replacing them, so enrolling once with glasses and once
without makes both work.

## Tuning

Everything lives in `src/config.py`.

| Symptom | Change |
|---|---|
| It does not recognise you (score just under the threshold) | Lower `match_threshold` |
| It confuses two people | Raise `match_threshold` or `match_margin` |
| Labels flicker | Raise `vote_window` |
| Frame rate too low | Raise `reidentify_every`, or lower `capture_width`/`capture_height` |
| It refuses to enroll you | Lower `enroll_min_face_px`, or sit closer |

The HUD shows the live score next to every face, so tuning is a reading rather
than a guess.

## Wiring the LED

LED long leg (anode) → ~330 Ω resistor → physical pin 11 (BCM GPIO 17).
LED short leg (cathode) → any ground pin, e.g. physical pin 9.

Set `led_enabled = False` in `src/config.py` to run without one.

## Development

Edit on Windows, then push to the Pi:

```powershell
.\sync.ps1
```

Tests run on the Pi — the Windows machine has no NumPy or OpenCV:

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/ -v"
```

## Troubleshooting

| Problem | Cause |
|---|---|
| No window appears | Started without `./run.sh`; or no Wayland socket in `/run/user/1000/` |
| `No camera found` | Ribbon cable, or the camera is not enabled — check `rpicam-hello --list-cameras` |
| ONNX shape error on startup | The YuNet model does not match this OpenCV — delete `models/` and re-run `./download_models.sh` |
| Faces look blue | Change `"RGB888"` to `"BGR888"` in `src/camera.py` |
| `Missing model` | Run `./download_models.sh` |
```

- [ ] **Step 5: Commit**

```bash
git add RasberiPi/face-recognition/README.md
git commit -m "docs: README with setup, tuning and troubleshooting"
```

---

## Deferred

Not built now, and not to be added without being asked:

- Anti-spoofing / liveness detection.
- A second stream at lower resolution for detection instead of a resize.
- Enrollment from a folder of photographs.
- Logging recognitions, or saving snapshots.
- Deleting or renaming an enrolled person from inside the app (edit `data/` and restart).
- Autostart on boot via systemd.
