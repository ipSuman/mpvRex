#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Legacy libmpv window embedding uses X11/XCB. On Wayland desktops,
# force the application and libmpv to use the X11 display path.
export QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY

exec "$SCRIPT_DIR/rex-player-bin" "$@"
