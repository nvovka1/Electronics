import os

import numpy as np
import pytest

from src.detector import Detector, ModelError, scale_row

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
YUNET = os.path.join(PROJECT_ROOT, "models", "yunet.onnx")
SFACE = os.path.join(PROJECT_ROOT, "models", "sface.onnx")
FIXTURE = os.path.join(PROJECT_ROOT, "tests", "fixtures", "face.jpg")


def yunet_row(x, y, w, h, score=0.9):
    """A YuNet result row: box, then five landmark x/y pairs, then the score."""
    row = np.arange(15, dtype=np.float32)
    row[0:4] = (x, y, w, h)
    row[14] = score
    return row


def test_scale_row_doubles_the_box():
    scaled = scale_row(yunet_row(10, 20, 30, 40), scale_x=2.0, scale_y=2.0)
    assert list(scaled[:4]) == [20.0, 40.0, 60.0, 80.0]


def test_scale_row_scales_x_and_y_independently():
    scaled = scale_row(yunet_row(10, 20, 30, 40), scale_x=2.0, scale_y=3.0)
    assert list(scaled[:4]) == [20.0, 60.0, 60.0, 120.0]


def test_scale_row_scales_all_five_landmarks():
    row = yunet_row(0, 0, 10, 10)
    row[4:14] = 1.0

    scaled = scale_row(row, scale_x=2.0, scale_y=4.0)

    assert list(scaled[4:14:2]) == [2.0] * 5   # landmark x values
    assert list(scaled[5:14:2]) == [4.0] * 5   # landmark y values


def test_scale_row_leaves_the_score_alone():
    scaled = scale_row(yunet_row(10, 20, 30, 40, score=0.77), 2.0, 2.0)
    assert scaled[14] == pytest.approx(0.77)


def test_scale_row_does_not_modify_its_input():
    row = yunet_row(10, 20, 30, 40)
    scale_row(row, 2.0, 2.0)
    assert row[0] == 10.0


def test_a_missing_model_file_says_what_to_run():
    with pytest.raises(ModelError, match="download_models.sh"):
        Detector("models/does-not-exist.onnx", (640, 360), 0.85, 0.3, 50)


@pytest.mark.skipif(
    not os.path.exists(YUNET) or not os.path.exists(FIXTURE),
    reason="needs the downloaded models and a face fixture from tools/grab_still.py",
)
def test_a_real_face_is_detected_and_embedded():
    import cv2

    from src.recognizer import Recognizer

    frame = cv2.imread(FIXTURE)
    detector = Detector(YUNET, (640, 360), 0.85, 0.3, 50)
    recognizer = Recognizer(SFACE)

    detections = detector.detect(frame)

    assert len(detections) >= 1
    x, y, w, h = detections[0].box
    assert 0 <= x < frame.shape[1] and 0 <= y < frame.shape[0]
    assert w > 40 and h > 40

    embedding = recognizer.embed(frame, detections[0])

    assert embedding.shape == (128,)
    assert float(np.linalg.norm(embedding)) == pytest.approx(1.0, abs=1e-4)
