#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds a Linux AppImage from a configured and built release folder:
#
#     QMAKE=/path/to/Qt/6.x/gcc_64/bin/qmake tools/make_appimage.sh build
#
# Use the official Qt (qt.io or aqtinstall), not a distribution's: their
# plugin folders hold third-party plugins that can't be bundled. CI does this
# on the oldest supported Ubuntu, so the AppImage runs on as many systems as
# possible. Downloads linuxdeploy and its Qt plugin on first use.
set -eu

build=${1:?usage: make_appimage.sh BUILD_DIR}
root=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "$build" && pwd)
: "${QMAKE:?set QMAKE to the official Qt's qmake}"

tools="$build/appimage-tools"
mkdir -p "$tools"
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    file="$tools/$tool-x86_64.AppImage"
    if [ ! -x "$file" ]; then
        curl -fsSL -o "$file" \
            "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage"
        chmod +x "$file"
    fi
done

appdir="$build/AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr

version=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -n 1)
id=io.github.pvint.OrbitSimulator

export PATH="$tools:$PATH"                  # linuxdeploy finds its Qt plugin here
export QMAKE                                # which Qt to bundle
export QML_SOURCES_PATHS="$root/src/app/qml" # the QML imports to bundle
export APPIMAGE_EXTRACT_AND_RUN=1           # no FUSE needed (CI containers lack it)
export LDAI_OUTPUT="OrbitSimulator-$version-x86_64.AppImage"
export OUTPUT="$LDAI_OUTPUT"

cd "$build"
linuxdeploy-x86_64.AppImage --appdir "$appdir" --plugin qt --output appimage \
    --desktop-file "$appdir/usr/share/applications/$id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$id.png"
echo "Built $build/$LDAI_OUTPUT"
