"""Live face recognition on the Raspberry Pi 5.

    ./run.sh -m src.main
    ./run.sh -m src.main --source file:tests/fixtures/face.jpg --headless

Commands are typed into this terminal, a single letter then Enter, because the
Pi is reached over SSH and has no keyboard of its own. The same letters also
work at the window for anyone who does attach one.

    q   quit
    e   enroll a person - the name is typed at the prompt that follows
    f   toggle fullscreen
    h   toggle the HUD
"""
from __future__ import annotations

import argparse
import os
import select
import sys
import time

import cv2

from src import overlay
from src.camera import CameraError, open_source
from src.config import CONFIG
from src.detector import Detector, ModelError
from src.enroll import EnrollmentSession
from src.face_database import FaceDatabase
from src.led import create_led
from src.recognizer import Recognizer
from src.tracker import Tracker

WINDOW = "Face Recognition"
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MESSAGE_SECONDS = 3.0
MAX_EMPTY_READS = 30
HELP = "Commands (type here, then Enter):  e = enroll   q = quit   h = hud   f = fullscreen"


# Pressing a key with a Ukrainian or Russian layout active sends a Cyrillic
# letter, not the Latin one printed on the key. Map the letters that share a
# physical key with a command, so the app works without switching layout.
CYRILLIC_KEYS = {
    "й": "q",  # й - same physical key as q
    "у": "e",  # у - same physical key as e
    "а": "f",  # а - same physical key as f
    "р": "h",  # р - same physical key as h
    "е": "e",  # е - a different key, but looks identical to e
}


