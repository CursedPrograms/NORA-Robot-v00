#!/usr/bin/env bash
# =============================================================
# NORA launcher — website or pygame controller
#
# Usage:
#   ./nora.sh            interactive menu
#   ./nora.sh web        open the web dashboard in the browser
#   ./nora.sh app        run the pygame controller (main.py)
#
# Put this file in the same folder as main.py, then once:
#   chmod +x nora.sh
# =============================================================

set -u

NORA_URL="http://192.168.4.1:5002"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$SCRIPT_DIR/venv"
MAIN="$SCRIPT_DIR/main.py"

launch_web() {
    echo "Opening $NORA_URL ..."
    echo "(make sure this PC is connected to the NORA WiFi network)"
    xdg-open "$NORA_URL" >/dev/null 2>&1 &
}

launch_app() {
    # Create the venv on first run if it doesn't exist yet
    if [ ! -f "$VENV/bin/activate" ]; then
        echo "No venv found — creating one and installing dependencies..."
        python3 -m venv "$VENV" || {
            echo "venv creation failed. Try: sudo apt install python3-venv python3-full"
            exit 1
        }
        "$VENV/bin/pip" install --quiet --upgrade pip
        "$VENV/bin/pip" install pygame requests pyserial || {
            echo "dependency install failed"; exit 1;
        }
        echo "venv ready."
    fi

    if [ ! -f "$MAIN" ]; then
        echo "main.py not found next to this script ($MAIN)"
        exit 1
    fi

    exec "$VENV/bin/python" "$MAIN"
}

case "${1:-}" in
    web) launch_web ;;
    app) launch_app ;;
    *)
        echo "==============================="
        echo "   NORA — pick a controller"
        echo "==============================="
        echo "  1) Website  (browser dashboard)"
        echo "  2) App      (pygame controller)"
        echo "  q) quit"
        read -rp "> " pick
        case "$pick" in
            1) launch_web ;;
            2) launch_app ;;
            *) echo "bye" ;;
        esac
        ;;
esac
