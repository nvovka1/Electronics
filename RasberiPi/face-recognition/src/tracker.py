"""Follows each face from frame to frame, so its label can stay put.

Matching is by box overlap: the same face barely moves between two consecutive
frames, so the box that overlaps most is the same person. This is deliberately
simple - it is not a motion model, and two faces that cross over each other may
swap ids. For a desk-facing camera that is a fair trade for the simplicity.
"""
from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass
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
