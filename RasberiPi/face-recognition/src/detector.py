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
