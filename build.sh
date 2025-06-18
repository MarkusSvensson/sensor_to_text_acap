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

# --- Install .eap on AXIS device ---
DEVICE_IP="192.168.68.59"
DEVICE_USER="root"
DEVICE_PASS="pass"  # Replace with actual password
EAP_FILE=$(find ./build -name '*.eap' | head -n 1)

if [ -f "$EAP_FILE" ]; then
  echo "Installing $EAP_FILE on device at $DEVICE_IP..."
  #curl -k -u "$DEVICE_USER:$DEVICE_PASS" --digest -X POST \
  #-F "file=@${EAP_FILE}" \
  #"https://${DEVICE_IP}/axis-cgi/applications/install.cgi"
  #curl -k -u root:pass \
  #   -F "app=@./build/app/Sensor_to_Text_App_1_0_1_armv7hf.eap" \
  #   https://192.168.68.59/axis-cgi/applications/upload.cgi

  curl -k --anyauth -u "$DEVICE_USER:$DEVICE_PASS" -F packfil=@./build/app/Sensor_to_Text_App_1_0_1_armv7hf.eap https://${DEVICE_IP}/axis-cgi/applications/upload.cgi
  curl -k --anyauth -u "$DEVICE_USER:$DEVICE_PASS" https://${DEVICE_IP}/axis-cgi/applications/control.cgi?package=sensor_to_text&action=start
else
  echo "ERROR: No .eap file found in ./build"
  exit 1
fi