#!/bin/bash
set -euo pipefail

# --- Configuration ---
# Define application name and version. These should match your manifest.json.
APP_NAME="sensor-to-text"
APP_VERSION="1.0.1"
#ARCH="aarch64" # Default architecture, can be overridden by passing ARCH=armv7hf
ARCH="armv7hf"

# Define target architecture (aarch64 for newer chips, armv7hf for older ones).
# IMPORTANT: Adjust this based on your specific Axis camera's chip architecture.
# You can pass ARCH=armv7hf to the script (e.g., ./build.sh ARCH=armv7hf) to override.
TARGET_ARCH="${ARCH:-aarch64}" # Default to aarch64 if ARCH is not set.

docker build --tag "${APP_NAME}:${APP_VERSION}" --build-arg ARCH=${TARGET_ARCH} .

docker cp $(docker create "${APP_NAME}:${APP_VERSION}"):/opt/app ./build