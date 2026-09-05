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
