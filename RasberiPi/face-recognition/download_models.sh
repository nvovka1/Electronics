#!/usr/bin/env bash
# Download the two ONNX models into models/, choosing the YuNet version that
# the installed OpenCV can actually load.
#
# Note the host: opencv_zoo keeps its models in Git LFS, and
# raw.githubusercontent.com serves a 130-byte text pointer for those instead of
# the file. media.githubusercontent.com/media/... resolves LFS properly.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p models

BASE="https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models"

OPENCV_VERSION="$(python3 -c 'import cv2; print(cv2.__version__)')"
MAJOR="${OPENCV_VERSION%%.*}"
REST="${OPENCV_VERSION#*.}"
MINOR="${REST%%.*}"

# The 2023mar YuNet changed its input and output shapes and needs OpenCV 4.8+.
if [ "$MAJOR" -gt 4 ] || { [ "$MAJOR" -eq 4 ] && [ "$MINOR" -ge 8 ]; }; then
  YUNET="face_detection_yunet_2023mar.onnx"
else
  YUNET="face_detection_yunet_2022mar.onnx"
fi
SFACE="face_recognition_sface_2021dec.onnx"

echo "OpenCV $OPENCV_VERSION -> $YUNET"

fetch() {
  url="$1"
  name="$2"
  min_bytes="$3"

  if [ -s "models/$name" ] && [ "$(stat -c%s "models/$name")" -ge "$min_bytes" ]; then
    echo "already have $name"
    return
  fi

  echo "downloading $name"
  curl -fsSL --retry 3 -o "models/$name.part" "$url"

  # A Git LFS pointer is valid text and a successful HTTP 200, so nothing fails
  # until OpenCV tries to parse it three steps later. Catch it here instead.
  if head -c 40 "models/$name.part" | grep -q "git-lfs"; then
    rm -f "models/$name.part"
    echo "ERROR: got a Git LFS pointer instead of $name." >&2
    echo "The URL must resolve LFS content (media.githubusercontent.com/media/...)." >&2
    exit 1
  fi

  actual="$(stat -c%s "models/$name.part")"
  if [ "$actual" -lt "$min_bytes" ]; then
    rm -f "models/$name.part"
    echo "ERROR: $name is only $actual bytes, expected at least $min_bytes." >&2
    exit 1
  fi

  mv "models/$name.part" "models/$name"
}

fetch "$BASE/face_detection_yunet/$YUNET" "$YUNET" 200000
fetch "$BASE/face_recognition_sface/$SFACE" "$SFACE" 30000000

# The code always opens models/yunet.onnx and models/sface.onnx, so the model
# version is a deployment detail rather than something baked into the source.
ln -sf "$YUNET" models/yunet.onnx
ln -sf "$SFACE" models/sface.onnx

echo
ls -l models/
