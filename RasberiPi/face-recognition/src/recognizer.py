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
