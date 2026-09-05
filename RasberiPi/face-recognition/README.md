# Face Recognition — Raspberry Pi 5

Live face recognition on a Raspberry Pi 5 with an IMX219 camera, shown fullscreen
on the HDMI monitor. Known people are labelled by name; everyone else is Unknown.
An LED lights while a known person is in frame.

**This is not a security system.** There is no anti-spoofing: a photograph of an
enrolled person held up to the camera is recognised as that person.

## How it works

A face is turned into 128 numbers (an "embedding") by OpenCV's SFace model. Two
pictures of the same person produce vectors that point in nearly the same
direction; different people point elsewhere. Recognising somebody is therefore
just measuring an angle — a cosine similarity between 0 and 1 — against the
samples saved during enrollment.

A face is accepted as a known person only when it clears `match_threshold`
**and** beats the second-best person by `match_margin`. Guessing between two
similar-scoring people is worse than saying Unknown.

## Setup on the Pi

```bash
sudo apt update
sudo apt install -y python3-picamera2 python3-opencv python3-numpy python3-gpiozero python3-pytest curl
./download_models.sh
```

`download_models.sh` picks the YuNet model version that matches the installed
OpenCV, and refuses to accept a Git LFS pointer in place of a real model.

## Running

```bash
./run.sh -m src.main
```

`run.sh` sets the display variables so the window lands on the HDMI monitor even
when launched over SSH. Running `python3 -m src.main` directly from SSH will not
show a window.

To type commands, connect with a terminal attached — `ssh -t admin@<pi>` — or the
app runs fine but ignores input.

Other modes:

```bash
./run.sh -m src.main --source file:tests/fixtures/face.jpg --headless   # no camera, no window
./run.sh tools/check_camera.py                                          # camera only, no recognition
./run.sh tools/grab_still.py                                            # save a test fixture
```

## Commands

The Pi has no keyboard of its own, so commands are typed into the SSH terminal
that launched the app — one letter, then Enter. The same letters work at the
window if you do attach a keyboard.

| Command | Action |
|---|---|
| `q` | Quit |
| `e` | Enroll — the name is typed at the prompt that follows (the video pauses meanwhile) |
| `f` | Toggle fullscreen |
| `h` | Toggle the HUD |

## Enrolling well

Enrollment takes five samples and refuses any frame with no face, more than one
face, a low-confidence detection, or a face narrower than 100 px. Samples that
disagree with the others are dropped; fewer than three survivors saves nothing.

Enroll in the light you will actually use. Enrolling the same name twice adds
samples rather than replacing them, so enrolling once with glasses and once
without makes both work.

## Tuning

Everything lives in `src/config.py`.

| Symptom | Change |
|---|---|
| It does not recognise you (score just under the threshold) | Lower `match_threshold` |
| It confuses two people | Raise `match_threshold` or `match_margin` |
| Labels flicker | Raise `vote_window` |
| Frame rate too low | Raise `reidentify_every`, or lower `capture_width`/`capture_height` |
| It refuses to enroll you | Lower `enroll_min_face_px`, or sit closer |

The HUD shows the live score next to every face, so tuning is a reading rather
than a guess.

## Wiring the LED

LED long leg (anode) → ~330 Ω resistor → physical pin 11 (BCM GPIO 17).
LED short leg (cathode) → any ground pin, e.g. physical pin 9.

Set `led_enabled = False` in `src/config.py` to run without one.

## Development

Edit on Windows, then push to the Pi:

```powershell
.\sync.ps1
```

Tests run on the Pi — the Windows machine has no NumPy or OpenCV:

```bash
ssh admin@192.168.0.111 "cd /home/admin/face-recognition && python3 -m pytest tests/ -v"
```

## Troubleshooting

| Problem | Cause |
|---|---|
| No window appears | Started without `./run.sh`; or no Wayland socket in `/run/user/1000/` |
| `No camera found` | Ribbon cable, or the camera is not enabled — check `rpicam-hello --list-cameras` |
| `Device or resource busy` | Another instance still has the camera. Only one process can own it. Find it with `pgrep -af '[s]rc.main\|[c]heck_camera'` and `kill` the PID |
| ONNX shape error on startup | The YuNet model does not match this OpenCV — delete `models/` and re-run `./download_models.sh` |
| Model file is ~130 bytes | A Git LFS pointer was downloaded instead of the model — re-run `./download_models.sh` |
| Faces look blue | Change `"RGB888"` to `"BGR888"` in `src/camera.py` |
| Typed commands do nothing | Connect with `ssh -t` so the app has a real terminal on stdin |
