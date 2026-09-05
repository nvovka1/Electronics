"""Every tunable value in the application lives here.

No other module may contain a magic number. When behaviour needs adjusting -
recognition too strict, LED flickering, frame rate too low - this is the only
file to edit.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Config:
    # Frame sizes. Capture is what you see; detection runs on a smaller copy
    # because finding faces at 640x360 costs a quarter of what it costs at 720p.
    capture_width: int = 1280
    capture_height: int = 720
    detect_width: int = 640
    detect_height: int = 360

    # YuNet detection.
    detect_score_threshold: float = 0.85
    detect_nms_threshold: float = 0.3
    detect_top_k: int = 50

    # Identity. A face is accepted as a known person only when it clears
    # match_threshold AND beats the runner-up person by match_margin.
    match_threshold: float = 0.40
    match_margin: float = 0.05
    vote_window: int = 5
    reidentify_every: int = 10

    # Tracking a face between frames.
    track_iou_min: float = 0.3
    track_max_misses: int = 8

    # Enrollment quality gate. Samples are taken every enroll_sample_interval
    # frames rather than every frame, so the five samples span a second or two
    # of real movement instead of being five copies of one instant.
    enroll_samples: int = 5
    enroll_sample_interval: int = 8
    enroll_min_face_px: int = 100
    enroll_min_det_score: float = 0.9
    enroll_outlier_similarity: float = 0.5
    enroll_min_accepted: int = 3

    # LED. led_active_high describes the wiring, not a preference:
    #   False - anode to 3.3V, cathode through the resistor to the pin. The LED
    #           lights when the pin is driven LOW. This is how ours is wired.
    #   True  - anode through the resistor to the pin, cathode to ground. The
    #           LED lights when the pin is driven HIGH.
    # Getting this wrong inverts the light: on for strangers, off for friends.
    led_enabled: bool = True
    led_pin: int = 17
    led_active_high: bool = False
    led_hold_ms: int = 500

    # Directories, relative to the project root.
    models_dir: str = "models"
    data_dir: str = "data"


CONFIG = Config()
