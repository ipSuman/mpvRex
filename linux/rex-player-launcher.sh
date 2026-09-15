#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Legacy libmpv window embedding uses X11/XCB. On Wayland desktops,
# force the application and libmpv to use the X11 display path.
export QT_QPA_PLATFORM=xcb
export WAYLAND_DISPLAY=
export LC_NUMERIC=C

exec "$SCRIPT_DIR/rex-player-bin" "$@"
