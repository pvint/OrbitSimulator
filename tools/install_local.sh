#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a release, test it, and install it system-wide (Linux), so the app
# in the menu is always the latest build.
#
#   tools/install_local.sh            install into /usr/local (needs sudo)
#   PREFIX=/opt/orbitsim tools/install_local.sh
#
# Installs the GUI and the CLI, the menu entry, the AppStream description and
# the icons. Uses the system's Qt (nothing is bundled). /usr/local stays
# separate from the package manager's /usr; to remove everything again:
#   sudo xargs rm -v < build/release/install_manifest.txt
set -eu
cd "$(dirname "$0")/.."
prefix="${PREFIX:-/usr/local}"

# A ctest earlier on the PATH may be broken (pip installs one); prefer the
# one that came with CMake.
ctest=ctest
[ -x /usr/bin/ctest ] && ctest=/usr/bin/ctest

cmake --preset release -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build --preset release
"$ctest" --preset release --output-on-failure
sudo cmake --install build/release

# Let the desktop notice the new menu entry and icon straight away.
if command -v update-desktop-database >/dev/null 2>&1; then
    sudo update-desktop-database -q "$prefix/share/applications" || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    sudo gtk-update-icon-cache -q -t "$prefix/share/icons/hicolor" || true
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 >/dev/null 2>&1 || true
fi
echo "Installed into $prefix: run orbitsim, or find Orbit Simulator in the menu."
