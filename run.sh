#!/usr/bin/env bash
# =============================================================
# NORA launcher — website or pygame controller
#
# Usage:
#   ./run.sh            interactive menu
#   ./run.sh web        open the web dashboard in the browser
#   ./run.sh app        run the pygame controller (scripts/controller.py,
#                       via scripts/controller.sh which owns its own venv)
# =============================================================

set -u

NORA_URL="http://192.168.4.1:5002"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTROLLER_LAUNCHER="$SCRIPT_DIR/scripts/controller.sh"

launch_web() {
    echo "Opening $NORA_URL ..."
    echo "(make sure this PC is connected to the NORA WiFi network)"
    xdg-open "$NORA_URL" >/dev/null 2>&1 &
}

launch_app() {
    if [ ! -f "$CONTROLLER_LAUNCHER" ]; then
        echo "not found: $CONTROLLER_LAUNCHER"
        exit 1
    fi
    exec bash "$CONTROLLER_LAUNCHER"
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
