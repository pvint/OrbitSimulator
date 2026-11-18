#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Rebuilds the app icon's PNG, Windows .ico and macOS .icns versions from
# src/app/assets/icons/orbitsim.svg. The results are checked in, so a build
# needs none of these tools.
#
# Needs: rsvg-convert (librsvg) and python3.
set -eu

cd "$(dirname "$0")/../src/app/assets/icons"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

for size in 16 24 32 48 64 128 256 512 1024; do
    rsvg-convert -w "$size" -h "$size" orbitsim.svg -o "$tmp/$size.png"
done

# PNGs for the Linux icon theme (hicolor).
mkdir -p png
for size in 16 32 48 64 128 256 512; do
    cp "$tmp/$size.png" "png/orbitsim-$size.png"
done

# Windows and macOS. Both formats are a small table of contents followed by
# PNG images, so a few lines of Python write them.
python3 - "$tmp" <<'PYTHON'
import struct, sys
tmp = sys.argv[1]

def png(size):
    with open(f"{tmp}/{size}.png", "rb") as f:
        return f.read()

# .ico: a 6-byte header, a 16-byte entry per image (a width or height of 0
# means 256), then the images. PNG images in .ico work since Windows Vista.
sizes = [16, 24, 32, 48, 64, 256]
images = [png(size) for size in sizes]
offset = 6 + 16 * len(sizes)
ico = struct.pack("<HHH", 0, 1, len(sizes))
for size, data in zip(sizes, images):
    ico += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
    offset += len(data)
with open("orbitsim.ico", "wb") as f:
    f.write(ico + b"".join(images))

# .icns: an 8-byte header, then (type, length, PNG) entries. These types
# hold PNG data at 32 to 1024 pixels.
entries = [(b"ic11", 32), (b"ic12", 64), (b"ic07", 128), (b"ic08", 256),
           (b"ic13", 256), (b"ic09", 512), (b"ic14", 512), (b"ic10", 1024)]
body = b""
for kind, size in entries:
    data = png(size)
    body += kind + struct.pack(">I", 8 + len(data)) + data
with open("orbitsim.icns", "wb") as f:
    f.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)
PYTHON

echo "Icons written to $(pwd)"
