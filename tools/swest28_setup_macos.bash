#!/usr/bin/env bash

set -euo pipefail

WORKSPACE_DIR="${HOME}/swest28"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Error: this setup script supports macOS only." >&2
  exit 1
fi

if [[ "$(uname -m)" != "arm64" ]]; then
  echo "Error: Zephyr's current getting started guide supports Apple Silicon macOS only." >&2
  exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
  echo "Error: Xcode Command Line Tools are required." >&2
  echo "Run 'xcode-select --install', complete the installation, and retry." >&2
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Error: Homebrew is required." >&2
  echo "Install it from https://brew.sh/ and run this script again." >&2
  exit 1
fi

brew install cmake ninja gperf python3 python-tk ccache qemu dtc libmagic wget openocd

mkdir "${WORKSPACE_DIR}"
pushd "${WORKSPACE_DIR}"
git clone https://github.com/soburi/s1c_zephyr_at_night
python3 -m venv .venv
source .venv/bin/activate
pip3 install west eclipse-zenoh

west init -l s1c_zephyr_at_night

west update
west packages pip --install
west zephyr-export

west sdk install -t arm-zephyr-eabi
pyocd pack install stm32c562ret6

cp s1c_zephyr_at_night/tools/zenohd_macos s1c_zephyr_at_night/tools/zenohd
chmod +x s1c_zephyr_at_night/tools/zenohd
