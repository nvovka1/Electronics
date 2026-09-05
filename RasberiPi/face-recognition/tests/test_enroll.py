from dataclasses import dataclass

import numpy as np

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
