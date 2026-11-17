#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rebuilds src/app/assets/textures/earth_day.jpg from NASA's original.
#
# Source: Blue Marble: Next Generation with Topography, July 2004
#         (NASA Earth Observatory, Reto Stöckli), public domain.
#         https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-topography/
#
# The original is 21600 x 10800. We ship 8192 x 4096: the largest size that is
# safe as a single GPU texture on most hardware (16384 would need over 500 MB of
# video memory). Needs curl and ImageMagick 7.
set -eu

url="https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography/july/world.topo.200407.3x21600x10800.jpg"
out="$(dirname "$0")/../src/app/assets/textures/earth_day.jpg"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

curl -sSfL -o "$work/source.jpg" "$url"
magick "$work/source.jpg" -filter Lanczos -resize 8192x4096! -strip \
    -sampling-factor 4:2:0 -quality 92 "$out"
echo "Wrote $out"
