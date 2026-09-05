from collections import deque

import numpy as np

from src import overlay
from src.tracker import Track


def blank_frame(width=1280, height=720):
    return np.zeros((height, width, 3), dtype=np.uint8)


def make_track(box, label=None, score=0.0, best_name=None):
    track = Track(id=1, box=box, votes=deque(maxlen=5))
    track.last_score = score
    track.last_best_name = best_name
    if label is not None:
        track.votes.append(label)
    return track


def test_label_sits_above_a_face_with_room_over_it():
    _, y = overlay.text_origin((100, 300, 80, 80), frame_height=720, text_height=14)
    assert y < 300


def test_label_moves_below_a_face_at_the_top_edge():
    _, y = overlay.text_origin((100, 2, 80, 80), frame_height=720, text_height=14)
    assert y > 2 + 80


def test_label_never_leaves_the_bottom_of_the_frame():
    _, y = overlay.text_origin((100, 0, 80, 719), frame_height=720, text_height=14)
    assert y < 720


def test_label_keeps_the_x_of_the_box():
    x, _ = overlay.text_origin((137, 300, 80, 80), frame_height=720, text_height=14)
    assert x == 137


def test_drawing_a_known_face_marks_the_frame():
    frame = blank_frame()
    overlay.draw_track(frame, make_track((100, 100, 80, 80), label="Alice", score=0.61))
    assert frame.any()


def test_drawing_an_unknown_face_marks_the_frame():
    frame = blank_frame()
    overlay.draw_track(
        frame, make_track((100, 100, 80, 80), score=0.34, best_name="Alice")
    )
    assert frame.any()


def test_drawing_a_face_at_the_edge_does_not_raise():
    frame = blank_frame()
    overlay.draw_track(frame, make_track((0, 0, 60, 60), label="Alice", score=0.5))
    overlay.draw_track(frame, make_track((1220, 660, 60, 60), label="Bob", score=0.5))


def test_hud_marks_the_frame():
    frame = blank_frame()
    overlay.draw_hud(frame, ["30.0 fps   faces: 1", "enrolled: 2   threshold: 0.40"])
    assert frame.any()


def test_banner_marks_the_frame():
    frame = blank_frame()
    overlay.draw_banner(frame, 'Enrolling "Alice" - 3/5 samples')
    assert frame.any()
