#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Starts a packaged app, saves a screenshot and checks that nothing went
# missing in packaging (a Qt plugin or QML module that wasn't bundled shows
# up as a warning, not a crash). Used by CI on each platform:
#
#     tools/smoke_test.sh path/to/orbitsim screenshot.png
set -uo pipefail

app=${1:?usage: smoke_test.sh APP SCREENSHOT}
shot=${2:?usage: smoke_test.sh APP SCREENSHOT}
log=$(mktemp)

export QT_FORCE_STDERR_LOGGING=1 # Windows GUI apps otherwise log only to the debugger
"$app" --screenshot "$shot" --screenshot-delay 5000 2>&1 | tee "$log"
status=${PIPESTATUS[0]}

if [ "$status" -ne 0 ]; then
    echo "::error::$app exited with $status"
    exit 1
fi
if [ ! -s "$shot" ]; then
    echo "::error::no screenshot from $app"
    exit 1
fi
if grep -Ei 'is not installed|cannot load|could not load|failed to load|not found|qrc:.*error' \
    "$log"; then
    echo "::error::$app reported missing parts (above)"
    exit 1
fi
echo "Smoke test passed: $shot"
