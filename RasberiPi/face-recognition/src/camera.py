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