def decode_input(raw: bytes) -> str:
    """Turn bytes typed at a terminal into text, whatever they are.

    A Windows console with a Cyrillic layout may send cp1251 rather than UTF-8,
    and a stray byte must never take the application down."""
    for encoding in ("utf-8", "cp1251"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", errors="replace")


def parse_command(raw: bytes):
    """The command letter in a typed line, or None if the line held none."""
    text = decode_input(raw).strip().lower()
    if not text:
        return None
    return CYRILLIC_KEYS.get(text[0], text[0])


def read_terminal_command():
    """One typed command from the terminal, or None if nothing was typed.

    This is what makes the app usable over SSH: the window never receives a
    keypress, because there is no keyboard attached to the Pi. select() with a
    zero timeout keeps the video running while nothing is being typed.

    Bytes are read straight from the file descriptor rather than through
    sys.stdin, because the text layer raises on anything that is not valid
    UTF-8 - which is exactly what a Cyrillic keyboard sends."""
    if not sys.stdin.isatty():
        return None
    ready, _, _ = select.select([sys.stdin], [], [], 0)
    if not ready:
        return None
    raw = os.read(sys.stdin.fileno(), 64)
    if not raw:
        return None
    return parse_command(raw)


def key_to_command(key: int):
    """Translate an OpenCV keycode into the same command letters."""
    if key in (27, ord("q")):
        return "q"
    if 32 <= key < 127:
        return chr(key).lower()
    return None


def parse_args():
    parser = argparse.ArgumentParser(description="Live face recognition")
    parser.add_argument(
        "--source",
        default="camera",
        help='"camera" (default), or "file:<path to an image or directory>"',
    )
    parser.add_argument(
        "--headless",
        action="store_true",
        help="do not open a window; print recognitions to the terminal instead",
    )
    return parser.parse_args()


def display_available() -> bool:
    return bool(os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY"))


def explain_missing_display() -> None:
    print(
        "No display found, so there is nothing to draw on.\n"
        "To put the window on the HDMI monitor, start the app with:\n"
        "    ./run.sh -m src.main\n"
        "or set these yourself first:\n"
        f"    export XDG_RUNTIME_DIR=/run/user/{os.getuid()}\n"
        "    export WAYLAND_DISPLAY=wayland-0\n"
        "Continuing without a window.\n",
        file=sys.stderr,
    )


def main() -> int:
    args = parse_args()
    headless = args.headless

    # The name prompt reads through sys.stdin, which raises on bytes that are
    # not valid UTF-8. A Cyrillic name typed from a Windows console can be
    # exactly that, and a name is not worth crashing over.
    try:
        sys.stdin.reconfigure(errors="replace")
    except (AttributeError, ValueError):
        pass

    if not headless and not display_available():
        explain_missing_display()
        headless = True

    models_dir = os.path.join(PROJECT_ROOT, CONFIG.models_dir)
    data_dir = os.path.join(PROJECT_ROOT, CONFIG.data_dir)

    try:
        detector = Detector(
            os.path.join(models_dir, "yunet.onnx"),
            (CONFIG.detect_width, CONFIG.detect_height),
            CONFIG.detect_score_threshold,
            CONFIG.detect_nms_threshold,
            CONFIG.detect_top_k,
        )
        recognizer = Recognizer(os.path.join(models_dir, "sface.onnx"))
        source = open_source(args.source, CONFIG.capture_width, CONFIG.capture_height)
    except (ModelError, CameraError) as error:
        print(error, file=sys.stderr)
        return 1

    database = FaceDatabase.load(data_dir, CONFIG.match_threshold, CONFIG.match_margin)
    tracker = Tracker(CONFIG.track_iou_min, CONFIG.track_max_misses, CONFIG.vote_window)
    led = create_led(
        CONFIG.led_enabled,
        CONFIG.led_pin,
        CONFIG.led_hold_ms,
        CONFIG.led_active_high,
    )

    if not headless:
        cv2.namedWindow(WINDOW, cv2.WINDOW_NORMAL)
        cv2.setWindowProperty(WINDOW, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    print(HELP)

    session = None
    show_hud = True
    fullscreen = True
    frame_number = 0
    empty_reads = 0
    fps = 0.0
    last_time = time.monotonic()
    message = ""
    message_until = 0.0
    printed: dict[int, str] = {}

    try:
        while True:
            frame = source.read()
            if frame is None:
                empty_reads += 1
                if empty_reads > MAX_EMPTY_READS:
                    print("The camera stopped returning frames.", file=sys.stderr)
                    return 1
                continue
            empty_reads = 0
            frame_number += 1

            detections = detector.detect(frame)
            tracks = tracker.update(detections)

            for track in tracks:
                if track.detection is None:
                    continue
                if track.needs_identity(frame_number, CONFIG.reidentify_every):
                    embedding = recognizer.embed(frame, track.detection)
                    result = database.match(embedding)
                    track.record_identity(
                        result.name, result.score, result.best_name, frame_number
                    )

            led.set(any(track.label is not None for track in tracks))

            if session is not None:
                if (
                    not session.done
                    and frame_number % CONFIG.enroll_sample_interval == 0
                ):
                    session.offer(
                        detections, lambda detection: recognizer.embed(frame, detection)
                    )
                if session.done:
                    kept, reason = session.finish(
                        CONFIG.enroll_outlier_similarity, CONFIG.enroll_min_accepted
                    )
                    if kept is None:
                        message = f"Enrollment failed: {reason}"
                    else:
                        database.add(session.name, kept)
                        database.save(data_dir)
                        message = f'Enrolled "{session.name}" ({len(kept)} samples)'
                    print(message)
                    message_until = time.monotonic() + MESSAGE_SECONDS
                    session = None

            command = read_terminal_command()

            if headless:
                for track in tracks:
                    label = track.label or "Unknown"
                    if printed.get(track.id) != label:
                        printed[track.id] = label
                        print(f"track {track.id}: {label} {track.last_score:.2f}")
                if args.source.startswith("file:"):
                    break
            else:
                for track in tracks:
                    overlay.draw_track(frame, track)

                if show_hud:
                    if database.is_empty():
                        second_line = "No one enrolled - press E"
                    else:
                        second_line = (
                            f"enrolled: {len(database.people())}   "
                            f"threshold: {CONFIG.match_threshold:.2f}"
                        )
                    overlay.draw_hud(
                        frame, [f"{fps:4.1f} fps   faces: {len(tracks)}", second_line]
                    )

                if session is not None:
                    overlay.draw_banner(frame, session.status())
                elif message and time.monotonic() < message_until:
                    overlay.draw_banner(frame, message)

                cv2.imshow(WINDOW, frame)

                # waitKey must be called every frame to pump the GUI, whether or
                # not anybody is pressing anything. A key at the window wins over
                # the terminal only because it is read second.
                command = key_to_command(cv2.waitKey(1) & 0xFF) or command

            if command == "q":
                break
            if command == "e" and session is None:
                # This blocks the video until a name is typed. That is a fair
                # trade for not having to write a text field in OpenCV.
                name = input("Name to enroll: ").strip()
                if name:
                    session = EnrollmentSession(
                        name,
                        CONFIG.enroll_samples,
                        CONFIG.enroll_min_det_score,
                        CONFIG.enroll_min_face_px,
                    )
            elif command == "h":
                show_hud = not show_hud
            elif command == "f" and not headless:
                fullscreen = not fullscreen
                cv2.setWindowProperty(
                    WINDOW,
                    cv2.WND_PROP_FULLSCREEN,
                    cv2.WINDOW_FULLSCREEN if fullscreen else cv2.WINDOW_NORMAL,
                )

            now = time.monotonic()
            instant = 1.0 / max(now - last_time, 1e-6)
            fps = instant if fps == 0.0 else 0.9 * fps + 0.1 * instant
            last_time = now
    except KeyboardInterrupt:
        pass
    finally:
        source.close()
        led.close()
        cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
