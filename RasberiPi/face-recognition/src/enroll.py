"""Collecting a few good samples of one person's face.

Everything here exists to keep bad data out of the database. A blurred face or
somebody walking past in the background, accepted once during enrollment, causes
false matches for weeks afterwards and gives no clue where they came from.
"""
from __future__ import annotations

from dataclasses import dataclass

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
