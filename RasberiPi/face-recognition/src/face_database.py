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
