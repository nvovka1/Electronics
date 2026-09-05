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
