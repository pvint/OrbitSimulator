#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes src/app/assets/textures/moon.jpg from NASA's CGI Moon Kit colour map.
#
# Source: CGI Moon Kit, NASA Scientific Visualization Studio, using Lunar
#         Reconnaissance Orbiter (LROC WAC) imagery; public domain.
#         https://svs.gsfc.nasa.gov/4720/
#         Download the 8K colour map in a browser (the app's texture was made
#         from lroc_color_16bit_srgb_8k.tif, 8192 x 4096, 16-bit sRGB), then
#         run:  tools/make_moon_texture.sh path/to/that.tif
#
# (It isn't downloaded automatically: svs.gsfc.nasa.gov's certificate chains
# to a Let's Encrypt root that older systems don't trust yet, and certificate
# checks are not something to switch off.)
#
# The map has longitude 0 (the centre of the side facing the Earth) in the
# middle and north at the top, the same layout as the Earth texture. Needs
# ImageMagick 7.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 lroc_color_16bit_srgb_8k.tif" >&2
    exit 1
fi
out="$(dirname "$0")/../src/app/assets/textures/moon.jpg"
magick "$1" -colorspace sRGB -depth 8 -resize 8192x4096! -strip \
    -sampling-factor 4:2:0 -quality 92 "$out"
echo "Wrote $out. Reconfigure (cmake --preset dev) so the build picks it up."
