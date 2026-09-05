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
