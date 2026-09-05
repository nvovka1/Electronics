"""All drawing. No state, no model calls, no decisions about identity.

The overlay deliberately shows the raw similarity score next to every face. That
turns "why doesn't it recognise me" from guesswork into a reading: if your face
sits at 0.34 and the threshold is 0.40, you know exactly what to change.
"""
from __future__ import annotations

import cv2

GREEN = (0, 200, 0)
RED = (0, 0, 220)
GREY = (160, 160, 160)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

FONT = cv2.FONT_HERSHEY_SIMPLEX
LABEL_SCALE = 0.6
LABEL_THICKNESS = 1
SUB_SCALE = 0.45
BOX_THICKNESS = 2
BANNER_HEIGHT = 44


def text_origin(box, frame_height: int, text_height: int, above_gap: int = 8):
    """Where to put a label so it stays on screen when the face is at an edge.

    Above the box normally; below it when the face is against the top of the
    frame; clamped inside the frame either way."""
    x, y, _, h = box
    if y - above_gap - text_height >= 0:
        return x, y - above_gap
    return x, min(y + h + above_gap + text_height, frame_height - 2)


def draw_track(frame, track) -> None:
    x, y, w, h = track.box
    known = track.label is not None
    colour = GREEN if known else RED

    cv2.rectangle(frame, (x, y), (x + w, y + h), colour, BOX_THICKNESS)

    if known:
        caption = f"{track.label} {track.last_score:.2f}"
        subcaption = None
    else:
        caption = "Unknown"
        subcaption = (
            f"best: {track.last_best_name} {track.last_score:.2f}"
            if track.last_best_name
            else None
        )

    (text_width, text_height), _ = cv2.getTextSize(
        caption, FONT, LABEL_SCALE, LABEL_THICKNESS
    )
    tx, ty = text_origin(track.box, frame.shape[0], text_height)

    cv2.rectangle(
        frame, (tx, ty - text_height - 4), (tx + text_width + 6, ty + 4), BLACK, cv2.FILLED
    )
    cv2.putText(
        frame, caption, (tx + 3, ty), FONT, LABEL_SCALE, colour, LABEL_THICKNESS, cv2.LINE_AA
    )

    if subcaption:
        cv2.putText(
            frame, subcaption, (tx + 3, ty + 16), FONT, SUB_SCALE, GREY, 1, cv2.LINE_AA
        )


def draw_hud(frame, lines) -> None:
    y = 24
    for line in lines:
        cv2.putText(frame, line, (12, y), FONT, 0.5, WHITE, 1, cv2.LINE_AA)
        y += 20


def draw_banner(frame, text: str) -> None:
    height, width = frame.shape[:2]
    cv2.rectangle(frame, (0, 0), (width, BANNER_HEIGHT), BLACK, cv2.FILLED)
    cv2.putText(frame, text, (16, 30), FONT, 0.7, WHITE, 2, cv2.LINE_AA)
