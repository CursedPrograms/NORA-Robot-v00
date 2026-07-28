#!/usr/bin/env bash
# =============================================================
# NORA pygame controller launcher (Linux/macOS/WSL)
#
# Creates a dedicated virtualenv next to this script on first run,
# installs dependencies from ../requirements.txt, then launches
# controller.py inside it. Re-running just reuses the existing venv.
#
# Usage:
#   ./controller.sh              connection picker
#   ./controller.sh --wifi       skip picker, connect over WiFi
#   ./controller.sh --bt /dev/rfcomm0
#
# First run: chmod +x controller.sh
# =============================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$SCRIPT_DIR/.venv"
REQS="$SCRIPT_DIR/../requirements.txt"
CONTROLLER="$SCRIPT_DIR/controller.py"

if [ ! -f "$VENV/bin/activate" ]; then
    echo "No venv found -- creating one and installing dependencies..."
    python3 -m venv "$VENV" || {
        echo "venv creation failed. Try: sudo apt install python3-venv python3-full"
        exit 1
    }
    "$VENV/bin/pip" install --quiet --upgrade pip
    "$VENV/bin/pip" install -r "$REQS" || {
        echo "dependency install failed"
        exit 1
    }
    echo "venv ready."
fi

exec "$VENV/bin/python" "$CONTROLLER" "$@"
